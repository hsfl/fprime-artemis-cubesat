#!/usr/bin/env python3
"""Local closed-loop emulator for ArtemisRpiTeensy_N2.

This script emulates the RPi <-> satellite Teensy <-> RF <-> ground Teensy link
on a single host. It creates three pseudo UART devices:

- app UART: passed to ArtemisRpiTeensyDeployment (-d ...)
- gds UART: passed to fprime-gds (--uart-device ...)
- payload UART: passed to the ground payload receiver (--port ...)

Supported link modes:
- channelized: current UART channel mux plus RF segment/reassemble emulation
- direct: raw byte bridge app<->gds for older no-mux topologies
- legacy-wrapper: compatibility alias for channelized
"""

from __future__ import annotations

import argparse
import errno
import os
import platform
import pty
import re
import selectors
import signal
import subprocess
import shutil
import sys
import time
import tty
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


FRAME_MAGIC_0 = 0xD4
FRAME_MAGIC_1 = 0xC3
CHANNEL_CCSDS = 0
CHANNEL_PAYLOAD = 1
CHANNEL_TEENSY_LOCAL = 2
CHANNEL_RF_COUNT = 2
CHANNEL_COUNT = 3
FRAME_MAX_PAYLOAD = 220
FRAME_TIMEOUT_S = 0.250

RF_SEGMENT_MAGIC_CCSDS = 0xA5
RF_SEGMENT_MAGIC_PAYLOAD = 0xA6
RF_PACKET_MAX_LEN = 49
RF_SEGMENT_HEADER_LEN = 5
RF_SEGMENT_MAX_DATA = RF_PACKET_MAX_LEN - RF_SEGMENT_HEADER_LEN
RF_REASSEMBLY_TIMEOUT_S = 0.500

DEFAULT_UPLINK_FLUSH_MS = 8
DEFAULT_GUI_PORT = 5050
DEFAULT_UART_BAUD = 115200
DEFAULT_MAX_LOG_RUNS = 5
DEPLOYMENT_NAME = "ArtemisRpiTeensyDeployment"
DICT_BASENAME = f"{DEPLOYMENT_NAME}TopologyDictionary.json"
TIMESTAMP_DIR_RE = re.compile(
    r"^\d{4}(?:[-_])\d{2}(?:[-_])\d{2}(?:T|-)\d{2}(?:[:_])\d{2}(?:[:_])\d{2}(?:\.\d+)?$"
)

N2_MAGIC = b"N2"
N2_TYPE_DATA = 2
N2_DATA_MIN_BYTES = 6
N2_DATA_INDEX_OFFSET = 4
N2_MAX_DATA_INDEX = 0xFFFF


def crc16_ccitt(payload: bytes) -> int:
    crc = 0xFFFF
    for value in payload:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def is_valid_channel(channel: int) -> bool:
    return 0 <= channel < CHANNEL_COUNT


def is_rf_channel(channel: int) -> bool:
    return 0 <= channel < CHANNEL_RF_COUNT


def rf_magic_for_channel(channel: int) -> int:
    return RF_SEGMENT_MAGIC_PAYLOAD if channel == CHANNEL_PAYLOAD else RF_SEGMENT_MAGIC_CCSDS


def channel_for_rf_magic(magic: int) -> Optional[int]:
    if magic == RF_SEGMENT_MAGIC_CCSDS:
        return CHANNEL_CCSDS
    if magic == RF_SEGMENT_MAGIC_PAYLOAD:
        return CHANNEL_PAYLOAD
    return None


class UartFrameParser:
    WAIT_MAGIC_0 = 0
    WAIT_MAGIC_1 = 1
    WAIT_CHANNEL = 2
    WAIT_LEN_LO = 3
    WAIT_LEN_HI = 4
    WAIT_PAYLOAD = 5
    WAIT_CRC_LO = 6
    WAIT_CRC_HI = 7

    def __init__(self) -> None:
        self.state = self.WAIT_MAGIC_0
        self.channel = CHANNEL_CCSDS
        self.frame_length = 0
        self.frame_index = 0
        self.payload = bytearray()
        self.crc_lo = 0
        self.frame_crc = 0
        self.last_frame_byte_ts: Optional[float] = None

        self.crc_drops = 0
        self.framing_drops = 0
        self.timeout_events = 0

    def _reset(self, timeout_reset: bool) -> None:
        self.state = self.WAIT_MAGIC_0
        self.channel = CHANNEL_CCSDS
        self.frame_length = 0
        self.frame_index = 0
        self.payload.clear()
        self.crc_lo = 0
        self.frame_crc = 0
        self.last_frame_byte_ts = None
        if timeout_reset:
            self.timeout_events += 1

    def poll_timeout(self, now: float) -> None:
        if self.state == self.WAIT_MAGIC_0 or self.last_frame_byte_ts is None:
            return
        if now - self.last_frame_byte_ts > FRAME_TIMEOUT_S:
            self._reset(timeout_reset=True)

    def feed(self, data: bytes, now: float) -> list[tuple[int, bytes]]:
        outputs: list[tuple[int, bytes]] = []
        for b in data:
            if self.state != self.WAIT_MAGIC_0 and self.last_frame_byte_ts is not None:
                if now - self.last_frame_byte_ts > FRAME_TIMEOUT_S:
                    self._reset(timeout_reset=True)
            self.last_frame_byte_ts = now

            if self.state == self.WAIT_MAGIC_0:
                if b == FRAME_MAGIC_0:
                    self.state = self.WAIT_MAGIC_1
                continue

            if self.state == self.WAIT_MAGIC_1:
                if b == FRAME_MAGIC_1:
                    self.state = self.WAIT_CHANNEL
                else:
                    self._reset(timeout_reset=False)
                continue

            if self.state == self.WAIT_CHANNEL:
                if is_valid_channel(b):
                    self.channel = b
                    self.state = self.WAIT_LEN_LO
                else:
                    self.framing_drops += 1
                    self._reset(timeout_reset=False)
                continue

            if self.state == self.WAIT_LEN_LO:
                self.frame_length = b
                self.state = self.WAIT_LEN_HI
                continue

            if self.state == self.WAIT_LEN_HI:
                self.frame_length |= b << 8
                if self.frame_length == 0 or self.frame_length > FRAME_MAX_PAYLOAD:
                    self.framing_drops += 1
                    self._reset(timeout_reset=False)
                else:
                    self.payload = bytearray(self.frame_length)
                    self.frame_index = 0
                    self.state = self.WAIT_PAYLOAD
                continue

            if self.state == self.WAIT_PAYLOAD:
                self.payload[self.frame_index] = b
                self.frame_index += 1
                if self.frame_index >= self.frame_length:
                    self.state = self.WAIT_CRC_LO
                continue

            if self.state == self.WAIT_CRC_LO:
                self.crc_lo = b
                self.state = self.WAIT_CRC_HI
                continue

            if self.state == self.WAIT_CRC_HI:
                self.frame_crc = self.crc_lo | (b << 8)
                payload = bytes(self.payload)
                if crc16_ccitt(payload) != self.frame_crc:
                    self.crc_drops += 1
                else:
                    outputs.append((self.channel, payload))
                self._reset(timeout_reset=False)

        return outputs


class RfSegmenter:
    def __init__(self) -> None:
        self.next_msg_id = 0

    def segment(self, channel: int, payload: bytes) -> list[bytes]:
        if not payload:
            return []
        if not is_rf_channel(channel):
            return []
        if len(payload) > FRAME_MAX_PAYLOAD:
            return []

        seg_count = (len(payload) + RF_SEGMENT_MAX_DATA - 1) // RF_SEGMENT_MAX_DATA
        if seg_count <= 0 or seg_count > 255:
            return []

        msg_id = self.next_msg_id
        self.next_msg_id = (self.next_msg_id + 1) & 0xFF

        packets: list[bytes] = []
        offset = 0
        for seg_idx in range(seg_count):
            chunk = payload[offset : offset + RF_SEGMENT_MAX_DATA]
            header = bytes(
                [
                    rf_magic_for_channel(channel),
                    msg_id,
                    seg_idx,
                    seg_count,
                    len(chunk),
                ]
            )
            packets.append(header + chunk)
            offset += len(chunk)
        return packets


class RfReassembler:
    def __init__(self) -> None:
        self.active = False
        self.expected_channel = CHANNEL_CCSDS
        self.expected_msg_id = 0
        self.expected_seg_idx = 0
        self.expected_seg_count = 0
        self.reassembly = bytearray()
        self.last_segment_ts: Optional[float] = None

        self.framing_drops = 0
        self.reassembly_timeouts = 0
        self.reassembly_drops = 0
        self.oversize_drops = 0

    def _reset(self, timeout_reset: bool, drop_reset: bool) -> None:
        self.active = False
        self.expected_channel = CHANNEL_CCSDS
        self.expected_msg_id = 0
        self.expected_seg_idx = 0
        self.expected_seg_count = 0
        self.reassembly.clear()
        self.last_segment_ts = None
        if timeout_reset:
            self.reassembly_timeouts += 1
        if drop_reset:
            self.reassembly_drops += 1

    def poll_timeout(self, now: float) -> None:
        if not self.active or self.last_segment_ts is None:
            return
        if now - self.last_segment_ts > RF_REASSEMBLY_TIMEOUT_S:
            self._reset(timeout_reset=True, drop_reset=True)

    def feed(self, packet: bytes, now: float) -> Optional[tuple[int, bytes]]:
        self.poll_timeout(now)

        if len(packet) < RF_SEGMENT_HEADER_LEN:
            self.framing_drops += 1
            return None

        channel = channel_for_rf_magic(packet[0])
        if channel is None:
            self.framing_drops += 1
            return None

        msg_id = packet[1]
        seg_idx = packet[2]
        seg_count = packet[3]
        chunk_len = packet[4]

        if seg_count == 0 or seg_idx >= seg_count:
            self.framing_drops += 1
            return None
        if chunk_len == 0:
            self.framing_drops += 1
            return None
        if len(packet) != RF_SEGMENT_HEADER_LEN + chunk_len:
            self.framing_drops += 1
            return None

        if not self.active:
            if seg_idx != 0:
                self.reassembly_drops += 1
                return None
            self.active = True
            self.expected_channel = channel
            self.expected_msg_id = msg_id
            self.expected_seg_idx = 0
            self.expected_seg_count = seg_count
            self.reassembly.clear()

        if (
            msg_id != self.expected_msg_id
            or channel != self.expected_channel
            or seg_count != self.expected_seg_count
            or seg_idx != self.expected_seg_idx
        ):
            self._reset(timeout_reset=False, drop_reset=True)
            if seg_idx != 0:
                return None
            self.active = True
            self.expected_channel = channel
            self.expected_msg_id = msg_id
            self.expected_seg_idx = 0
            self.expected_seg_count = seg_count
            self.reassembly.clear()

        chunk = packet[RF_SEGMENT_HEADER_LEN:]
        if len(self.reassembly) + len(chunk) > FRAME_MAX_PAYLOAD:
            self.oversize_drops += 1
            self._reset(timeout_reset=False, drop_reset=True)
            return None

        self.reassembly.extend(chunk)
        self.expected_seg_idx += 1
        self.last_segment_ts = now

        if seg_idx + 1 == seg_count:
            complete = bytes(self.reassembly)
            complete_channel = self.expected_channel
            self._reset(timeout_reset=False, drop_reset=False)
            return complete_channel, complete
        return None


class BurstAggregator:
    def __init__(self, flush_timeout_s: float) -> None:
        self.flush_timeout_s = flush_timeout_s
        self.buf = bytearray()
        self.last_byte_ts: Optional[float] = None

    def _emit(self) -> Optional[bytes]:
        if not self.buf:
            return None
        out = bytes(self.buf)
        self.buf.clear()
        self.last_byte_ts = None
        return out

    def feed(self, data: bytes, now: float) -> list[bytes]:
        outputs: list[bytes] = []
        for b in data:
            self.buf.append(b)
            self.last_byte_ts = now
            if len(self.buf) >= FRAME_MAX_PAYLOAD:
                item = self._emit()
                if item is not None:
                    outputs.append(item)
        return outputs

    def poll(self, now: float) -> Optional[bytes]:
        if not self.buf or self.last_byte_ts is None:
            return None
        if now - self.last_byte_ts >= self.flush_timeout_s:
            return self._emit()
        return None


@dataclass
class LoopStats:
    app_uart_bytes_in: int = 0
    gds_uart_bytes_in: int = 0
    payload_uart_bytes_in: int = 0
    app_frames_in: int = 0
    gds_messages_in: int = 0
    payload_messages_in: int = 0
    rf_packets_app_to_gds: int = 0
    rf_packets_gds_to_app: int = 0
    rf_packets_payload_to_app: int = 0
    gds_bytes_out: int = 0
    payload_bytes_out: int = 0
    app_bytes_out: int = 0
    payload_bytes_observed: int = 0
    payload_data_packets_dropped: int = 0
    local_frames_observed: int = 0


def build_uart_frame(channel: int, payload: bytes) -> bytes:
    if not is_valid_channel(channel):
        raise ValueError("channel out of range")
    if len(payload) == 0 or len(payload) > FRAME_MAX_PAYLOAD:
        raise ValueError("payload length out of range")
    crc = crc16_ccitt(payload)
    length = len(payload)
    return bytes(
        [
            FRAME_MAGIC_0,
            FRAME_MAGIC_1,
            channel,
            length & 0xFF,
            (length >> 8) & 0xFF,
        ]
    ) + payload + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


class EmulationLoop:
    def __init__(
        self,
        app_cmd: Optional[list[str]],
        gds_cmd: Optional[list[str]],
        uplink_flush_ms: int,
        link_mode: str,
        drop_payload_data_index: Optional[int] = None,
    ) -> None:
        self.app_cmd = app_cmd
        self.gds_cmd = gds_cmd
        self.uplink_flush_s = uplink_flush_ms / 1000.0
        self.link_mode = link_mode
        if drop_payload_data_index is not None and not (
            0 <= drop_payload_data_index <= N2_MAX_DATA_INDEX
        ):
            raise ValueError("payload DATA packet index out of range")
        self.drop_payload_data_index = drop_payload_data_index
        self._payload_data_drop_injected = False

        self.stop_requested = False
        self.exit_code = 0

        self.selector = selectors.DefaultSelector()
        self.children: list[subprocess.Popen[bytes]] = []

        self.app_master_fd: Optional[int] = None
        self.app_slave_fd: Optional[int] = None
        self.gds_master_fd: Optional[int] = None
        self.gds_slave_fd: Optional[int] = None
        self.payload_master_fd: Optional[int] = None
        self.payload_slave_fd: Optional[int] = None

        self.app_uart_device = ""
        self.gds_uart_device = ""
        self.payload_uart_device = ""

        self.pending_writes: dict[int, bytearray] = {}

        self.app_uart_parser = UartFrameParser()
        self.sat_to_ground_segmenter = RfSegmenter()
        self.ground_reassembler = RfReassembler()

        self.gds_burst_aggregator = BurstAggregator(self.uplink_flush_s)
        self.payload_burst_aggregator = BurstAggregator(self.uplink_flush_s)
        self.ground_to_sat_segmenter = RfSegmenter()
        self.sat_reassembler = RfReassembler()

        self.stats = LoopStats()

    def _spawn_pty(self) -> tuple[int, int, str]:
        master_fd, slave_fd = pty.openpty()
        os.set_blocking(master_fd, False)
        slave_path = os.ttyname(slave_fd)
        self.selector.register(master_fd, selectors.EVENT_READ)
        self.pending_writes[master_fd] = bytearray()
        return master_fd, slave_fd, slave_path

    def _launch_child(self, cmd: list[str], name: str) -> None:
        proc = subprocess.Popen(
            cmd,
            start_new_session=True,
        )
        self.children.append(proc)
        print(f"[emulation] launched {name}: {' '.join(cmd)}")

    def setup(self) -> None:
        self.app_master_fd, self.app_slave_fd, self.app_uart_device = self._spawn_pty()
        self.gds_master_fd, self.gds_slave_fd, self.gds_uart_device = self._spawn_pty()
        self.payload_master_fd, self.payload_slave_fd, self.payload_uart_device = self._spawn_pty()
        # The receiver is launched after discovering the marker below. Disable
        # PTY echo immediately so downlink bytes cannot loop back as uplink
        # during that brief startup gap.
        tty.setraw(self.payload_slave_fd)

        print(f"[emulation] app UART device: {self.app_uart_device}")
        print(f"[emulation] gds UART device: {self.gds_uart_device}")
        print(f"[emulation] payload UART device: {self.payload_uart_device}")
        # Machine-readable contract used by local-demo orchestration. Flush so
        # a receiver can be launched promptly even when stdout is redirected.
        print(f"PAYLOAD_UART_DEVICE={self.payload_uart_device}", flush=True)

        if self.app_cmd is not None:
            cmd = [
                item.format(
                    app_uart=self.app_uart_device,
                    gds_uart=self.gds_uart_device,
                    payload_uart=self.payload_uart_device,
                )
                for item in self.app_cmd
            ]
            self._launch_child(cmd, "flight app")
        if self.gds_cmd is not None:
            cmd = [
                item.format(
                    app_uart=self.app_uart_device,
                    gds_uart=self.gds_uart_device,
                    payload_uart=self.payload_uart_device,
                )
                for item in self.gds_cmd
            ]
            self._launch_child(cmd, "gds")

        signal.signal(signal.SIGINT, self._handle_signal)
        signal.signal(signal.SIGTERM, self._handle_signal)

    def _handle_signal(self, signum: int, _frame: object) -> None:
        print(f"\n[emulation] received signal {signum}, shutting down")
        self.stop_requested = True

    def _read_fd(self, fd: int) -> bytes:
        try:
            return os.read(fd, 4096)
        except BlockingIOError:
            return b""
        except OSError as exc:
            if exc.errno in (errno.EIO, errno.EBADF):
                return b""
            raise

    def _queue_write(self, fd: int, payload: bytes) -> None:
        if not payload:
            return
        pending = self.pending_writes[fd]
        if pending:
            pending.extend(payload)
            return
        try:
            written = os.write(fd, payload)
            if written < len(payload):
                pending.extend(payload[written:])
        except BlockingIOError:
            pending.extend(payload)
        except OSError as exc:
            if exc.errno not in (errno.EIO, errno.EBADF):
                raise
            pending.extend(payload)

    def _flush_pending(self) -> None:
        for fd, pending in self.pending_writes.items():
            if not pending:
                continue
            try:
                written = os.write(fd, pending)
                if written > 0:
                    del pending[:written]
            except BlockingIOError:
                continue
            except OSError as exc:
                if exc.errno not in (errno.EIO, errno.EBADF):
                    raise

    def _process_app_to_gds(self, data: bytes, now: float) -> None:
        self.stats.app_uart_bytes_in += len(data)
        if self.link_mode == "direct":
            self.stats.gds_bytes_out += len(data)
            self._queue_write(self.gds_master_fd, data)  # type: ignore[arg-type]
            return

        app_frames = self.app_uart_parser.feed(data, now)
        for channel, frame in app_frames:
            self.stats.app_frames_in += 1
            if channel == CHANNEL_TEENSY_LOCAL:
                self.stats.local_frames_observed += 1
                continue
            rf_packets = self.sat_to_ground_segmenter.segment(channel, frame)
            self.stats.rf_packets_app_to_gds += len(rf_packets)
            for packet in rf_packets:
                reassembled = self.ground_reassembler.feed(packet, now)
                if reassembled is None:
                    continue
                out_channel, payload = reassembled
                if out_channel == CHANNEL_CCSDS:
                    self.stats.gds_bytes_out += len(payload)
                    self._queue_write(self.gds_master_fd, payload)  # type: ignore[arg-type]
                elif out_channel == CHANNEL_PAYLOAD:
                    self.stats.payload_bytes_observed += len(payload)
                    if self._should_drop_payload_data(payload):
                        self.stats.payload_data_packets_dropped += 1
                        packet_index = int.from_bytes(
                            payload[N2_DATA_INDEX_OFFSET : N2_DATA_INDEX_OFFSET + 2],
                            byteorder="little",
                        )
                        print(
                            "[emulation] injected payload DATA drop: "
                            f"packet_index={packet_index}",
                            flush=True,
                        )
                        continue
                    self.stats.payload_bytes_out += len(payload)
                    self._queue_write(self.payload_master_fd, payload)  # type: ignore[arg-type]

    def _should_drop_payload_data(self, payload: bytes) -> bool:
        if self.drop_payload_data_index is None or self._payload_data_drop_injected:
            return False
        if (
            len(payload) < N2_DATA_MIN_BYTES
            or payload[:2] != N2_MAGIC
            or payload[2] != N2_TYPE_DATA
        ):
            return False
        packet_index = int.from_bytes(
            payload[N2_DATA_INDEX_OFFSET : N2_DATA_INDEX_OFFSET + 2],
            byteorder="little",
        )
        if packet_index != self.drop_payload_data_index:
            return False
        self._payload_data_drop_injected = True
        return True

    def _process_ground_message_to_app(self, channel: int, message: bytes, now: float) -> None:
        if channel == CHANNEL_CCSDS:
            self.stats.gds_messages_in += 1
        elif channel == CHANNEL_PAYLOAD:
            self.stats.payload_messages_in += 1
        rf_packets = self.ground_to_sat_segmenter.segment(channel, message)
        if channel == CHANNEL_CCSDS:
            self.stats.rf_packets_gds_to_app += len(rf_packets)
        elif channel == CHANNEL_PAYLOAD:
            self.stats.rf_packets_payload_to_app += len(rf_packets)
        for packet in rf_packets:
            reassembled = self.sat_reassembler.feed(packet, now)
            if reassembled is not None:
                channel, payload = reassembled
                framed = build_uart_frame(channel, payload)
                self.stats.app_bytes_out += len(framed)
                self._queue_write(self.app_master_fd, framed)  # type: ignore[arg-type]

    def _process_gds_message_to_app(self, message: bytes, now: float) -> None:
        self._process_ground_message_to_app(CHANNEL_CCSDS, message, now)

    def _process_payload_message_to_app(self, message: bytes, now: float) -> None:
        self._process_ground_message_to_app(CHANNEL_PAYLOAD, message, now)

    def _process_gds_to_app(self, data: bytes, now: float) -> None:
        self.stats.gds_uart_bytes_in += len(data)
        if self.link_mode == "direct":
            self.stats.gds_messages_in += 1
            self.stats.app_bytes_out += len(data)
            self._queue_write(self.app_master_fd, data)  # type: ignore[arg-type]
            return

        messages = self.gds_burst_aggregator.feed(data, now)
        for message in messages:
            self._process_gds_message_to_app(message, now)

    def _process_payload_to_app(self, data: bytes, now: float) -> None:
        self.stats.payload_uart_bytes_in += len(data)
        if self.link_mode == "direct":
            return

        messages = self.payload_burst_aggregator.feed(data, now)
        for message in messages:
            self._process_payload_message_to_app(message, now)

    def _poll_children(self) -> None:
        for proc in self.children:
            code = proc.poll()
            if code is not None:
                self.exit_code = code if code != 0 else self.exit_code
                print(f"[emulation] child exited with code {code}; shutting down")
                self.stop_requested = True
                return

    def run(self) -> int:
        self.setup()
        print("[emulation] loop running. Ctrl-C to stop.")

        try:
            while not self.stop_requested:
                now = time.monotonic()
                self._poll_children()

                events = self.selector.select(timeout=0.01)
                for key, _ in events:
                    fd = key.fd
                    data = self._read_fd(fd)
                    if not data:
                        continue
                    if fd == self.app_master_fd:
                        self._process_app_to_gds(data, now)
                    elif fd == self.gds_master_fd:
                        self._process_gds_to_app(data, now)
                    elif fd == self.payload_master_fd:
                        self._process_payload_to_app(data, now)

                if self.link_mode != "direct":
                    uplink_msg = self.gds_burst_aggregator.poll(now)
                    if uplink_msg is not None:
                        self._process_gds_message_to_app(uplink_msg, now)
                    payload_uplink_msg = self.payload_burst_aggregator.poll(now)
                    if payload_uplink_msg is not None:
                        self._process_payload_message_to_app(payload_uplink_msg, now)

                    self.app_uart_parser.poll_timeout(now)
                    self.ground_reassembler.poll_timeout(now)
                    self.sat_reassembler.poll_timeout(now)
                self._flush_pending()
        finally:
            self.shutdown()
        return self.exit_code

    def shutdown(self) -> None:
        live_children = [proc for proc in self.children if proc.poll() is None]
        for proc in live_children:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                continue
            except Exception:
                try:
                    proc.terminate()
                except Exception:
                    pass
        deadline = time.monotonic() + 3.0
        for proc in live_children:
            remaining = max(0.1, deadline - time.monotonic())
            try:
                proc.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                except Exception:
                    try:
                        proc.kill()
                    except Exception:
                        pass
                try:
                    proc.wait(timeout=1.0)
                except Exception:
                    pass

        for fd in (
            self.app_master_fd,
            self.gds_master_fd,
            self.payload_master_fd,
            self.app_slave_fd,
            self.gds_slave_fd,
            self.payload_slave_fd,
        ):
            if fd is None:
                continue
            try:
                self.selector.unregister(fd)
            except Exception:
                pass
            try:
                os.close(fd)
            except OSError:
                pass

        print("[emulation] final stats:")
        print(f"  app_uart_bytes_in={self.stats.app_uart_bytes_in}")
        print(f"  app_frames_in={self.stats.app_frames_in}")
        print(f"  rf_packets_app_to_gds={self.stats.rf_packets_app_to_gds}")
        print(f"  gds_bytes_out={self.stats.gds_bytes_out}")
        print(f"  gds_uart_bytes_in={self.stats.gds_uart_bytes_in}")
        print(f"  gds_messages_in={self.stats.gds_messages_in}")
        print(f"  rf_packets_gds_to_app={self.stats.rf_packets_gds_to_app}")
        print(f"  payload_uart_bytes_in={self.stats.payload_uart_bytes_in}")
        print(f"  payload_messages_in={self.stats.payload_messages_in}")
        print(f"  rf_packets_payload_to_app={self.stats.rf_packets_payload_to_app}")
        print(f"  app_bytes_out={self.stats.app_bytes_out}")
        print(f"  payload_bytes_observed={self.stats.payload_bytes_observed}")
        print(f"  payload_bytes_out={self.stats.payload_bytes_out}")
        print(f"  payload_data_packets_dropped={self.stats.payload_data_packets_dropped}")
        print(f"  local_frames_observed={self.stats.local_frames_observed}")
        print(
            "  uart_parser: "
            f"crc_drops={self.app_uart_parser.crc_drops} "
            f"framing_drops={self.app_uart_parser.framing_drops} "
            f"timeout_events={self.app_uart_parser.timeout_events}"
        )
        print(
            "  ground_reassembler: "
            f"framing_drops={self.ground_reassembler.framing_drops} "
            f"reassembly_timeouts={self.ground_reassembler.reassembly_timeouts} "
            f"reassembly_drops={self.ground_reassembler.reassembly_drops} "
            f"oversize_drops={self.ground_reassembler.oversize_drops}"
        )
        print(
            "  sat_reassembler: "
            f"framing_drops={self.sat_reassembler.framing_drops} "
            f"reassembly_timeouts={self.sat_reassembler.reassembly_timeouts} "
            f"reassembly_drops={self.sat_reassembler.reassembly_drops} "
            f"oversize_drops={self.sat_reassembler.oversize_drops}"
        )


def _is_timestamp_log_dir(path: Path) -> bool:
    return path.is_dir() and TIMESTAMP_DIR_RE.match(path.name) is not None


def _prune_timestamp_dirs(log_root: Path, max_runs: int) -> list[Path]:
    if max_runs < 0:
        return []
    if not log_root.exists():
        return []
    runs = [entry for entry in log_root.iterdir() if _is_timestamp_log_dir(entry)]
    runs.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    to_remove = runs[max_runs:]
    removed: list[Path] = []
    for stale in to_remove:
        try:
            shutil.rmtree(stale)
            removed.append(stale)
        except FileNotFoundError:
            continue
    return removed


def _find_latest_file(root: Path, pattern: str) -> Optional[Path]:
    matches = list(root.glob(pattern))
    if not matches:
        return None
    matches.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return matches[0]


def _host_artifact_platform() -> str:
    system = platform.system()
    if system == "Darwin":
        return "Darwin"
    if system == "Linux":
        return "Linux"
    return system


def _find_host_file_or_latest(root: Path, pattern: str) -> Optional[Path]:
    host_path = root / _host_artifact_platform() / pattern
    if host_path.exists():
        return host_path
    return _find_latest_file(root, f"*/{pattern}")


def _resolve_default_app_binary(project_root: Path) -> Optional[Path]:
    build_root = project_root / "build-artifacts"
    if not build_root.exists():
        return None
    return _find_host_file_or_latest(
        build_root, f"{DEPLOYMENT_NAME}/bin/{DEPLOYMENT_NAME}"
    )


def _resolve_default_dictionary(project_root: Path) -> Optional[Path]:
    build_root = project_root / "build-artifacts"
    if not build_root.exists():
        return None
    return _find_host_file_or_latest(
        build_root, f"{DEPLOYMENT_NAME}/dict/{DICT_BASENAME}"
    )


def parse_args() -> argparse.Namespace:
    script_path = Path(__file__).resolve()
    default_project_root = script_path.parent.parent

    parser = argparse.ArgumentParser(
        description="Run a local closed-loop emulator for app <-> Teensy link <-> GDS."
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=default_project_root,
        help=f"Path to ArtemisRpiTeensy_N2 root (default: {default_project_root})",
    )
    parser.add_argument(
        "--app-binary",
        type=Path,
        default=None,
        help="Path to ArtemisRpiTeensyDeployment binary (auto-detected if omitted)",
    )
    parser.add_argument(
        "--dictionary",
        type=Path,
        default=None,
        help="Path to deployment dictionary JSON (auto-detected if omitted)",
    )
    parser.add_argument(
        "--gui-port",
        type=int,
        default=DEFAULT_GUI_PORT,
        help=f"GDS web UI port (default: {DEFAULT_GUI_PORT})",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=DEFAULT_UART_BAUD,
        help=f"GDS UART baud (default: {DEFAULT_UART_BAUD})",
    )
    parser.add_argument(
        "--framing-selection",
        default="space-packet-space-data-link",
        help='GDS framing selection (default: "space-packet-space-data-link")',
    )
    parser.add_argument(
        "--uplink-flush-ms",
        type=int,
        default=DEFAULT_UPLINK_FLUSH_MS,
        help=f"Ground USB burst flush timeout in ms (default: {DEFAULT_UPLINK_FLUSH_MS})",
    )
    parser.add_argument(
        "--link-mode",
        choices=("channelized", "direct", "legacy-wrapper"),
        default="channelized",
        help=(
            'Byte bridge mode: "channelized" for the current UART mux, '
            '"direct" for older no-mux topologies, or "legacy-wrapper" as a '
            'compatibility alias for channelized (default: channelized)'
        ),
    )
    parser.add_argument(
        "--drop-payload-data-index",
        type=int,
        choices=range(N2_MAX_DATA_INDEX + 1),
        default=None,
        metavar="INDEX",
        help=(
            "Drop the first channel-1 N2 DATA packet whose encoded packet index "
            "matches INDEX; retransmissions pass (default: disabled)"
        ),
    )
    parser.add_argument(
        "--no-app",
        action="store_true",
        help="Do not launch the flight app (emulator still creates app UART PTY)",
    )
    parser.add_argument(
        "--no-gds",
        action="store_true",
        help="Do not launch fprime-gds (emulator still creates gds UART PTY)",
    )
    parser.add_argument(
        "--max-log-runs",
        type=int,
        default=DEFAULT_MAX_LOG_RUNS,
        help=(
            "Keep at most this many timestamped logs in logs/ and logs/local_emulation "
            f"(default: {DEFAULT_MAX_LOG_RUNS})"
        ),
    )
    parser.add_argument(
        "--no-log-prune",
        action="store_true",
        help="Disable automatic pruning of old timestamped log directories",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_root = args.project_root.resolve()
    logs_root = project_root / "logs"
    emulation_logs_root = logs_root / "local_emulation"

    app_binary = args.app_binary.resolve() if args.app_binary else _resolve_default_app_binary(project_root)
    dictionary = args.dictionary.resolve() if args.dictionary else _resolve_default_dictionary(project_root)

    if not args.no_app and app_binary is None:
        print(
            "[emulation] could not find deployment binary. "
            "Build first: fprime-util generate -f && fprime-util build",
            file=sys.stderr,
        )
        return 1
    if not args.no_gds and dictionary is None:
        print(
            "[emulation] could not find topology dictionary. "
            "Build first: fprime-util generate -f && fprime-util build",
            file=sys.stderr,
        )
        return 1

    app_cmd: Optional[list[str]]
    if args.no_app:
        app_cmd = None
    else:
        app_cmd = [str(app_binary), "-d", "{app_uart}"]

    gds_cmd: Optional[list[str]]
    if args.no_gds:
        gds_cmd = None
    else:
        run_log_dir = emulation_logs_root / time.strftime("%Y_%m_%d-%H_%M_%S")
        gds_cmd = [
            "fprime-gds",
            "-n",
            "--dictionary",
            str(dictionary),
            "--communication-selection",
            "uart",
            "--uart-device",
            "{gds_uart}",
            "--uart-baud",
            str(args.baud),
            "--uart-skip-port-check",
            "--framing-selection",
            args.framing_selection,
            "--gui-port",
            str(args.gui_port),
            "--logs",
            str(run_log_dir),
        ]

    if not args.no_log_prune:
        # Keep logs bounded both in the dedicated emulation folder and in the legacy logs root.
        # This prevents historical runs from growing indefinitely.
        removed = _prune_timestamp_dirs(emulation_logs_root, args.max_log_runs)
        removed += _prune_timestamp_dirs(logs_root, args.max_log_runs)
        if removed:
            print(f"[emulation] pruned {len(removed)} old log directories")

    loop = EmulationLoop(
        app_cmd=app_cmd,
        gds_cmd=gds_cmd,
        uplink_flush_ms=args.uplink_flush_ms,
        link_mode=args.link_mode,
        drop_payload_data_index=args.drop_payload_data_index,
    )

    print("[emulation] topology:")
    if args.link_mode == "direct":
        print("  app raw bytes <-> gds raw bytes (direct local bridge)")
    else:
        print("  app channel 0 wrapper -> RF segment/reassemble -> gds raw bytes")
        print("  app channel 1 wrapper -> RF segment/reassemble -> payload receiver raw bytes")
        print("  app channel 2 wrapper -> satellite-local RPC observed locally, not forwarded")
        print("  gds raw bytes -> burst packetization -> RF channel 0 -> channel wrapper -> app")
        print("  payload receiver raw bytes -> burst packetization -> RF channel 1 -> channel wrapper -> app")
    if not args.no_gds:
        print(f"[emulation] open GDS at http://127.0.0.1:{args.gui_port}")
    rc = loop.run()

    if not args.no_log_prune:
        removed = _prune_timestamp_dirs(emulation_logs_root, args.max_log_runs)
        removed += _prune_timestamp_dirs(logs_root, args.max_log_runs)
        if removed:
            print(f"[emulation] post-run prune removed {len(removed)} old log directories")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
