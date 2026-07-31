#!/usr/bin/env python3
"""Local closed-loop emulator for ArtemisRpiTeensy_N2.

This script emulates the RPi <-> satellite Teensy <-> RF <-> ground Teensy link
on a single host. It creates two pseudo UART devices:

- app UART: passed to ArtemisRpiTeensyDeployment (-d ...)
- gds UART: passed to fprime-gds (--uart-device ...)

Supported link modes:
- channelized: current UART channel mux plus RF segment/reassemble emulation
- direct: raw byte bridge app<->gds for older no-mux topologies
- legacy-wrapper: compatibility alias for channelized
"""

from __future__ import annotations

import argparse
import errno
import os
import pty
import re
import selectors
import signal
import subprocess
import shutil
import sys
import time
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

TEENSY_TARGET_RF_STATUS = 2
TEENSY_STATUS_OK = 0
TEENSY_STATUS_BAD_REQUEST = 1
TEENSY_STATUS_TARGET_ERROR = 4
TEENSY_RF_OP_LINK_STATS = 1
TEENSY_RF_OP_STATUS = 3
TEENSY_RF_OP_SET_ENABLED = 2

RADIO_STATE_OFF = 0
RADIO_STATE_READY = 1
RADIO_FAULT_NONE = 0
RADIO_FAULT_INIT_FAILED = 1
RADIO_FAULT_WATCHDOG_RESET = 2
RADIO_BOOT_FLAG_WATCHDOG = 0x01
RADIO_STATUS_PAYLOAD_LEN = 33
RADIO_SET_ENABLED_PAYLOAD_LEN = 4
RSSI_INVALID_DBM = 0
RSSI_INVALID_AGE_MS = 0xFFFFFFFF

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
        self.next_msg_id = [0] * CHANNEL_RF_COUNT

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

        msg_id = self.next_msg_id[channel]
        self.next_msg_id[channel] = (msg_id + 1) & 0xFF

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
    app_frames_in: int = 0
    gds_messages_in: int = 0
    rf_packets_app_to_gds: int = 0
    rf_packets_gds_to_app: int = 0
    gds_bytes_out: int = 0
    app_bytes_out: int = 0
    payload_bytes_observed: int = 0
    local_frames_observed: int = 0
    local_responses_sent: int = 0
    radio_frames_dropped_off: int = 0


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


class RadioRpcEmulator:
    """Deterministic satellite-local model for RF target-2 RPCs."""

    LOCAL_HEADER_LEN = 4

    def __init__(self, init_failures: int = 0) -> None:
        if init_failures < 0:
            raise ValueError("radio init failures must be non-negative")
        self.remaining_init_failures = init_failures
        self.state = RADIO_STATE_OFF
        self.fault = RADIO_FAULT_NONE
        self.boot_flags = 0
        self.init_attempts = 0
        self.last_rssi_dbm = RSSI_INVALID_DBM
        self.last_rssi_ts: Optional[float] = None
        self.rx_good = 0
        self.rx_bad = 0
        self.tx_good = 0
        self.rf_rx_packets = 0
        self.rf_tx_packets = 0
        self.rf_tx_drops = 0

    @property
    def ready(self) -> bool:
        return self.state == RADIO_STATE_READY

    @staticmethod
    def _response(request_id: int, status: int, payload: bytes = b"") -> bytes:
        return bytes(
            [TEENSY_TARGET_RF_STATUS, request_id & 0xFF, status & 0xFF, len(payload) & 0xFF]
        ) + payload

    def _status_payload(self, now: float) -> bytes:
        if self.last_rssi_ts is None:
            rssi_valid = 0
            rssi_age_ms = RSSI_INVALID_AGE_MS
        else:
            rssi_valid = 1
            rssi_age_ms = min(max(0, int((now - self.last_rssi_ts) * 1000.0)), RSSI_INVALID_AGE_MS)

        payload = bytearray(RADIO_STATUS_PAYLOAD_LEN)
        payload[0] = TEENSY_RF_OP_STATUS
        payload[1:3] = int(self.last_rssi_dbm).to_bytes(2, "little", signed=True)
        payload[3:5] = int(self.rx_good & 0xFFFF).to_bytes(2, "little")
        payload[5:7] = int(self.rx_bad & 0xFFFF).to_bytes(2, "little")
        payload[7:9] = int(self.tx_good & 0xFFFF).to_bytes(2, "little")
        payload[9:13] = int(self.rf_rx_packets & 0xFFFFFFFF).to_bytes(4, "little")
        payload[13:17] = int(self.rf_tx_packets & 0xFFFFFFFF).to_bytes(4, "little")
        payload[17:21] = int(self.rf_tx_drops & 0xFFFFFFFF).to_bytes(4, "little")
        payload[21] = self.state
        payload[22] = self.fault
        payload[23] = self.boot_flags
        payload[24] = rssi_valid
        payload[25:29] = rssi_age_ms.to_bytes(4, "little")
        payload[29:33] = int(self.init_attempts & 0xFFFFFFFF).to_bytes(4, "little")
        return bytes(payload)

    def _legacy_stats_payload(self, now: float) -> bytes:
        payload = bytearray(self._status_payload(now)[:21])
        payload[0] = TEENSY_RF_OP_LINK_STATS
        return bytes(payload)

    def _set_enabled(self, enabled: bool) -> tuple[bytes, int]:
        requested = 1 if enabled else 0
        if not enabled:
            self.state = RADIO_STATE_OFF
            self.fault = RADIO_FAULT_NONE
            return bytes([TEENSY_RF_OP_SET_ENABLED, requested, self.state, self.fault]), TEENSY_STATUS_OK
        if not self.ready:
            self.init_attempts += 1
            if self.remaining_init_failures > 0:
                self.remaining_init_failures -= 1
                self.state = RADIO_STATE_OFF
                self.fault = RADIO_FAULT_INIT_FAILED
                return (
                    bytes([TEENSY_RF_OP_SET_ENABLED, requested, self.state, self.fault]),
                    TEENSY_STATUS_TARGET_ERROR,
                )
            self.state = RADIO_STATE_READY
            self.fault = RADIO_FAULT_NONE
        return bytes([TEENSY_RF_OP_SET_ENABLED, requested, self.state, self.fault]), TEENSY_STATUS_OK

    def handle(self, request: bytes, now: float) -> Optional[bytes]:
        if not request or request[0] != TEENSY_TARGET_RF_STATUS:
            return None
        request_id = request[1] if len(request) > 1 else 0
        if len(request) < self.LOCAL_HEADER_LEN:
            return self._response(request_id, TEENSY_STATUS_BAD_REQUEST)
        payload_len = request[2]
        if request[3] != 0 or len(request) != self.LOCAL_HEADER_LEN + payload_len:
            return self._response(request_id, TEENSY_STATUS_BAD_REQUEST)
        payload = request[self.LOCAL_HEADER_LEN :]
        if payload_len == 1 and payload[0] == TEENSY_RF_OP_LINK_STATS:
            return self._response(request_id, TEENSY_STATUS_OK, self._legacy_stats_payload(now))
        if payload_len == 1 and payload[0] == TEENSY_RF_OP_STATUS:
            return self._response(request_id, TEENSY_STATUS_OK, self._status_payload(now))
        if payload_len == 2 and payload[0] == TEENSY_RF_OP_SET_ENABLED and payload[1] in (0, 1):
            response_payload, status = self._set_enabled(payload[1] == 1)
            return self._response(request_id, status, response_payload)
        return self._response(request_id, TEENSY_STATUS_BAD_REQUEST)

    def note_rf_tx_packet(self) -> None:
        self.tx_good += 1
        self.rf_tx_packets += 1

    def note_rf_rx_packet(self, now: float) -> None:
        self.rx_good += 1
        self.rf_rx_packets += 1
        self.last_rssi_dbm = -75
        self.last_rssi_ts = now

    def note_rf_tx_drop(self) -> None:
        self.rf_tx_drops += 1


class EmulationLoop:
    def __init__(
        self,
        app_cmd: Optional[list[str]],
        gds_cmd: Optional[list[str]],
        uplink_flush_ms: int,
        link_mode: str,
    ) -> None:
        self.app_cmd = app_cmd
        self.gds_cmd = gds_cmd
        self.uplink_flush_s = uplink_flush_ms / 1000.0
        self.link_mode = link_mode
        self.radio = RadioRpcEmulator()

        self.stop_requested = False
        self.exit_code = 0

        self.selector = selectors.DefaultSelector()
        self.children: list[subprocess.Popen[bytes]] = []

        self.app_master_fd: Optional[int] = None
        self.app_slave_fd: Optional[int] = None
        self.gds_master_fd: Optional[int] = None
        self.gds_slave_fd: Optional[int] = None

        self.app_uart_device = ""
        self.gds_uart_device = ""

        self.pending_writes: dict[int, bytearray] = {}

        self.app_uart_parser = UartFrameParser()
        self.sat_to_ground_segmenter = RfSegmenter()
        self.ground_reassembler = RfReassembler()

        self.gds_burst_aggregator = BurstAggregator(self.uplink_flush_s)
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

        print(f"[emulation] app UART device: {self.app_uart_device}")
        print(f"[emulation] gds UART device: {self.gds_uart_device}")

        if self.app_cmd is not None:
            cmd = [item.format(app_uart=self.app_uart_device, gds_uart=self.gds_uart_device) for item in self.app_cmd]
            self._launch_child(cmd, "flight app")
        if self.gds_cmd is not None:
            cmd = [item.format(app_uart=self.app_uart_device, gds_uart=self.gds_uart_device) for item in self.gds_cmd]
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
                response = self.radio.handle(frame, now)
                if response is not None:
                    framed = build_uart_frame(CHANNEL_TEENSY_LOCAL, response)
                    self.stats.local_responses_sent += 1
                    self.stats.app_bytes_out += len(framed)
                    self._queue_write(self.app_master_fd, framed)  # type: ignore[arg-type]
                continue
            if not self.radio.ready:
                self.stats.radio_frames_dropped_off += 1
                self.radio.note_rf_tx_drop()
                continue
            rf_packets = self.sat_to_ground_segmenter.segment(channel, frame)
            self.stats.rf_packets_app_to_gds += len(rf_packets)
            for packet in rf_packets:
                self.radio.note_rf_tx_packet()
                reassembled = self.ground_reassembler.feed(packet, now)
                if reassembled is None:
                    continue
                out_channel, payload = reassembled
                if out_channel == CHANNEL_CCSDS:
                    self.stats.gds_bytes_out += len(payload)
                    self._queue_write(self.gds_master_fd, payload)  # type: ignore[arg-type]
                elif out_channel == CHANNEL_PAYLOAD:
                    self.stats.payload_bytes_observed += len(payload)

    def _process_gds_message_to_app(self, message: bytes, now: float) -> None:
        self.stats.gds_messages_in += 1
        if not self.radio.ready:
            self.stats.radio_frames_dropped_off += 1
            return
        rf_packets = self.ground_to_sat_segmenter.segment(CHANNEL_CCSDS, message)
        self.stats.rf_packets_gds_to_app += len(rf_packets)
        for packet in rf_packets:
            self.radio.note_rf_rx_packet(now)
            reassembled = self.sat_reassembler.feed(packet, now)
            if reassembled is not None:
                channel, payload = reassembled
                framed = build_uart_frame(channel, payload)
                self.stats.app_bytes_out += len(framed)
                self._queue_write(self.app_master_fd, framed)  # type: ignore[arg-type]

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

                if self.link_mode != "direct":
                    uplink_msg = self.gds_burst_aggregator.poll(now)
                    if uplink_msg is not None:
                        self._process_gds_message_to_app(uplink_msg, now)

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

        for fd in (self.app_master_fd, self.gds_master_fd, self.app_slave_fd, self.gds_slave_fd):
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
        print(f"  app_bytes_out={self.stats.app_bytes_out}")
        print(f"  payload_bytes_observed={self.stats.payload_bytes_observed}")
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


def _resolve_default_app_binary(project_root: Path) -> Optional[Path]:
    build_root = project_root / "build-artifacts"
    if not build_root.exists():
        return None
    return _find_latest_file(
        build_root, f"*/{DEPLOYMENT_NAME}/bin/{DEPLOYMENT_NAME}"
    )


def _resolve_default_dictionary(project_root: Path) -> Optional[Path]:
    build_root = project_root / "build-artifacts"
    if not build_root.exists():
        return None
    return _find_latest_file(
        build_root, f"*/{DEPLOYMENT_NAME}/dict/{DICT_BASENAME}"
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
    )

    print("[emulation] topology:")
    if args.link_mode == "direct":
        print("  app raw bytes <-> gds raw bytes (direct local bridge)")
    else:
        print("  app channel 0 wrapper -> RF segment/reassemble -> gds raw bytes")
        print("  app channel 1 wrapper -> RF segment/reassemble -> payload stream observed locally")
        print("  app channel 2 wrapper -> satellite-local RPC observed locally, not forwarded")
        print("  gds raw bytes -> burst packetization -> RF channel 0 -> channel wrapper -> app")
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
