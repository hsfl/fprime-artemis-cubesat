#!/usr/bin/env python3
"""Bidirectional C3M RF22 HackRF adapter for GDS and payload PTYs."""

from __future__ import annotations

import argparse
import math
import os
import signal
import sys
import time
from collections import deque
from pathlib import Path

import numpy as np

from bridge_core import (
    BridgeBackpressureError,
    BridgeLock,
    UplinkBatcher,
    VirtualChannel,
    atomic_write_json,
)
from hackrf_device import HackRFConfig, HackRFDevice, HackRFError, HackRFTimeoutError
from rf_autocal import (
    ADAPT_RX_CLIP_BLOCKS,
    ADAPT_RX_CLIP_FRACTION,
    ADAPT_RX_DWELL_S,
    ADAPT_RX_SILENCE_S,
    ADAPT_TX_SUCCESSES_PER_STEP_DOWN,
    ADAPT_TX_TIMEOUTS_PER_STEP,
    RF_AMP_SEARCH_STATES,
    RX_CANDIDATES,
    TX_CANDIDATES,
    RxWindow,
    build_fprime_ping_command,
    next_rx_state,
    next_tx_state,
    previous_rx_state,
    previous_tx_state,
    select_rx_window,
    tx_search_attempt_budget,
)
from rf22_iq_decoder import SegmentReassembler, StreamingCs8Decoder
from rf22_protocol import (
    DEFAULT_PROFILE,
    MessageIdStore,
    build_message_packets,
    load_profile,
    matches_ack,
)
from rf22_tx import modulate
from rf_safety import (
    BASELINE_RX_LNA_GAIN_DB,
    BASELINE_RX_VGA_GAIN_DB,
    BASELINE_TX_LEADING_MS,
    BASELINE_TX_MODE,
    DEFAULT_RF_PATH_LABEL,
    TxSafetyError,
    validate_tx_request,
)


SAMPLE_RATE = 8_000_000
RX_CENTER_HZ = 432_500_000
RF_CARRIER_HZ = 433_000_000
DEFAULT_SERIAL = "0000000000000000675c62dc301090cf"


class HackRfGroundBridge:
    def __init__(self, args: argparse.Namespace) -> None:
        if args.tx_mode != BASELINE_TX_MODE:
            raise TxSafetyError(
                f"channel-0 TX mode is fixed to {BASELINE_TX_MODE!r}; "
                "use the GDS Teensy/RFM23BP fallback if ACK mode is unavailable"
            )
        args.rf_path_label = validate_tx_request(
            enable_tx=args.enable_tx,
            tx_gain=args.tx_gain,
            tx_safety_confirmed=args.tx_safety_confirmed,
            allow_elevated_tx_gain=args.allow_elevated_tx_gain,
            rf_path_label=args.rf_path_label,
        )
        if not args.enable_tx:
            args.tx_gain = 0
            args.tx_safety_confirmed = False
            args.allow_elevated_tx_gain = False
        self.args = args
        self.profile = load_profile(args.network)
        self.channels = {
            0: VirtualChannel(0, args.gds_symlink, max_backlog_bytes=args.max_pty_backlog),
            1: VirtualChannel(
                1, args.payload_symlink, max_backlog_bytes=args.max_pty_backlog
            ),
        }
        self.batchers = {
            channel: UplinkBatcher(
                self.profile.frame_max_payload, args.uplink_idle_ms / 1000.0
            )
            for channel in self.channels
        }
        self.reassembler = SegmentReassembler(self.profile)
        self.decoder = StreamingCs8Decoder(
            sample_rate=SAMPLE_RATE,
            center_hz=RX_CENTER_HZ,
            carrier_hz=RF_CARRIER_HZ,
            overlap_seconds=args.rx_overlap_ms / 1000.0,
            profile=self.profile,
        )
        self.message_ids = MessageIdStore(args.message_id_state)
        self.tx_queue: deque[tuple[int, bytes]] = deque()
        self.tx_queue_bytes = 0
        self.device: HackRFDevice | None = None
        self.stopping = False
        self._rx_accumulator = bytearray()
        self._ack_target: tuple[int, int, int] | None = None
        self._ack_received = False
        self._fprime_probe_token: bytes | None = None
        self._fprime_probe_response = False
        self._rx_rf_amp_enabled = False
        self._tx_rf_amp_enabled = False
        self._adaptive_enabled = False
        self._last_valid_frame_s: float | None = None
        self._last_rx_adjustment_s = time.monotonic()
        self._last_tx_s = self._last_rx_adjustment_s
        self._last_adaptive_rx_block = 0
        self._consecutive_clipped_blocks = 0
        self._consecutive_ack_timeouts = 0
        self._consecutive_ack_successes = 0
        self._last_proven_tx_state: tuple[int, bool] | None = None
        self._last_metrics_write = 0.0
        self._started_wall_s = time.time()
        self._started_monotonic_s = time.monotonic()
        self._capture_streams = {}
        self.metrics: dict[str, object] = {
            "schema": 1,
            "pid": os.getpid(),
            "network": self.profile.name,
            "network_id": self.profile.network_id,
            "rf_contract": {
                "downlink_header": self.profile.radio_header("downlink").hex(),
                "uplink_header": self.profile.radio_header("uplink").hex(),
                "channel_magic": {
                    "0": f"{self.profile.segment_magic[0]:02x}",
                    "1": f"{self.profile.segment_magic[1]:02x}",
                },
            },
            "serial": args.serial,
            "tx_enabled": args.enable_tx,
            "tx_mode": args.tx_mode if args.enable_tx else "disabled",
            "tx_gain": args.tx_gain if args.enable_tx else 0,
            "tx_leading_ms": args.tx_leading_ms,
            "rx_lna_gain": args.rx_lna_gain,
            "rx_vga_gain": args.rx_vga_gain,
            "gain_control": "automatic" if getattr(args, "auto_calibrate", False) else "fixed",
            "calibration": {
                "enabled": bool(getattr(args, "auto_calibrate", False)),
                "state": "pending" if getattr(args, "auto_calibrate", False) else "disabled",
                "runs": 0,
                "rx_windows": [],
                "tx_probes": [],
                "selected": None,
            },
            "rf_path_label": args.rf_path_label,
            "tx_safety_confirmed": args.tx_safety_confirmed,
            "elevated_tx_gain_confirmed": args.allow_elevated_tx_gain,
            "rf_amp_enabled": False,
            "rx_rf_amp_enabled": False,
            "tx_rf_amp_enabled": False,
            "antenna_power_enabled": False,
            "adaptive_link": {
                "enabled": False,
                "state": "pending",
                "last_valid_frame_age_s": None,
                "rx_adjustments": 0,
                "rx_reacquisitions": 0,
                "rx_search_wraps": 0,
                "rx_max_holds": 0,
                "tx_adjustments": 0,
                "tx_search_wraps": 0,
                "tx_max_holds": 0,
                "consecutive_ack_timeouts": 0,
                "consecutive_ack_successes": 0,
                "last_adjustment_reason": None,
                "history": [],
            },
            "started_at_s": self._started_wall_s,
            "radio_state": "starting",
            "reconnects": 0,
            "rx_blocks": 0,
            "rx_bytes": 0,
            "rx_iq": {
                "sampled_complex_samples": 0,
                "clipped_complex_samples": 0,
                "clipped_fraction": 0.0,
                "last_block_clipped_fraction": 0.0,
                "max_block_clipped_fraction": 0.0,
                "peak_abs": 0,
                "last_block_complex_rms_dbfs": None,
            },
            "rf22_frames": 0,
            "messages": {"0": 0, "1": 0},
            "message_bytes": {"0": 0, "1": 0},
            "tx_messages": {"0": 0, "1": 0},
            "tx_segments": 0,
            "tx_attempts": 0,
            "tx_failures": 0,
            "tx_queue": {
                "messages": 0,
                "bytes": 0,
                "max_messages": args.max_tx_queue_messages,
                "max_bytes": args.max_tx_queue_bytes,
                "high_water_messages": 0,
                "high_water_bytes": 0,
                "rejections": 0,
            },
            "ack_received": 0,
            "ack_timeouts": 0,
            "degraded_segments": 0,
            "last_error": None,
        }

    def log(self, event: str, **fields: object) -> None:
        details = " ".join(f"{key}={value}" for key, value in fields.items())
        print(f"{event}{' ' if details else ''}{details}", flush=True)

    def request_stop(self, _signum: int, _frame: object) -> None:
        self.stopping = True

    def _open_runtime(self) -> None:
        for channel in self.channels.values():
            channel.open()
        self.args.uplink_capture_dir.mkdir(parents=True, exist_ok=True)
        for channel in self.channels:
            path = self.args.uplink_capture_dir / f"channel-{channel}-uplink.bin"
            # Assign each stream immediately so a later open failure can close
            # every file already opened by this attempt.
            self._capture_streams[channel] = path.open("ab", buffering=0)
        self.log(
            "LIVE_READY",
            gds=self.args.gds_symlink,
            payload=self.args.payload_symlink,
            metrics=self.args.metrics_file,
            network=self.profile.name,
        )
        self._write_metrics(force=True)

    def _close_runtime(self, *, failed: bool = False) -> None:
        errors: list[str] = []
        for channel, stream in list(self._capture_streams.items()):
            try:
                stream.close()
            except Exception as exc:
                errors.append(f"capture channel {channel}: {exc!r}")
        self._capture_streams.clear()
        for channel, port in self.channels.items():
            try:
                port.close()
            except Exception as exc:
                errors.append(f"PTY channel {channel}: {exc!r}")
        self.metrics["radio_state"] = (
            "cleanup_failed" if errors else ("failed" if failed else "stopped")
        )
        try:
            self._write_metrics(force=True)
        except Exception as exc:
            errors.append(f"final metrics: {exc!r}")
        if errors:
            raise RuntimeError("runtime cleanup failed: " + "; ".join(errors))

    def _write_metrics(self, *, force: bool = False) -> None:
        now = time.monotonic()
        if not force and now - self._last_metrics_write < 1.0:
            return
        self._last_metrics_write = now
        adaptive = self.metrics["adaptive_link"]
        assert isinstance(adaptive, dict)
        adaptive["last_valid_frame_age_s"] = (
            None
            if self._last_valid_frame_s is None
            else round(max(0.0, now - self._last_valid_frame_s), 3)
        )
        device = self.device
        self.metrics.update(
            {
                "updated_at_s": time.time(),
                "uptime_s": round(now - self._started_monotonic_s, 3),
                "hackrf_mode": device.mode if device is not None else "closed",
                "hackrf_rx_dropped_blocks": (
                    device.rx_dropped_blocks if device is not None else 0
                ),
                "pty": {
                    str(channel): {
                        "symlink": str(port.symlink),
                        "target": port.slave_name,
                        "backlog_bytes": port.backlog_bytes,
                        "downlink_bytes": port.downlink_bytes,
                        "uplink_bytes": port.uplink_bytes,
                        "uplink_batch_bytes": self.batchers[channel].pending_bytes,
                    }
                    for channel, port in self.channels.items()
                },
                "reassembly": {
                    "completed": self.reassembler.completed,
                    "duplicates": self.reassembler.duplicates,
                    "drops": self.reassembler.drops,
                    "timeouts": self.reassembler.timeouts,
                },
            }
        )
        atomic_write_json(self.args.metrics_file, self.metrics)

    def _handle_frames(self, frames) -> None:
        for frame in frames:
            self._note_valid_frame()
            self.metrics["rf22_frames"] = int(self.metrics["rf22_frames"]) + 1
            if self._ack_target is not None and matches_ack(
                frame.payload,
                channel=self._ack_target[0],
                msg_id=self._ack_target[1],
                segment_index=self._ack_target[2],
                profile=self.profile,
            ):
                self._ack_received = True
                self.metrics["ack_received"] = int(self.metrics["ack_received"]) + 1
                self.log(
                    "ACK",
                    channel=self._ack_target[0],
                    id=f"0x{self._ack_target[1]:02x}",
                    segment=self._ack_target[2],
                )
                continue

            message = self.reassembler.accept(frame)
            if message is None:
                continue
            if (
                message.channel == 0
                and self._fprime_probe_token is not None
                and self._fprime_probe_token in message.data
            ):
                self._fprime_probe_response = True
                self.log("AUTO_TX_PONG", token=self._fprime_probe_token.hex())
            self.channels[message.channel].queue_downlink(message.data)
            messages = self.metrics["messages"]
            message_bytes = self.metrics["message_bytes"]
            assert isinstance(messages, dict) and isinstance(message_bytes, dict)
            key = str(message.channel)
            messages[key] = int(messages[key]) + 1
            message_bytes[key] = int(message_bytes[key]) + len(message.data)
            self.log(
                "MESSAGE",
                channel=message.channel,
                id=f"0x{message.msg_id:02x}",
                bytes=len(message.data),
            )

    def _decode_block(self, block: bytes) -> None:
        self.metrics["rx_blocks"] = int(self.metrics["rx_blocks"]) + 1
        self.metrics["rx_bytes"] = int(self.metrics["rx_bytes"]) + len(block)
        self._handle_frames(self.decoder.feed(block))

    def _update_rx_iq_metrics(self, block: bytes) -> None:
        raw = np.frombuffer(block, dtype=np.int8)
        if raw.size < 2:
            return
        if raw.size % 2:
            raw = raw[:-1]

        components = raw.astype(np.int16)
        pairs = components.reshape((-1, 2))
        component_abs = np.abs(pairs)
        clipped = int(np.count_nonzero(np.any(component_abs >= 127, axis=1)))
        complex_samples = int(pairs.shape[0])
        clipped_fraction = clipped / complex_samples
        complex_power = np.square(pairs.astype(np.float32)).sum(axis=1)
        complex_rms = float(np.sqrt(np.mean(complex_power))) / 128.0
        rms_dbfs = 20.0 * math.log10(max(complex_rms, np.finfo(float).tiny))

        iq_metrics = self.metrics["rx_iq"]
        assert isinstance(iq_metrics, dict)
        total_samples = int(iq_metrics["sampled_complex_samples"]) + complex_samples
        total_clipped = int(iq_metrics["clipped_complex_samples"]) + clipped
        iq_metrics.update(
            {
                "sampled_complex_samples": total_samples,
                "clipped_complex_samples": total_clipped,
                "clipped_fraction": total_clipped / total_samples,
                "last_block_clipped_fraction": clipped_fraction,
                "max_block_clipped_fraction": max(
                    float(iq_metrics["max_block_clipped_fraction"]),
                    clipped_fraction,
                ),
                "peak_abs": max(
                    int(iq_metrics["peak_abs"]), int(component_abs.max())
                ),
                "last_block_complex_rms_dbfs": round(rms_dbfs, 3),
            }
        )

    def _update_tx_queue_metrics(self) -> dict[str, int]:
        queue_metrics = self.metrics["tx_queue"]
        assert isinstance(queue_metrics, dict)
        queue_metrics["messages"] = len(self.tx_queue)
        queue_metrics["bytes"] = self.tx_queue_bytes
        queue_metrics["high_water_messages"] = max(
            int(queue_metrics["high_water_messages"]), len(self.tx_queue)
        )
        queue_metrics["high_water_bytes"] = max(
            int(queue_metrics["high_water_bytes"]), self.tx_queue_bytes
        )
        return queue_metrics

    def _enqueue_tx(self, channel: int, message: bytes) -> None:
        queued_messages = len(self.tx_queue)
        queued_bytes = self.tx_queue_bytes
        if (
            queued_messages + 1 > self.args.max_tx_queue_messages
            or queued_bytes + len(message) > self.args.max_tx_queue_bytes
        ):
            queue_metrics = self._update_tx_queue_metrics()
            queue_metrics["rejections"] = int(queue_metrics["rejections"]) + 1
            raise BridgeBackpressureError(
                f"TX queue rejected channel {channel} message ({len(message)} bytes): "
                f"queued={queued_messages} messages/{queued_bytes} bytes, "
                f"limits={self.args.max_tx_queue_messages} messages/"
                f"{self.args.max_tx_queue_bytes} bytes"
            )
        self.tx_queue.append((channel, bytes(message)))
        self.tx_queue_bytes += len(message)
        self._update_tx_queue_metrics()

    def _dequeue_tx(self) -> tuple[int, bytes]:
        channel, message = self.tx_queue.popleft()
        self.tx_queue_bytes -= len(message)
        self._update_tx_queue_metrics()
        return channel, message

    def _poll_rx(self, timeout_s: float, *, low_latency: bool = False) -> None:
        assert self.device is not None
        try:
            block = self.device.read_rx_block(timeout_s=timeout_s)
        except HackRFTimeoutError:
            return
        self._update_rx_iq_metrics(block)
        if low_latency:
            if self._rx_accumulator:
                self._decode_block(bytes(self._rx_accumulator))
                self._rx_accumulator.clear()
            self._decode_block(block)
            return
        self._rx_accumulator.extend(block)
        target = int(SAMPLE_RATE * (self.args.rx_block_ms / 1000.0)) * 2
        while len(self._rx_accumulator) >= target:
            chunk = bytes(self._rx_accumulator[:target])
            del self._rx_accumulator[:target]
            self._decode_block(chunk)

    def _service_ptys(self) -> None:
        now = time.monotonic()
        for channel, port in self.channels.items():
            port.flush_downlink()
            outgoing = port.read_uplink()
            if outgoing:
                self._capture_streams[channel].write(outgoing)
                self.batchers[channel].feed(outgoing, now)
            for message in self.batchers[channel].pop_ready(now):
                if self.args.enable_tx:
                    self._enqueue_tx(channel, message)
                else:
                    self.log("UPLINK_CAPTURE", channel=channel, bytes=len(message), tx="disabled")

    def _switch_and_transmit(self, waveform: bytes) -> float:
        assert self.device is not None
        device = self.device
        device.discard_rx_blocks()
        self._rx_accumulator.clear()
        if device.mode == "rx":
            device.stop_rx(timeout_s=self.args.stop_timeout)
        device.set_amp_enabled(self._tx_rf_amp_enabled)
        device.set_frequency(RF_CARRIER_HZ)
        started = time.monotonic()
        try:
            result = device.transmit_cs8(
                waveform,
                timeout_s=self.args.tx_timeout,
                stop_timeout_s=self.args.stop_timeout,
                resume_rx=False,
            )
        finally:
            if device.mode == "idle":
                device.set_frequency(RX_CENTER_HZ)
                device.set_amp_enabled(self._rx_rf_amp_enabled)
                self.decoder.reset()
                self.reassembler.reset()
                device.start_rx()
            self._last_tx_s = time.monotonic()
        return time.monotonic() - started if "result" not in locals() else result.elapsed_s

    def _wait_for_ack(self, channel: int, msg_id: int, segment_index: int) -> bool:
        self._ack_target = (channel, msg_id, segment_index)
        self._ack_received = False
        # The extra decoder overlap is host-side lookahead, not extra RF ACK airtime.
        deadline = time.monotonic() + self.profile.ack_timeout_ms / 1000.0
        decode_deadline = deadline + self.args.rx_overlap_ms / 1000.0 + 0.010
        try:
            while not self.stopping and time.monotonic() < decode_deadline:
                self._poll_rx(0.020, low_latency=True)
                self._service_ptys()
                if self._ack_received:
                    return True
            return False
        finally:
            self._ack_target = None

    def _send_packet_acknowledged(
        self,
        packet: bytes,
        channel: int,
        msg_id: int,
        segment_index: int,
        *,
        max_attempts: int | None = None,
    ) -> bool:
        if max_attempts is None:
            base_attempts = self.profile.ack_retries + 1
            attempts = (
                tx_search_attempt_budget(
                    self.args.tx_gain,
                    self._tx_rf_amp_enabled,
                    base_attempts,
                )
                if self._adaptive_enabled
                else base_attempts
            )
        else:
            attempts = max_attempts
        if attempts < 1:
            raise ValueError("max_attempts must be at least one")
        for attempt in range(attempts):
            self.metrics["tx_attempts"] = int(self.metrics["tx_attempts"]) + 1
            waveform = modulate(
                packet,
                repeat=1,
                offset_hz=0,
                leading_silence_s=self.args.tx_leading_ms / 1000.0,
                inter_burst_silence_s=0,
                trailing_silence_s=0,
            )
            elapsed = self._switch_and_transmit(waveform)
            self.log(
                "TX_ATTEMPT",
                mode="ack",
                channel=channel,
                id=f"0x{msg_id:02x}",
                segment=segment_index,
                attempt=attempt + 1,
                switch_tx_s=f"{elapsed:.4f}",
            )
            if self._wait_for_ack(channel, msg_id, segment_index):
                self._note_ack_result(True)
                return True
            self.metrics["ack_timeouts"] = int(self.metrics["ack_timeouts"]) + 1
            self._note_ack_result(False)
            self.log(
                "ACK_TIMEOUT",
                channel=channel,
                id=f"0x{msg_id:02x}",
                segment=segment_index,
                attempt=attempt + 1,
            )
        return False

    def _reset_rx_measurement_pipeline(self) -> None:
        assert self.device is not None
        self.device.discard_rx_blocks()
        self._rx_accumulator.clear()
        self.decoder.reset()
        self.reassembler.reset()

    def _set_rx_amp(self, enabled: bool) -> None:
        assert self.device is not None
        if self._rx_rf_amp_enabled == enabled:
            return
        if self.device.mode == "rx":
            self.device.stop_rx(timeout_s=self.args.stop_timeout)
        self.device.set_amp_enabled(enabled)
        self._rx_rf_amp_enabled = enabled
        self.metrics["rx_rf_amp_enabled"] = enabled
        self.metrics["rf_amp_enabled"] = enabled or self._tx_rf_amp_enabled
        self._reset_rx_measurement_pipeline()
        self.device.start_rx()

    def _adaptive_metrics(self) -> dict[str, object]:
        adaptive = self.metrics["adaptive_link"]
        assert isinstance(adaptive, dict)
        return adaptive

    def _record_adaptive_event(self, event: str, reason: str) -> None:
        adaptive = self._adaptive_metrics()
        history = adaptive["history"]
        assert isinstance(history, list)
        history.append(
            {
                "at_s": time.time(),
                "event": event,
                "reason": reason,
                "rx_lna_gain_db": self.args.rx_lna_gain,
                "rx_vga_gain_db": self.args.rx_vga_gain,
                "rx_rf_amp_enabled": self._rx_rf_amp_enabled,
                "tx_gain_db": self.args.tx_gain,
                "tx_rf_amp_enabled": self._tx_rf_amp_enabled,
            }
        )
        del history[:-100]
        adaptive["last_adjustment_reason"] = reason

    def _initialize_adaptive_link(self) -> None:
        self._adaptive_enabled = bool(getattr(self.args, "auto_calibrate", False))
        now = time.monotonic()
        self._last_valid_frame_s = now
        self._last_rx_adjustment_s = now
        self._last_tx_s = now
        self._last_adaptive_rx_block = int(self.metrics["rx_blocks"])
        self._consecutive_clipped_blocks = 0
        self._consecutive_ack_timeouts = 0
        self._consecutive_ack_successes = 0
        self._last_proven_tx_state = (
            self.args.tx_gain,
            self._tx_rf_amp_enabled,
        )
        adaptive = self._adaptive_metrics()
        adaptive.update(
            {
                "enabled": self._adaptive_enabled,
                "state": "tracking" if self._adaptive_enabled else "disabled",
                "consecutive_ack_timeouts": 0,
                "consecutive_ack_successes": 0,
            }
        )
        if self._adaptive_enabled:
            self._record_adaptive_event("initialized", "startup_calibration")
            self.log(
                "ADAPTIVE_LINK_READY",
                rx_lna=self.args.rx_lna_gain,
                rx_vga=self.args.rx_vga_gain,
                rx_amp=str(self._rx_rf_amp_enabled).lower(),
                tx=self.args.tx_gain,
                tx_amp=str(self._tx_rf_amp_enabled).lower(),
            )

    def _note_valid_frame(self, now: float | None = None) -> None:
        now = time.monotonic() if now is None else now
        self._last_valid_frame_s = now
        if not self._adaptive_enabled:
            return
        adaptive = self._adaptive_metrics()
        if adaptive["state"] in {"searching", "overload_recovery"}:
            adaptive["state"] = "tracking"
            adaptive["rx_reacquisitions"] = int(adaptive["rx_reacquisitions"]) + 1
            self._record_adaptive_event("rx_reacquired", "valid_crc_frame")
            self.log(
                "ADAPT_RX_REACQUIRED",
                lna=self.args.rx_lna_gain,
                vga=self.args.rx_vga_gain,
                amp=str(self._rx_rf_amp_enabled).lower(),
            )

    def _apply_rx_state(
        self,
        lna_gain_db: int,
        vga_gain_db: int,
        rf_amp_enabled: bool,
        *,
        reason: str,
    ) -> bool:
        assert self.device is not None
        old = (
            self.args.rx_lna_gain,
            self.args.rx_vga_gain,
            self._rx_rf_amp_enabled,
        )
        new = (lna_gain_db, vga_gain_db, rf_amp_enabled)
        if new == old:
            return False
        self._set_rx_amp(rf_amp_enabled)
        self.device.set_rx_gains(lna_gain_db, vga_gain_db)
        self._reset_rx_measurement_pipeline()
        self.args.rx_lna_gain = lna_gain_db
        self.args.rx_vga_gain = vga_gain_db
        self.metrics["rx_lna_gain"] = lna_gain_db
        self.metrics["rx_vga_gain"] = vga_gain_db
        self.metrics["rx_rf_amp_enabled"] = rf_amp_enabled
        self.metrics["rf_amp_enabled"] = (
            rf_amp_enabled or self._tx_rf_amp_enabled
        )
        self._last_rx_adjustment_s = time.monotonic()
        adaptive = self._adaptive_metrics()
        adaptive["state"] = (
            "overload_recovery" if reason == "iq_clipping" else "searching"
        )
        adaptive["rx_adjustments"] = int(adaptive["rx_adjustments"]) + 1
        self._record_adaptive_event("rx_adjusted", reason)
        self.log(
            "ADAPT_RX_GAIN",
            reason=reason,
            old=f"{old[0]}/{old[1]}/{'on' if old[2] else 'off'}",
            new=f"{lna_gain_db}/{vga_gain_db}/{'on' if rf_amp_enabled else 'off'}",
        )
        self._write_metrics(force=True)
        return True

    def _apply_tx_state(
        self, gain_db: int, rf_amp_enabled: bool, *, reason: str
    ) -> bool:
        assert self.device is not None
        old = (self.args.tx_gain, self._tx_rf_amp_enabled)
        new = (gain_db, rf_amp_enabled)
        if new == old:
            return False
        self.device.set_tx_gain(gain_db)
        self.args.tx_gain = gain_db
        self._tx_rf_amp_enabled = rf_amp_enabled
        self.metrics["tx_gain"] = gain_db
        self.metrics["tx_rf_amp_enabled"] = rf_amp_enabled
        self.metrics["rf_amp_enabled"] = (
            self._rx_rf_amp_enabled or rf_amp_enabled
        )
        adaptive = self._adaptive_metrics()
        adaptive["tx_adjustments"] = int(adaptive["tx_adjustments"]) + 1
        self._record_adaptive_event("tx_adjusted", reason)
        self.log(
            "ADAPT_TX_GAIN",
            reason=reason,
            old=f"{old[0]}/{'on' if old[1] else 'off'}",
            new=f"{gain_db}/{'on' if rf_amp_enabled else 'off'}",
        )
        self._write_metrics(force=True)
        return True

    def _note_ack_result(self, received: bool) -> None:
        if not self._adaptive_enabled:
            return
        adaptive = self._adaptive_metrics()
        if received:
            self._consecutive_ack_timeouts = 0
            self._consecutive_ack_successes += 1
            current_tx_state = (self.args.tx_gain, self._tx_rf_amp_enabled)
            if current_tx_state != self._last_proven_tx_state:
                self._last_proven_tx_state = current_tx_state
                self._record_adaptive_event("tx_proven", "ack_received")
            if (
                self._consecutive_ack_successes
                >= ADAPT_TX_SUCCESSES_PER_STEP_DOWN
            ):
                lower = previous_tx_state(
                    self.args.tx_gain, self._tx_rf_amp_enabled
                )
                assert isinstance(lower.gain, int)
                self._apply_tx_state(
                    lower.gain,
                    lower.rf_amp_enabled,
                    reason="stable_ack_headroom",
                )
                self._consecutive_ack_successes = 0
        else:
            self._consecutive_ack_successes = 0
            self._consecutive_ack_timeouts += 1
            if self._consecutive_ack_timeouts >= ADAPT_TX_TIMEOUTS_PER_STEP:
                higher, amp_started, wrapped = next_tx_state(
                    self.args.tx_gain, self._tx_rf_amp_enabled
                )
                assert isinstance(higher.gain, int)
                if amp_started:
                    self.log(
                        "ADAPT_TX_AMP_FALLBACK",
                        reason="normal_gain_search_failed",
                    )
                if wrapped:
                    adaptive["tx_max_holds"] = int(adaptive["tx_max_holds"]) + 1
                    self.log(
                        "ADAPT_TX_MAX_HOLD",
                        reason="gain_search_exhausted",
                        gain=higher.gain,
                        amp="on",
                    )
                self._apply_tx_state(
                    higher.gain,
                    higher.rf_amp_enabled,
                    reason="ack_timeouts",
                )
                self._consecutive_ack_timeouts = 0
        adaptive["consecutive_ack_timeouts"] = self._consecutive_ack_timeouts
        adaptive["consecutive_ack_successes"] = self._consecutive_ack_successes

    def _maintain_adaptive_link(self, now: float | None = None) -> None:
        if not self._adaptive_enabled or self.device is None:
            return
        now = time.monotonic() if now is None else now
        if (
            self._ack_target is not None
            or self._fprime_probe_token is not None
            or now - self._last_tx_s < 0.75
        ):
            self._consecutive_clipped_blocks = 0
            self._last_adaptive_rx_block = int(self.metrics["rx_blocks"])
            return

        rx_blocks = int(self.metrics["rx_blocks"])
        if rx_blocks != self._last_adaptive_rx_block:
            self._last_adaptive_rx_block = rx_blocks
            iq = self.metrics["rx_iq"]
            assert isinstance(iq, dict)
            if float(iq["last_block_clipped_fraction"]) >= ADAPT_RX_CLIP_FRACTION:
                self._consecutive_clipped_blocks += 1
            else:
                self._consecutive_clipped_blocks = 0

        if (
            self._consecutive_clipped_blocks >= ADAPT_RX_CLIP_BLOCKS
            and now - self._last_rx_adjustment_s >= ADAPT_RX_DWELL_S
        ):
            lower = previous_rx_state(
                (self.args.rx_lna_gain, self.args.rx_vga_gain),
                self._rx_rf_amp_enabled,
            )
            assert isinstance(lower.gain, tuple)
            self._apply_rx_state(
                lower.gain[0],
                lower.gain[1],
                lower.rf_amp_enabled,
                reason="iq_clipping",
            )
            self._consecutive_clipped_blocks = 0
            return

        if (
            self._last_valid_frame_s is not None
            and now - self._last_valid_frame_s >= ADAPT_RX_SILENCE_S
            and now - self._last_rx_adjustment_s >= ADAPT_RX_DWELL_S
        ):
            higher, amp_started, wrapped = next_rx_state(
                (self.args.rx_lna_gain, self.args.rx_vga_gain),
                self._rx_rf_amp_enabled,
            )
            assert isinstance(higher.gain, tuple)
            adaptive = self._adaptive_metrics()
            if amp_started:
                self.log(
                    "ADAPT_RX_AMP_FALLBACK",
                    reason="normal_gain_search_failed",
                )
            if wrapped:
                adaptive["rx_max_holds"] = int(adaptive["rx_max_holds"]) + 1
                self.log(
                    "ADAPT_RX_MAX_HOLD",
                    reason="gain_search_exhausted",
                    lna=higher.gain[0],
                    vga=higher.gain[1],
                    amp="on",
                )
            self._apply_rx_state(
                higher.gain[0],
                higher.gain[1],
                higher.rf_amp_enabled,
                reason="frame_silence",
            )

    def _measure_rx_candidate(
        self, lna_gain_db: int, vga_gain_db: int, rf_amp_enabled: bool
    ) -> RxWindow:
        assert self.device is not None
        self._set_rx_amp(rf_amp_enabled)
        self.device.set_rx_gains(lna_gain_db, vga_gain_db)
        self._reset_rx_measurement_pipeline()
        # Do not score samples captured while the analog gain stages settle.
        settle_deadline = time.monotonic() + 0.150
        while time.monotonic() < settle_deadline and not self.stopping:
            try:
                self.device.read_rx_block(timeout_s=0.025)
            except HackRFTimeoutError:
                pass
        self._reset_rx_measurement_pipeline()

        frame_start = int(self.metrics["rf22_frames"])
        iq = self.metrics["rx_iq"]
        assert isinstance(iq, dict)
        clip_start = int(iq["clipped_complex_samples"])
        drops_start = self.device.rx_dropped_blocks
        deadline = time.monotonic() + self.args.auto_rx_window_s
        while not self.stopping and time.monotonic() < deadline:
            self._poll_rx(0.050, low_latency=True)
            if int(self.metrics["rf22_frames"]) > frame_start:
                break
        window = RxWindow(
            lna_gain_db=lna_gain_db,
            vga_gain_db=vga_gain_db,
            valid_frames=int(self.metrics["rf22_frames"]) - frame_start,
            clipped_samples=int(iq["clipped_complex_samples"]) - clip_start,
            dropped_blocks=self.device.rx_dropped_blocks - drops_start,
            rf_amp_enabled=rf_amp_enabled,
        )
        self.log(
            "AUTO_RX_PROBE",
            lna=lna_gain_db,
            vga=vga_gain_db,
            amp=str(rf_amp_enabled).lower(),
            frames=window.valid_frames,
            clipped=window.clipped_samples,
            dropped=window.dropped_blocks,
            passed=str(window.passed).lower(),
        )
        return window

    def _calibrate_rx(self) -> RxWindow:
        calibration = self.metrics["calibration"]
        assert isinstance(calibration, dict)
        windows: list[RxWindow] = []
        for rf_amp_enabled in RF_AMP_SEARCH_STATES:
            if rf_amp_enabled:
                self.log("AUTO_RX_AMP_FALLBACK", reason="normal_gain_search_failed")
            stage_windows: list[RxWindow] = []
            for lna_gain_db, vga_gain_db in RX_CANDIDATES:
                if self.stopping:
                    raise RuntimeError("automatic RX calibration was interrupted")
                window = self._measure_rx_candidate(
                    lna_gain_db, vga_gain_db, rf_amp_enabled
                )
                windows.append(window)
                stage_windows.append(window)
                calibration["rx_windows"] = [
                    item.__dict__ | {"passed": item.passed} for item in windows
                ]
                self._write_metrics(force=True)
                selected = select_rx_window(stage_windows)
                if selected is not None:
                    self.args.rx_lna_gain = selected.lna_gain_db
                    self.args.rx_vga_gain = selected.vga_gain_db
                    self.metrics["rx_lna_gain"] = selected.lna_gain_db
                    self.metrics["rx_vga_gain"] = selected.vga_gain_db
                    self.metrics["rx_rf_amp_enabled"] = selected.rf_amp_enabled
                    self.metrics["rf_amp_enabled"] = (
                        selected.rf_amp_enabled or self._tx_rf_amp_enabled
                    )
                    return selected
        raise RuntimeError(
            "automatic RX calibration found no clean CRC-valid C3M RF22 frame"
        )

    def _calibrate_tx(self) -> tuple[int, bool]:
        assert self.device is not None
        calibration = self.metrics["calibration"]
        assert isinstance(calibration, dict)
        probes: list[dict[str, object]] = []
        for rf_amp_enabled in RF_AMP_SEARCH_STATES:
            if rf_amp_enabled:
                self.log("AUTO_TX_AMP_FALLBACK", reason="normal_gain_search_failed")
            for gain_db in TX_CANDIDATES:
                if self.stopping:
                    raise RuntimeError("automatic TX calibration was interrupted")
                self._tx_rf_amp_enabled = rf_amp_enabled
                self.device.set_tx_gain(gain_db)
                msg_id = self.message_ids.reserve(0)
                token = (int(time.time_ns()) ^ (gain_db << 24) ^ msg_id) & 0xFFFFFFFF
                command = build_fprime_ping_command(token, msg_id)
                packet = build_message_packets(
                    command,
                    channel=0,
                    msg_id=msg_id,
                    direction="uplink",
                    profile=self.profile,
                )[0]
                self._fprime_probe_token = token.to_bytes(4, "big")
                self._fprime_probe_response = False
                waveform = modulate(
                    packet,
                    repeat=1,
                    offset_hz=0,
                    leading_silence_s=self.args.tx_leading_ms / 1000.0,
                    inter_burst_silence_s=0,
                    trailing_silence_s=0,
                )
                self.metrics["tx_attempts"] = int(self.metrics["tx_attempts"]) + 1
                self._switch_and_transmit(waveform)
                deadline = time.monotonic() + self.args.auto_tx_response_s
                while not self.stopping and time.monotonic() < deadline:
                    self._poll_rx(0.050, low_latency=True)
                    if self._fprime_probe_response:
                        break
                passed = self._fprime_probe_response
                self._fprime_probe_token = None
                probe = {
                    "gain_db": gain_db,
                    "rf_amp_enabled": rf_amp_enabled,
                    "pong": passed,
                    "token": token,
                }
                probes.append(probe)
                calibration["tx_probes"] = probes
                self._write_metrics(force=True)
                self.log(
                    "AUTO_TX_PROBE",
                    gain=gain_db,
                    amp=str(rf_amp_enabled).lower(),
                    pong=str(passed).lower(),
                )
                if passed:
                    self.args.tx_gain = gain_db
                    self.metrics["tx_gain"] = gain_db
                    self.metrics["tx_rf_amp_enabled"] = rf_amp_enabled
                    self.metrics["rf_amp_enabled"] = (
                        self._rx_rf_amp_enabled or rf_amp_enabled
                    )
                    return gain_db, rf_amp_enabled
        raise RuntimeError(
            "automatic TX calibration received no F Prime PING response across the HackRF TX VGA range"
        )

    def _auto_calibrate(self) -> None:
        calibration = self.metrics["calibration"]
        assert isinstance(calibration, dict)
        calibration["state"] = "running"
        calibration["runs"] = int(calibration["runs"]) + 1
        calibration["rx_windows"] = []
        calibration["tx_probes"] = []
        calibration["selected"] = None
        self.metrics["radio_state"] = "calibrating"
        self._write_metrics(force=True)
        selected_rx = self._calibrate_rx()
        selected_tx, selected_tx_amp = (
            (0, False) if not self.args.enable_tx else self._calibrate_tx()
        )
        calibration["selected"] = {
            "rx_lna_gain_db": selected_rx.lna_gain_db,
            "rx_vga_gain_db": selected_rx.vga_gain_db,
            "rx_rf_amp_enabled": selected_rx.rf_amp_enabled,
            "tx_gain_db": selected_tx,
            "tx_rf_amp_enabled": selected_tx_amp,
        }
        calibration["state"] = "complete"
        self._reset_rx_measurement_pipeline()
        self.log(
            "AUTO_CALIBRATION_COMPLETE",
            rx_lna=selected_rx.lna_gain_db,
            rx_vga=selected_rx.vga_gain_db,
            rx_amp=str(selected_rx.rf_amp_enabled).lower(),
            tx=selected_tx,
            tx_amp=str(selected_tx_amp).lower(),
        )
        self._initialize_adaptive_link()

    def _send_packet_unacknowledged(
        self, packet: bytes, channel: int, msg_id: int, segment_index: int
    ) -> bool:
        if channel != 1:
            raise ValueError("only channel 1 may use ACK-free application repair")
        repeats = self.args.payload_repeats
        waveform = modulate(
            packet,
            repeat=repeats,
            offset_hz=0,
            leading_silence_s=self.args.tx_leading_ms / 1000.0,
            inter_burst_silence_s=self.args.repeat_gap_ms / 1000.0,
            trailing_silence_s=0,
        )
        self.metrics["tx_attempts"] = int(self.metrics["tx_attempts"]) + repeats
        self.metrics["degraded_segments"] = int(self.metrics["degraded_segments"]) + 1
        elapsed = self._switch_and_transmit(waveform)
        self.log(
            "TX_UNACKNOWLEDGED",
            channel=channel,
            id=f"0x{msg_id:02x}",
            segment=segment_index,
            repeats=repeats,
            elapsed_s=f"{elapsed:.4f}",
            ack="not_observed",
        )
        return True

    def _transmit_message(self, channel: int, message: bytes) -> None:
        msg_id = self.message_ids.reserve(channel)
        packets = build_message_packets(
            message,
            channel=channel,
            msg_id=msg_id,
            direction="uplink",
            profile=self.profile,
        )
        self.log(
            "TX_MESSAGE",
            channel=channel,
            id=f"0x{msg_id:02x}",
            bytes=len(message),
            segments=len(packets),
            mode=self.args.tx_mode,
        )
        success = True
        for segment_index, packet in enumerate(packets):
            if channel == 0:
                sent = self._send_packet_acknowledged(
                    packet, channel, msg_id, segment_index
                )
            else:
                sent = self._send_packet_unacknowledged(
                    packet, channel, msg_id, segment_index
                )
            if not sent:
                success = False
                break
            self.metrics["tx_segments"] = int(self.metrics["tx_segments"]) + 1

        if success:
            tx_messages = self.metrics["tx_messages"]
            assert isinstance(tx_messages, dict)
            key = str(channel)
            tx_messages[key] = int(tx_messages[key]) + 1
        else:
            self.metrics["tx_failures"] = int(self.metrics["tx_failures"]) + 1
            self.log(
                "TX_FAILED",
                channel=channel,
                id=f"0x{msg_id:02x}",
                bytes=len(message),
            )

    def _connected_loop(self) -> None:
        assert self.device is not None
        while not self.stopping:
            self._poll_rx(0.025)
            self._service_ptys()
            if self.tx_queue:
                channel, message = self._dequeue_tx()
                self._transmit_message(channel, message)
            self._maintain_adaptive_link()
            self._write_metrics()

    def run(self) -> int:
        reconnect_delay = 0.5
        run_failed = False
        try:
            self._open_runtime()
            while not self.stopping:
                config = HackRFConfig(
                    center_freq_hz=RX_CENTER_HZ,
                    sample_rate_hz=SAMPLE_RATE,
                    bandwidth_hz=1_750_000,
                    rx_lna_gain_db=self.args.rx_lna_gain,
                    rx_vga_gain_db=self.args.rx_vga_gain,
                    tx_vga_gain_db=self.args.tx_gain,
                    amp_enabled=False,
                    antenna_power_enabled=False,
                    rx_queue_blocks=self.args.rx_queue_blocks,
                )
                try:
                    with HackRFDevice(self.args.serial, config=config) as device:
                        self.device = device
                        device.start_rx()
                        if getattr(self.args, "auto_calibrate", False):
                            self._auto_calibrate()
                        else:
                            self._initialize_adaptive_link()
                        self.metrics["radio_state"] = "receiving"
                        self.metrics["last_error"] = None
                        self.log("HACKRF_READY", serial=self.args.serial, center=RX_CENTER_HZ)
                        self._write_metrics(force=True)
                        reconnect_delay = 0.5
                        self._connected_loop()
                except (HackRFError, OSError) as exc:
                    self.metrics["radio_state"] = "reconnecting"
                    self.metrics["last_error"] = str(exc)
                    self.metrics["reconnects"] = int(self.metrics["reconnects"]) + 1
                    self.log("HACKRF_ERROR", error=repr(exc), retry_s=reconnect_delay)
                    self._write_metrics(force=True)
                    deadline = time.monotonic() + reconnect_delay
                    while not self.stopping and time.monotonic() < deadline:
                        self._service_ptys()
                        time.sleep(0.025)
                    reconnect_delay = min(5.0, reconnect_delay * 2)
                finally:
                    self.device = None
                    self._rx_accumulator.clear()
                    self.decoder.reset()
                    self.reassembler.reset()
            return 0
        except BridgeBackpressureError as exc:
            run_failed = True
            self.metrics["last_error"] = str(exc)
            self.log("BRIDGE_FATAL", error=repr(exc))
            return 1
        except Exception as exc:
            run_failed = True
            self.metrics["last_error"] = str(exc)
            self.log("BRIDGE_FATAL", error=repr(exc))
            raise
        finally:
            active_error = sys.exc_info()[1]
            try:
                self._close_runtime(failed=run_failed)
            except Exception as cleanup_exc:
                if active_error is None:
                    raise
                self.log("BRIDGE_CLEANUP_FAILED", error=repr(cleanup_exc))


def default_state_dir() -> Path:
    return Path.home() / ".local" / "state" / "fprime-hackrf-rf22"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gds-symlink", type=Path, default=Path("/tmp/c3m-sdr/gds-port"))
    parser.add_argument("--payload-symlink", type=Path, default=Path("/tmp/c3m-sdr/payload-port"))
    parser.add_argument("--symlink", type=Path, dest="gds_symlink", help=argparse.SUPPRESS)
    parser.add_argument("--metrics-file", type=Path, default=Path("/tmp/c3m-sdr/bridge-status.json"))
    parser.add_argument(
        "--uplink-capture-dir", type=Path, default=Path("/tmp/c3m-sdr/uplink")
    )
    parser.add_argument("--state-dir", type=Path, default=default_state_dir())
    parser.add_argument("--lock-file", type=Path)
    parser.add_argument("--message-id-state", type=Path)
    parser.add_argument("--serial", default=DEFAULT_SERIAL)
    parser.add_argument("--network", default=DEFAULT_PROFILE.name)
    parser.add_argument("--enable-tx", action="store_true")
    parser.add_argument("--auto-calibrate", action="store_true")
    parser.add_argument("--auto-rx-window-s", type=float, default=3.0)
    parser.add_argument("--auto-tx-response-s", type=float, default=1.5)
    parser.add_argument("--tx-gain", type=int, choices=range(0, 48), default=0)
    parser.add_argument("--tx-safety-confirmed", action="store_true")
    parser.add_argument("--allow-elevated-tx-gain", action="store_true")
    parser.add_argument("--rf-path-label", default=DEFAULT_RF_PATH_LABEL)
    parser.add_argument("--payload-repeats", type=int, default=1)
    parser.add_argument("--repeat-gap-ms", type=float, default=25.0)
    # The HackRF TX path needs time for its PLL/front end to settle before the
    # first RF22 preamble.  A 5 ms lead consistently lost the only burst on
    # this bench; 100 ms delivered the same single frame through both
    # hackrf_transfer and the direct libhackrf wrapper.  The lead is zero-I/Q,
    # so the addressed RF22 packet is still transmitted exactly once.
    parser.add_argument("--tx-leading-ms", type=float, default=BASELINE_TX_LEADING_MS)
    parser.add_argument("--tx-timeout", type=float, default=3.0)
    parser.add_argument("--stop-timeout", type=float, default=1.0)
    parser.add_argument(
        "--rx-lna-gain",
        type=int,
        choices=range(0, 41, 8),
        default=BASELINE_RX_LNA_GAIN_DB,
    )
    parser.add_argument(
        "--rx-vga-gain",
        type=int,
        choices=range(0, 63, 2),
        default=BASELINE_RX_VGA_GAIN_DB,
    )
    parser.add_argument("--rx-queue-blocks", type=int, default=64)
    parser.add_argument("--rx-block-ms", type=float, default=100.0)
    parser.add_argument("--rx-overlap-ms", type=float, default=25.0)
    parser.add_argument("--uplink-idle-ms", type=float, default=12.0)
    parser.add_argument("--max-pty-backlog", type=int, default=1 << 20)
    parser.add_argument("--max-tx-queue-messages", type=int, default=256)
    parser.add_argument("--max-tx-queue-bytes", type=int, default=1 << 20)
    parser.set_defaults(tx_mode=BASELINE_TX_MODE)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.rf_path_label = validate_tx_request(
            enable_tx=args.enable_tx,
            tx_gain=args.tx_gain,
            tx_safety_confirmed=args.tx_safety_confirmed,
            allow_elevated_tx_gain=args.allow_elevated_tx_gain,
            rf_path_label=args.rf_path_label,
        )
    except TxSafetyError as exc:
        parser.error(str(exc))
    if not args.enable_tx:
        args.tx_gain = 0
        args.tx_safety_confirmed = False
        args.allow_elevated_tx_gain = False
    if args.payload_repeats < 1:
        parser.error("payload repeat count must be at least one")
    if args.auto_rx_window_s <= 0 or args.auto_tx_response_s <= 0:
        parser.error("automatic calibration windows must be positive")
    if args.rx_block_ms <= args.rx_overlap_ms:
        parser.error("--rx-block-ms must be greater than --rx-overlap-ms")
    if (
        args.max_pty_backlog < 1
        or args.max_tx_queue_messages < 1
        or args.max_tx_queue_bytes < 1
    ):
        parser.error("PTY and TX queue limits must be at least one")
    args.state_dir = args.state_dir.expanduser().resolve()
    args.lock_file = args.lock_file or args.state_dir / f"{args.network}-{args.serial}.lock"
    args.message_id_state = (
        args.message_id_state
        or args.state_dir / f"{args.network}-{args.serial}-message-ids.json"
    )

    bridge = HackRfGroundBridge(args)
    signal.signal(signal.SIGINT, bridge.request_stop)
    signal.signal(signal.SIGTERM, bridge.request_stop)
    try:
        with BridgeLock(args.lock_file):
            return bridge.run()
    except Exception as exc:
        print(f"BRIDGE_START_FAILED error={exc!r}", file=sys.stderr, flush=True)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
