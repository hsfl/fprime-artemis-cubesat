#!/usr/bin/env python
"""Receive generic channel-1 payload blobs from the ground Teensy.

The stream is intentionally filetype-agnostic. It reconstructs bytes by packet
index, verifies CRCs, writes the blob, and sends retry bitmaps when needed.

Two modes:
  * single-file (default): wait for one transfer and write it to --output.
  * directory (--output-dir DIR): keep listening and save every completed
    transfer into DIR as a uniquely named file for background HIL capture.
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import pathlib
import struct
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass

import serial


MAGIC = b"N2"
PREVIEW_MAGIC = b"PV"
TYPE_HEADER = 1
TYPE_DATA = 2
TYPE_END = 3
TYPE_RETRY_REQUEST = 4
MAX_PACKET = 44
DATA_BYTES = 35
PREVIEW_VERSION = 1
PREVIEW_TYPE_FRAGMENT = 1
PREVIEW_WIDTH = 80
PREVIEW_HEIGHT = 60
PREVIEW_PIXEL_FORMAT_U8 = 1
PREVIEW_TOTAL_BYTES = PREVIEW_WIDTH * PREVIEW_HEIGHT
PREVIEW_HEADER_BYTES = 20
PREVIEW_FRAGMENT_DATA_BYTES = 24
PREVIEW_FINALIZE_DEADLINE_S = 0.75
RETRY_INTERVAL_S = 5.0
RETRY_AFTER_SILENCE_S = 8.0
RETRY_AFTER_REPAIR_QUIET_S = 0.5
CHECKPOINT_SCHEMA_VERSION = 1
DEFAULT_CHECKPOINT_MAX_AGE_S = 24 * 60 * 60
CHECKPOINT_OWNER_FILENAME = ".payload-receiver-owned"
CHECKPOINT_OWNER_VALUE = "neutron-payload-receiver-checkpoint-v1\n"


@dataclass(frozen=True)
class ReceiverEvent:
    """Immutable receiver state update for operator-facing integrations."""

    kind: str
    timestamp_s: float
    message: str
    port: str
    product_id: int
    transfer_id: int | None
    total_bytes: int
    received_bytes: int
    total_packets: int
    received_packets: int
    missing_packets: int
    retry_rounds: int
    expected_crc: int | None
    actual_crc: int | None = None
    crc_ok: bool | None = None
    output_path: str | None = None
    retry_start: int | None = None
    retry_count: int | None = None
    retry_bitmap_bytes: int | None = None
    error_phase: str | None = None
    error: str | None = None
    partial: bool = False
    missing_packet_indices: tuple[int, ...] = ()
    timeout_reason: str | None = None
    completion_reason: str | None = None
    packet_data_bytes: int = DATA_BYTES
    missing_map_path: str | None = None
    transfer_started_at_s: float | None = None
    preview_session_id: int | None = None
    preview_frame_sequence: int | None = None
    preview_width: int | None = None
    preview_height: int | None = None
    preview_total_bytes: int | None = None
    preview_received_bytes: int | None = None
    preview_fragment_count: int | None = None
    preview_received_fragments: int | None = None
    preview_percent: float | None = None
    preview_complete: bool | None = None
    preview_crc_ok: bool | None = None
    preview_finalize_reason: str | None = None
    preview_pixels: bytes | None = None


@dataclass
class PreviewFrame:
    session_id: int
    frame_sequence: int
    width: int
    height: int
    total_bytes: int
    fragment_count: int
    expected_crc: int
    pixels: bytearray
    fragments: dict[int, bytes]
    started_s: float
    last_fragment_s: float


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


class PayloadReceiver:
    def __init__(
        self,
        port: str,
        baud: int,
        output: pathlib.Path,
        timeout_s: float,
        output_dir: pathlib.Path | None = None,
        ext: str = ".bin",
        debug: bool = False,
        on_event: Callable[[ReceiverEvent], None] | None = None,
        serial_factory: Callable[..., object] | None = None,
        stop_requested: Callable[[], bool] | None = None,
        consume_cancel_requested: Callable[[], bool] | None = None,
        transfer_timeout_s: float | None = None,
        absolute_transfer_timeout_s: float | None = None,
        save_partial_on_timeout: bool = False,
        checkpoint_dir: pathlib.Path | None = None,
        checkpoint_max_age_s: float = DEFAULT_CHECKPOINT_MAX_AGE_S,
        source_identity: dict[str, object] | None = None,
        port_resolver: Callable[[], str | None] | None = None,
        reconnect_timeout_s: float = 0.0,
        reconnect_interval_s: float = 0.25,
        save_partial_on_disconnect: bool = False,
        retain_checkpoint_after_complete: bool = False,
    ) -> None:
        self.port = port
        self.baud = baud
        self.output = output
        self.output_dir = output_dir
        self.ext = ext
        self.debug = debug
        self.on_event = on_event
        self.serial_factory = serial_factory or serial.Serial
        self.stop_requested = stop_requested or (lambda: False)
        self.consume_cancel_requested = consume_cancel_requested or (lambda: False)
        self.transfer_timeout_s = transfer_timeout_s
        self.absolute_transfer_timeout_s = absolute_transfer_timeout_s
        self.save_partial_on_timeout = save_partial_on_timeout
        self.checkpoint_dir = checkpoint_dir.resolve() if checkpoint_dir is not None else None
        if self.checkpoint_dir is not None:
            filesystem_root = pathlib.Path(self.checkpoint_dir.anchor)
            reserved_paths = {filesystem_root, pathlib.Path.home().resolve(), self.output.parent.resolve()}
            if self.output_dir is not None:
                reserved_paths.add(self.output_dir.resolve())
            if self.checkpoint_dir in reserved_paths:
                raise ValueError(
                    f"checkpoint directory must be a dedicated child directory: {self.checkpoint_dir}"
                )
        self.checkpoint_max_age_s = checkpoint_max_age_s
        self.source_identity = dict(source_identity or {})
        self.port_resolver = port_resolver
        self.reconnect_timeout_s = max(0.0, reconnect_timeout_s)
        self.reconnect_interval_s = max(0.01, reconnect_interval_s)
        self.save_partial_on_disconnect = save_partial_on_disconnect
        self.retain_checkpoint_after_complete = retain_checkpoint_after_complete
        self.debug_total_bytes = 0
        self.debug_last_report_s = 0.0
        self.debug_seen_magic = False
        self.debug_type_counts: dict[int, int] = {}
        self.received_count = 0
        self.timeout_s = timeout_s
        self.transfer_id: int | None = None
        self.product_id = 0
        self.total_bytes = 0
        self.total_packets = 0
        self.file_crc = 0
        self.packet_data_bytes = DATA_BYTES
        self.packets: dict[int, bytes] = {}
        self.end_seen = False
        self.rx_buffer = bytearray()
        self.next_retry_request_s = 0.0
        self.last_packet_s = 0.0
        self.last_end_s = 0.0
        self.retry_rounds = 0
        self.transfer_started_s = 0.0
        self.transfer_started_wall_s = 0.0
        self.last_packet_wall_s = 0.0
        self.next_retry_request_wall_s = 0.0
        self.checkpoint_rejection_reason: str | None = None
        self.ignored_transfer_id: int | None = None
        # Preview traffic is intentionally transient: it never enters the N2
        # checkpoint, retry, or science-file reconstruction paths.
        self.preview_frame: PreviewFrame | None = None

    @property
    def received_bytes(self) -> int:
        return sum(len(chunk) for chunk in self.packets.values())

    @staticmethod
    def _atomic_write(path: pathlib.Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
        try:
            with temporary.open("wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, path)
            try:
                directory_fd = os.open(path.parent, os.O_RDONLY)
            except OSError:
                directory_fd = None
            if directory_fd is not None:
                try:
                    os.fsync(directory_fd)
                finally:
                    os.close(directory_fd)
        finally:
            temporary.unlink(missing_ok=True)

    def _received_bitmap(self) -> bytes:
        bitmap = bytearray((self.total_packets + 7) // 8)
        for index in self.packets:
            if 0 <= index < self.total_packets:
                bitmap[index // 8] |= 1 << (index % 8)
        return bytes(bitmap)

    def _checkpoint_owner_path(self) -> pathlib.Path:
        assert self.checkpoint_dir is not None
        return self.checkpoint_dir / CHECKPOINT_OWNER_FILENAME

    def _checkpoint_is_owned(self) -> bool:
        if self.checkpoint_dir is None:
            return False
        try:
            return self._checkpoint_owner_path().read_text(encoding="utf-8") == CHECKPOINT_OWNER_VALUE
        except OSError:
            return False

    def _ensure_checkpoint_owned(self) -> None:
        assert self.checkpoint_dir is not None
        if self._checkpoint_is_owned():
            return
        if self.checkpoint_dir.exists() and any(self.checkpoint_dir.iterdir()):
            raise ValueError(
                f"checkpoint directory is not owned by payload receiver: {self.checkpoint_dir}"
            )
        self.checkpoint_dir.mkdir(parents=True, exist_ok=True)
        self._atomic_write(
            self._checkpoint_owner_path(), CHECKPOINT_OWNER_VALUE.encode("utf-8")
        )

    def persist_checkpoint(self, packet_index: int | None = None, reason: str | None = None) -> None:
        if self.checkpoint_dir is None or self.transfer_id is None or self.total_packets <= 0:
            return
        self._ensure_checkpoint_owned()
        packet_dir = self.checkpoint_dir / "packets"
        if packet_index is not None:
            packet = self.packets.get(packet_index)
            if packet is None:
                raise ValueError(f"checkpoint packet {packet_index} is unavailable")
            self._atomic_write(packet_dir / f"{packet_index:06d}.bin", packet)

        now_wall = time.time()
        now_mono = time.monotonic()
        next_retry_wall = (
            now_wall + max(0.0, self.next_retry_request_s - now_mono)
            if self.next_retry_request_s > 0.0
            else 0.0
        )
        manifest = {
            "schema_version": CHECKPOINT_SCHEMA_VERSION,
            "source_identity": self.source_identity,
            "transfer": {
                "transfer_id": self.transfer_id,
                "product_id": self.product_id,
                "total_bytes": self.total_bytes,
                "total_packets": self.total_packets,
                "packet_data_bytes": self.packet_data_bytes,
                "file_crc": self.file_crc,
            },
            "received_bitmap_b64": base64.b64encode(self._received_bitmap()).decode("ascii"),
            "packet_crc16": {
                str(index): crc16_ccitt(chunk) for index, chunk in sorted(self.packets.items())
            },
            "retry_rounds": self.retry_rounds,
            "end_seen": self.end_seen,
            "transfer_started_wall_s": self.transfer_started_wall_s,
            "last_packet_wall_s": self.last_packet_wall_s,
            "next_retry_request_wall_s": next_retry_wall,
            "updated_at_wall_s": now_wall,
            "last_reason": reason,
        }
        encoded = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
        self._atomic_write(self.checkpoint_dir / "manifest.json", encoded)

    def clear_checkpoint(self) -> None:
        if self.checkpoint_dir is None or not self._checkpoint_is_owned():
            return
        packet_dir = self.checkpoint_dir / "packets"
        if packet_dir.is_dir():
            for packet_path in packet_dir.iterdir():
                if (
                    packet_path.is_file()
                    and len(packet_path.stem) == 6
                    and packet_path.stem.isdigit()
                    and packet_path.suffix == ".bin"
                ):
                    packet_path.unlink()
            try:
                packet_dir.rmdir()
            except OSError:
                pass
        (self.checkpoint_dir / "manifest.json").unlink(missing_ok=True)
        remaining = {path.name for path in self.checkpoint_dir.iterdir()}
        if remaining == {CHECKPOINT_OWNER_FILENAME}:
            self._checkpoint_owner_path().unlink()
            self.checkpoint_dir.rmdir()

    def _reject_checkpoint(self, reason: str) -> None:
        self.checkpoint_rejection_reason = reason
        if self.checkpoint_dir is not None and self._checkpoint_is_owned():
            stamp = int(time.time() * 1000)
            rejected = self.checkpoint_dir.with_name(f"{self.checkpoint_dir.name}.rejected.{stamp}")
            suffix = 1
            while rejected.exists():
                rejected = self.checkpoint_dir.with_name(
                    f"{self.checkpoint_dir.name}.rejected.{stamp}.{suffix}"
                )
                suffix += 1
            os.replace(self.checkpoint_dir, rejected)
            self._atomic_write(rejected / "rejection_reason.txt", (reason + "\n").encode("utf-8"))
        self.emit("checkpoint_rejected", reason, error_phase="checkpoint", error=reason)

    def load_checkpoint(self) -> bool:
        if self.checkpoint_dir is None:
            return False
        manifest_path = self.checkpoint_dir / "manifest.json"
        if not manifest_path.is_file():
            return False
        if not self._checkpoint_is_owned():
            reason = f"checkpoint rejected: directory is not owned by payload receiver: {self.checkpoint_dir}"
            self.checkpoint_rejection_reason = reason
            self.emit("checkpoint_rejected", reason, error_phase="checkpoint", error=reason)
            return False
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if manifest.get("schema_version") != CHECKPOINT_SCHEMA_VERSION:
                raise ValueError("unsupported checkpoint schema")
            updated = float(manifest["updated_at_wall_s"])
            age = time.time() - updated
            if age < -300:
                raise ValueError("checkpoint timestamp is in the future")
            if self.checkpoint_max_age_s > 0 and age > self.checkpoint_max_age_s:
                raise ValueError(
                    f"stale checkpoint age {age:.1f}s exceeds {self.checkpoint_max_age_s:g}s"
                )
            saved_source = manifest.get("source_identity") or {}
            if self.source_identity and saved_source != self.source_identity:
                raise ValueError("checkpoint source identity does not match selected Teensy")

            identity = manifest["transfer"]
            transfer_id = int(identity["transfer_id"])
            product_id = int(identity["product_id"])
            total_bytes = int(identity["total_bytes"])
            total_packets = int(identity["total_packets"])
            packet_data_bytes = int(identity["packet_data_bytes"])
            file_crc = int(identity["file_crc"])
            if not (0 <= transfer_id <= 255 and 0 <= product_id <= 0xFFFFFFFF):
                raise ValueError("checkpoint transfer identity is out of range")
            if total_bytes <= 0 or total_packets <= 0 or not (1 <= packet_data_bytes <= DATA_BYTES):
                raise ValueError("checkpoint transfer dimensions are invalid")
            expected_packets = (total_bytes + packet_data_bytes - 1) // packet_data_bytes
            if total_packets != expected_packets or not (0 <= file_crc <= 0xFFFF):
                raise ValueError("checkpoint transfer dimensions are inconsistent")

            bitmap = base64.b64decode(manifest["received_bitmap_b64"], validate=True)
            if len(bitmap) != (total_packets + 7) // 8:
                raise ValueError("checkpoint bitmap length is invalid")
            packet_crc = manifest.get("packet_crc16") or {}
            packets: dict[int, bytes] = {}
            for index in range(total_packets):
                if not (bitmap[index // 8] & (1 << (index % 8))):
                    continue
                packet_path = self.checkpoint_dir / "packets" / f"{index:06d}.bin"
                chunk = packet_path.read_bytes()
                expected_size = min(packet_data_bytes, total_bytes - index * packet_data_bytes)
                if len(chunk) != expected_size:
                    raise ValueError(f"checkpoint packet {index} length is invalid")
                expected_crc = int(packet_crc[str(index)])
                if crc16_ccitt(chunk) != expected_crc:
                    raise ValueError(f"checkpoint packet {index} CRC is invalid")
                packets[index] = chunk

            now_wall = time.time()
            now_mono = time.monotonic()
            started_wall = float(manifest.get("transfer_started_wall_s") or updated)
            last_wall = float(manifest.get("last_packet_wall_s") or updated)
            next_retry_wall = float(manifest.get("next_retry_request_wall_s") or 0.0)
            self.transfer_id = transfer_id
            self.product_id = product_id
            self.total_bytes = total_bytes
            self.total_packets = total_packets
            self.packet_data_bytes = packet_data_bytes
            self.file_crc = file_crc
            self.packets = packets
            self.end_seen = bool(manifest.get("end_seen"))
            self.last_end_s = now_mono if self.end_seen else 0.0
            self.retry_rounds = max(0, int(manifest.get("retry_rounds") or 0))
            self.transfer_started_wall_s = started_wall
            self.last_packet_wall_s = last_wall
            self.transfer_started_s = now_mono - max(0.0, now_wall - started_wall)
            self.last_packet_s = now_mono - max(0.0, now_wall - last_wall)
            self.next_retry_request_wall_s = next_retry_wall
            self.next_retry_request_s = (
                now_mono + max(0.0, next_retry_wall - now_wall) if next_retry_wall > 0 else 0.0
            )
        except (KeyError, TypeError, ValueError, OSError, json.JSONDecodeError) as exc:
            self._reject_checkpoint(f"checkpoint rejected: {exc}")
            self.reset_transfer(clear_checkpoint=False)
            return False

        message = (
            f"resumed checkpoint: product={self.product_id} transfer={self.transfer_id} "
            f"received={len(self.packets)}/{self.total_packets}"
        )
        print(message)
        self.emit("transfer_resumed", message)
        return True

    def emit(
        self,
        kind: str,
        message: str = "",
        *,
        actual_crc: int | None = None,
        crc_ok: bool | None = None,
        output_path: pathlib.Path | None = None,
        retry_start: int | None = None,
        retry_count: int | None = None,
        retry_bitmap_bytes: int | None = None,
        error_phase: str | None = None,
        error: str | None = None,
        partial: bool = False,
        missing_packet_indices: tuple[int, ...] = (),
        timeout_reason: str | None = None,
        completion_reason: str | None = None,
        missing_map_path: pathlib.Path | None = None,
        preview_session_id: int | None = None,
        preview_frame_sequence: int | None = None,
        preview_width: int | None = None,
        preview_height: int | None = None,
        preview_total_bytes: int | None = None,
        preview_received_bytes: int | None = None,
        preview_fragment_count: int | None = None,
        preview_received_fragments: int | None = None,
        preview_percent: float | None = None,
        preview_complete: bool | None = None,
        preview_crc_ok: bool | None = None,
        preview_finalize_reason: str | None = None,
        preview_pixels: bytes | None = None,
    ) -> None:
        if self.on_event is None:
            return
        missing = len(self.missing_packets()) if self.total_packets > 0 else 0
        self.on_event(
            ReceiverEvent(
                kind=kind,
                timestamp_s=time.time(),
                message=message,
                port=self.port,
                product_id=self.product_id,
                transfer_id=self.transfer_id,
                total_bytes=self.total_bytes,
                received_bytes=self.received_bytes,
                total_packets=self.total_packets,
                received_packets=len(self.packets),
                missing_packets=missing,
                retry_rounds=self.retry_rounds,
                expected_crc=self.file_crc if self.transfer_id is not None else None,
                actual_crc=actual_crc,
                crc_ok=crc_ok,
                output_path=str(output_path) if output_path is not None else None,
                retry_start=retry_start,
                retry_count=retry_count,
                retry_bitmap_bytes=retry_bitmap_bytes,
                error_phase=error_phase,
                error=error,
                partial=partial,
                missing_packet_indices=missing_packet_indices,
                timeout_reason=timeout_reason,
                completion_reason=completion_reason,
                packet_data_bytes=self.packet_data_bytes,
                missing_map_path=str(missing_map_path) if missing_map_path is not None else None,
                transfer_started_at_s=(
                    self.transfer_started_wall_s if self.transfer_started_wall_s > 0.0 else None
                ),
                preview_session_id=preview_session_id,
                preview_frame_sequence=preview_frame_sequence,
                preview_width=preview_width,
                preview_height=preview_height,
                preview_total_bytes=preview_total_bytes,
                preview_received_bytes=preview_received_bytes,
                preview_fragment_count=preview_fragment_count,
                preview_received_fragments=preview_received_fragments,
                preview_percent=preview_percent,
                preview_complete=preview_complete,
                preview_crc_ok=preview_crc_ok,
                preview_finalize_reason=preview_finalize_reason,
                preview_pixels=preview_pixels,
            )
        )

    def run(self) -> int:
        deadline = time.monotonic() + self.timeout_s
        opened = False
        try:
            with self.serial_factory(self.port, self.baud, timeout=0.2) as ser:
                opened = True
                self.emit("ready", f"listening on {self.port}")
                self.load_checkpoint()
                while time.monotonic() < deadline:
                    packet = self.read_packet(ser)
                    if not packet:
                        self.request_retries_if_due(ser)
                        if self.complete:
                            break
                        continue
                    self.handle_packet(packet, ser)
                    self.finalize_preview_if_due()
                    self.request_retries_if_due(ser)
                    if self.complete:
                        break

                if not self.complete:
                    self.request_retries_if_due(ser, force=True)
                    retry_deadline = time.monotonic() + min(20.0, self.timeout_s)
                    while time.monotonic() < retry_deadline and not self.complete:
                        packet = self.read_packet(ser)
                        if packet:
                            self.handle_packet(packet, ser)
                        self.finalize_preview_if_due()
                        self.request_retries_if_due(ser)
        except serial.SerialException as exc:
            phase = "io" if opened else "open"
            self.persist_checkpoint(reason=f"serial {phase} failure: {exc}")
            self.emit("serial_error", str(exc), error_phase=phase, error=str(exc))
            raise

        if not self.complete:
            missing = self.missing_packets()
            message = f"incomplete: received={len(self.packets)} total={self.total_packets} missing={len(missing)}"
            print(message)
            self.emit("incomplete", message)
            return 2

        blob = self.reconstruct()
        actual_crc = crc16_ccitt(blob)
        if actual_crc != self.file_crc:
            message = f"crc mismatch: actual=0x{actual_crc:04x} expected=0x{self.file_crc:04x}"
            print(message)
            self.emit("crc_checked", message, actual_crc=actual_crc, crc_ok=False)
            return 3

        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_bytes(blob)
        self.clear_checkpoint()
        message = (
            "complete: "
            f"product={self.product_id} transfer={self.transfer_id} bytes={len(blob)} "
            f"packets={self.total_packets} crc=0x{actual_crc:04x} output={self.output}"
        )
        print(message)
        self.emit("crc_checked", "CRC verified", actual_crc=actual_crc, crc_ok=True)
        self.emit(
            "transfer_saved",
            message,
            actual_crc=actual_crc,
            crc_ok=True,
            output_path=self.output,
        )
        return 0

    @property
    def complete(self) -> bool:
        return self.total_packets > 0 and len(self.packets) == self.total_packets

    def read_packet(self, ser: serial.Serial) -> bytes:
        packet = self.try_extract_packet()
        if packet:
            return packet

        partial_deadline = time.monotonic() + 0.5
        while time.monotonic() < partial_deadline:
            chunk = ser.read(MAX_PACKET)
            if chunk:
                self.debug_report(chunk)
                self.rx_buffer += chunk
                packet = self.try_extract_packet()
                if packet:
                    return packet
            elif not self.rx_buffer:
                return b""
        return b""

    def debug_report(self, chunk: bytes) -> None:
        if not self.debug:
            return
        self.debug_total_bytes += len(chunk)
        if not self.debug_seen_magic and MAGIC in (self.rx_buffer[-1:] + chunk):
            self.debug_seen_magic = True
            index = chunk.find(MAGIC)
            snippet = chunk[index : index + 8] if index >= 0 else chunk[:8]
            print(f"debug: first N2 magic seen; bytes={snippet.hex(' ')}")
        now = time.monotonic()
        if now - self.debug_last_report_s >= 1.0:
            self.debug_last_report_s = now
            counts = self.debug_type_counts
            other = sum(
                value
                for key, value in counts.items()
                if key not in (TYPE_HEADER, TYPE_DATA, TYPE_END)
            )
            print(
                f"debug: rx_bytes_total={self.debug_total_bytes} "
                f"types[header={counts.get(TYPE_HEADER, 0)} data={counts.get(TYPE_DATA, 0)} "
                f"end={counts.get(TYPE_END, 0)} other={other}] "
                f"header_seen={self.transfer_id is not None} "
                f"packets={len(self.packets)}/{self.total_packets}"
            )

    def try_extract_packet(self) -> bytes:
        while True:
            n2_index = self.rx_buffer.find(MAGIC)
            preview_index = self.rx_buffer.find(PREVIEW_MAGIC)
            candidates = [index for index in (n2_index, preview_index) if index >= 0]
            magic_index = min(candidates) if candidates else -1
            if magic_index < 0:
                trailing_magic_prefix = (
                    self.rx_buffer[-1:]
                    if self.rx_buffer.endswith((MAGIC[:1], PREVIEW_MAGIC[:1]))
                    else b""
                )
                self.rx_buffer.clear()
                self.rx_buffer += trailing_magic_prefix
                return b""
            if magic_index > 0:
                del self.rx_buffer[:magic_index]
            if len(self.rx_buffer) < 4:
                return b""

            if self.rx_buffer[:2] == PREVIEW_MAGIC:
                if len(self.rx_buffer) < PREVIEW_HEADER_BYTES:
                    return b""
                if (
                    self.rx_buffer[2] != PREVIEW_VERSION
                    or self.rx_buffer[3] != PREVIEW_TYPE_FRAGMENT
                    or not 1 <= self.rx_buffer[15] <= PREVIEW_FRAGMENT_DATA_BYTES
                ):
                    del self.rx_buffer[0]
                    continue
                needed = PREVIEW_HEADER_BYTES + self.rx_buffer[15]
            else:
                packet_type = self.rx_buffer[2]
                if packet_type == TYPE_HEADER:
                    needed = 17
                elif packet_type == TYPE_END:
                    needed = 8
                elif packet_type == TYPE_DATA:
                    if len(self.rx_buffer) < 7:
                        return b""
                    valid_len = self.rx_buffer[6]
                    if valid_len > self.packet_data_bytes:
                        del self.rx_buffer[0]
                        continue
                    needed = 7 + valid_len + 2
                elif packet_type == TYPE_RETRY_REQUEST:
                    if len(self.rx_buffer) < 7:
                        return b""
                    needed = 7 + self.rx_buffer[6]
                else:
                    del self.rx_buffer[0]
                    continue

            if needed > MAX_PACKET:
                del self.rx_buffer[0]
                continue
            if len(self.rx_buffer) < needed:
                return b""
            packet = bytes(self.rx_buffer[:needed])
            del self.rx_buffer[:needed]
            return packet

    def handle_packet(self, packet: bytes, ser: serial.Serial) -> None:
        if len(packet) >= 2 and packet[:2] == PREVIEW_MAGIC:
            self.handle_preview_fragment(packet)
            return
        if len(packet) < 4 or packet[0:2] != MAGIC:
            return
        packet_type = packet[2]
        transfer_id = packet[3]

        if self.debug:
            self.debug_type_counts[packet_type] = self.debug_type_counts.get(packet_type, 0) + 1

        if packet_type == TYPE_HEADER:
            self.handle_header(packet)
            return

        if self.transfer_id is None or transfer_id != self.transfer_id:
            return

        if packet_type == TYPE_DATA:
            self.handle_data(packet)
        elif packet_type == TYPE_END:
            self.end_seen = True
            self.last_end_s = time.monotonic()
            self.persist_checkpoint(reason="end packet accepted")

    def handle_preview_fragment(self, packet: bytes) -> None:
        """Accept a best-effort, downsampled preview fragment without N2 side effects."""

        if len(packet) < PREVIEW_HEADER_BYTES or packet[:2] != PREVIEW_MAGIC:
            return
        (
            version,
            packet_type,
            session_id,
            frame_sequence,
            width,
            height,
            pixel_format,
            fragment_index,
            fragment_count,
            data_length,
            total_bytes,
            expected_crc,
        ) = (
            packet[2],
            packet[3],
            struct.unpack_from("<H", packet, 4)[0],
            struct.unpack_from("<I", packet, 6)[0],
            packet[10],
            packet[11],
            packet[12],
            packet[13],
            packet[14],
            packet[15],
            struct.unpack_from("<H", packet, 16)[0],
            struct.unpack_from("<H", packet, 18)[0],
        )
        if (
            version != PREVIEW_VERSION
            or packet_type != PREVIEW_TYPE_FRAGMENT
            or width != PREVIEW_WIDTH
            or height != PREVIEW_HEIGHT
            or pixel_format != PREVIEW_PIXEL_FORMAT_U8
            or total_bytes != PREVIEW_TOTAL_BYTES
            or not 1 <= fragment_count <= 200
            or fragment_index >= fragment_count
            or not 1 <= data_length <= PREVIEW_FRAGMENT_DATA_BYTES
            or len(packet) != PREVIEW_HEADER_BYTES + data_length
            or fragment_count != (total_bytes + PREVIEW_FRAGMENT_DATA_BYTES - 1) // PREVIEW_FRAGMENT_DATA_BYTES
        ):
            return
        offset = fragment_index * PREVIEW_FRAGMENT_DATA_BYTES
        expected_length = min(PREVIEW_FRAGMENT_DATA_BYTES, total_bytes - offset)
        if data_length != expected_length:
            return
        chunk = packet[PREVIEW_HEADER_BYTES:]
        now = time.monotonic()
        active = self.preview_frame
        identity = (session_id, frame_sequence)
        if active is not None and identity != (active.session_id, active.frame_sequence):
            self.finalize_preview("next_frame", now=now)
            active = None
        if active is None:
            active = PreviewFrame(
                session_id=session_id,
                frame_sequence=frame_sequence,
                width=width,
                height=height,
                total_bytes=total_bytes,
                fragment_count=fragment_count,
                expected_crc=expected_crc,
                pixels=bytearray(b"\xff" * total_bytes),
                fragments={},
                started_s=now,
                last_fragment_s=now,
            )
            self.preview_frame = active
        elif (
            active.width != width
            or active.height != height
            or active.total_bytes != total_bytes
            or active.fragment_count != fragment_count
            or active.expected_crc != expected_crc
        ):
            # Same identity with incompatible metadata is malformed and cannot
            # be allowed to contaminate the displayed frame.
            return
        existing = active.fragments.get(fragment_index)
        if existing is not None and existing != chunk:
            return
        if existing is None:
            active.fragments[fragment_index] = chunk
            active.pixels[offset : offset + data_length] = chunk
        active.last_fragment_s = now
        if len(active.fragments) == active.fragment_count:
            self.finalize_preview("all_fragments", now=now)

    def finalize_preview_if_due(self, now: float | None = None) -> bool:
        active = self.preview_frame
        if active is None:
            return False
        now = time.monotonic() if now is None else now
        if now - active.last_fragment_s < PREVIEW_FINALIZE_DEADLINE_S:
            return False
        self.finalize_preview("deadline", now=now)
        return True

    def finalize_preview(self, reason: str, *, now: float | None = None) -> None:
        active = self.preview_frame
        if active is None:
            return
        now = time.monotonic() if now is None else now
        complete = len(active.fragments) == active.fragment_count
        received_bytes = sum(len(chunk) for chunk in active.fragments.values())
        pixels = bytes(active.pixels)
        actual_crc = crc16_ccitt(pixels) if complete else None
        percent = round(100.0 * received_bytes / active.total_bytes, 1)
        status = "complete" if complete else "partial"
        message = (
            f"preview {status}: session={active.session_id} frame={active.frame_sequence} "
            f"received={len(active.fragments)}/{active.fragment_count} ({percent:.1f}%) reason={reason}"
        )
        self.emit(
            "preview_frame",
            message,
            preview_session_id=active.session_id,
            preview_frame_sequence=active.frame_sequence,
            preview_width=active.width,
            preview_height=active.height,
            preview_total_bytes=active.total_bytes,
            preview_received_bytes=received_bytes,
            preview_fragment_count=active.fragment_count,
            preview_received_fragments=len(active.fragments),
            preview_percent=percent,
            preview_complete=complete,
            preview_crc_ok=(actual_crc == active.expected_crc) if complete else None,
            preview_finalize_reason=reason,
            preview_pixels=pixels,
        )
        self.preview_frame = None

    def request_retries_if_due(self, ser: serial.Serial, force: bool = False) -> None:
        now = time.monotonic()
        header_seen = self.transfer_id is not None and self.total_packets > 0
        if not self.end_seen and (
            not header_seen or self.last_packet_s == 0.0 or now - self.last_packet_s < RETRY_AFTER_SILENCE_S
        ):
            return
        if (
            self.end_seen
            and self.last_packet_s > self.last_end_s
            and now - self.last_packet_s < RETRY_AFTER_REPAIR_QUIET_S
        ):
            return
        if not force and now < self.next_retry_request_s:
            return
        if self.request_retries(ser):
            self.next_retry_request_s = now + RETRY_INTERVAL_S
            self.next_retry_request_wall_s = time.time() + RETRY_INTERVAL_S
            self.persist_checkpoint(reason="retry requested")

    def handle_header(self, packet: bytes) -> None:
        if len(packet) < 17:
            return
        transfer_id = packet[3]
        product_id = struct.unpack_from("<I", packet, 4)[0]
        total_bytes = struct.unpack_from("<I", packet, 8)[0]
        total_packets = struct.unpack_from("<H", packet, 12)[0]
        packet_data_bytes = packet[14]
        file_crc = struct.unpack_from("<H", packet, 15)[0]
        if total_bytes <= 0 or total_packets <= 0 or not (1 <= packet_data_bytes <= DATA_BYTES):
            return
        if total_packets != (total_bytes + packet_data_bytes - 1) // packet_data_bytes:
            return
        if self.ignored_transfer_id is not None:
            if transfer_id == self.ignored_transfer_id:
                return
            self.ignored_transfer_id = None
        same_transfer = (
            self.transfer_id == transfer_id
            and self.product_id == product_id
            and self.total_bytes == total_bytes
            and self.total_packets == total_packets
            and self.packet_data_bytes == packet_data_bytes
            and self.file_crc == file_crc
        )
        if same_transfer:
            self.last_packet_s = time.monotonic()
            self.last_packet_wall_s = time.time()
            self.persist_checkpoint(reason="repeated header accepted")
            return

        self.clear_checkpoint()
        self.transfer_id = transfer_id
        self.product_id = product_id
        self.total_bytes = total_bytes
        self.total_packets = total_packets
        self.packet_data_bytes = packet_data_bytes
        self.file_crc = file_crc
        self.packets.clear()
        self.end_seen = False
        self.last_end_s = 0.0
        self.last_packet_s = time.monotonic()
        self.last_packet_wall_s = time.time()
        self.transfer_started_s = self.last_packet_s
        self.transfer_started_wall_s = self.last_packet_wall_s
        self.next_retry_request_wall_s = 0.0
        message = (
            "header: "
            f"product={self.product_id} transfer={self.transfer_id} bytes={self.total_bytes} "
            f"packets={self.total_packets} crc=0x{self.file_crc:04x}"
        )
        print(message)
        self.persist_checkpoint(reason="header accepted")
        self.emit("transfer_started", message)

    def handle_data(self, packet: bytes) -> None:
        if len(packet) < 9:
            return
        index = struct.unpack_from("<H", packet, 4)[0]
        valid_len = packet[6]
        crc_offset = 7 + valid_len
        if valid_len > self.packet_data_bytes or crc_offset + 2 > len(packet):
            return
        expected_crc = struct.unpack_from("<H", packet, crc_offset)[0]
        if crc16_ccitt(packet[:crc_offset]) != expected_crc:
            return
        if index < self.total_packets and valid_len == self._expected_packet_size(index):
            chunk = packet[7:crc_offset]
            existing = self.packets.get(index)
            if existing is not None and existing != chunk:
                message = f"conflicting duplicate packet rejected index={index}"
                self.persist_checkpoint(reason=message)
                self.emit("packet_conflict", message, error_phase="reassembly", error=message)
                return
            is_new = existing is None
            if is_new:
                self.packets[index] = chunk
            self.last_packet_s = time.monotonic()
            self.last_packet_wall_s = time.time()
            if is_new:
                self.persist_checkpoint(packet_index=index, reason="data packet accepted")
                self.emit("progress")
            if len(self.packets) % 50 == 0 or len(self.packets) == self.total_packets:
                print(f"progress: {len(self.packets)}/{self.total_packets}")

    def request_retries(self, ser: serial.Serial) -> bool:
        if self.transfer_id is None or self.total_packets == 0:
            return False
        missing = self.missing_packets()
        if not missing:
            return False
        start = missing[0]
        span = [idx for idx in missing if idx < start + 8 * 36]
        bitmap = bytearray(36)
        for idx in span:
            rel = idx - start
            bitmap[rel // 8] |= 1 << (rel % 8)
        while bitmap and bitmap[-1] == 0:
            bitmap.pop()
        request = bytearray()
        request += MAGIC
        request += bytes([TYPE_RETRY_REQUEST, self.transfer_id])
        request += struct.pack("<H", start)
        request += bytes([len(bitmap)])
        request += bitmap
        ser.write(bytes(request))
        self.retry_rounds += 1
        message = f"retry: start={start} count={len(span)} bitmap_bytes={len(bitmap)}"
        print(message)
        self.emit(
            "retry_requested",
            message,
            retry_start=start,
            retry_count=len(span),
            retry_bitmap_bytes=len(bitmap),
        )
        return True

    def missing_packets(self) -> list[int]:
        return [idx for idx in range(self.total_packets) if idx not in self.packets]

    def reconstruct(self) -> bytes:
        chunks = [self.packets[idx] for idx in range(self.total_packets)]
        return b"".join(chunks)[: self.total_bytes]

    def reconstruct_partial(self) -> bytes:
        """Preserve byte positions while zero-filling unavailable packets.

        The zero bytes are placeholders only. Consumers must use the emitted
        missing-packet map to mark affected samples as unknown; this blob is
        never eligible for whole-file CRC success.
        """

        chunks = [
            self.packets.get(index, b"\x00" * self._expected_packet_size(index))
            for index in range(self.total_packets)
        ]
        return b"".join(chunks)[: self.total_bytes]

    def _expected_packet_size(self, index: int) -> int:
        start = index * self.packet_data_bytes
        return max(0, min(self.packet_data_bytes, self.total_bytes - start))

    def reset_transfer(self, *, clear_checkpoint: bool = True) -> None:
        if clear_checkpoint:
            self.clear_checkpoint()
        self.transfer_id = None
        self.product_id = 0
        self.total_bytes = 0
        self.total_packets = 0
        self.file_crc = 0
        self.packet_data_bytes = DATA_BYTES
        self.packets = {}
        self.end_seen = False
        self.last_end_s = 0.0
        self.next_retry_request_s = 0.0
        self.last_packet_s = 0.0
        self.retry_rounds = 0
        self.transfer_started_s = 0.0
        self.transfer_started_wall_s = 0.0
        self.last_packet_wall_s = 0.0
        self.next_retry_request_wall_s = 0.0

    def finalize_to_dir(self) -> None:
        assert self.output_dir is not None
        blob = self.reconstruct()
        actual_crc = crc16_ccitt(blob)
        crc_ok = actual_crc == self.file_crc
        self.received_count += 1
        self.output_dir.mkdir(parents=True, exist_ok=True)

        suffix = "" if crc_ok else ".badcrc"
        base = f"Dp_{time.strftime('%Y%m%d_%H%M%S')}"
        target = self.output_dir / f"{base}{self.ext}{suffix}"
        dupe = 1
        while target.exists():
            target = self.output_dir / f"{base}_{dupe:03d}{self.ext}{suffix}"
            dupe += 1
        self._atomic_write(target, blob)
        status = "ok" if crc_ok else f"CRC MISMATCH actual=0x{actual_crc:04x} expected=0x{self.file_crc:04x}"
        message = (
            f"saved: {target.name} product={self.product_id} transfer={self.transfer_id} "
            f"bytes={len(blob)} packets={self.total_packets} [{status}]"
        )
        print(message)
        self.emit("crc_checked", status, actual_crc=actual_crc, crc_ok=crc_ok)
        self.emit(
            "transfer_saved",
            message,
            actual_crc=actual_crc,
            crc_ok=crc_ok,
            output_path=target,
        )

    def finalize_partial_to_dir(
        self,
        timeout_reason: str,
        *,
        completion_reason: str = "timeout",
    ) -> None:
        """Save an incomplete positional blob for format-aware recovery."""

        assert self.output_dir is not None
        missing = tuple(self.missing_packets())
        blob = self.reconstruct_partial()
        self.received_count += 1
        self.output_dir.mkdir(parents=True, exist_ok=True)
        base = f"Dp_{time.strftime('%Y%m%d_%H%M%S')}"
        target = self.output_dir / f"{base}{self.ext}.partial"
        dupe = 1
        while target.exists():
            target = self.output_dir / f"{base}_{dupe:03d}{self.ext}.partial"
            dupe += 1
        target.write_bytes(blob)
        missing_map = pathlib.Path(f"{target}.missing.json")
        missing_payload = {
            "schema_version": 1,
            "product_id": self.product_id,
            "transfer_id": self.transfer_id,
            "total_bytes": self.total_bytes,
            "total_packets": self.total_packets,
            "received_packets": len(self.packets),
            "missing_packet_indices": list(missing),
            "packet_data_bytes": self.packet_data_bytes,
            "expected_crc": self.file_crc,
            "failure_reason": timeout_reason,
            "completion_reason": completion_reason,
        }
        self._atomic_write(
            missing_map,
            (json.dumps(missing_payload, indent=2, sort_keys=True) + "\n").encode("utf-8"),
        )
        message = (
            f"partial: {target.name} product={self.product_id} transfer={self.transfer_id} "
            f"received={len(self.packets)}/{self.total_packets} missing={len(missing)} "
            f"reason={timeout_reason}"
        )
        print(message)
        self.emit(
            "partial_saved",
            message,
            crc_ok=False,
            output_path=target,
            partial=True,
            missing_packet_indices=missing,
            timeout_reason=timeout_reason,
            completion_reason=completion_reason,
            missing_map_path=missing_map,
        )

    def finalize_operator_cancel_if_requested(self) -> bool:
        """Finalize an active transfer locally without stopping the serial worker."""

        if not self.consume_cancel_requested():
            return False
        if self.transfer_id is None or self.total_packets <= 0:
            return False
        if self.complete:
            self.finalize_to_dir()
            self.reset_transfer(clear_checkpoint=not self.retain_checkpoint_after_complete)
            return True

        canceled_transfer_id = self.transfer_id
        self.finalize_partial_to_dir(
            "stopped by operator on ground; satellite transmission was not interrupted",
            completion_reason="operator_cancelled",
        )
        self.reset_transfer()
        self.ignored_transfer_id = canceled_transfer_id
        return True

    def run_directory(self, idle_timeout_s: float = 0.0) -> int:
        assert self.output_dir is not None
        self.output_dir.mkdir(parents=True, exist_ok=True)
        print(f"listening on {self.port}: saving completed transfers to {self.output_dir}/ (Ctrl-C to stop)")
        last_activity = time.monotonic()
        self.load_checkpoint()
        if self.complete:
            self.finalize_to_dir()
            self.reset_transfer(clear_checkpoint=not self.retain_checkpoint_after_complete)
            last_activity = time.monotonic()
        recovery_deadline_s: float | None = None
        try:
            while not self.stop_requested():
                opened = False
                try:
                    with self.serial_factory(self.port, self.baud, timeout=0.2) as ser:
                        opened = True
                        self.emit("ready", f"listening on {self.port}")
                        while not self.stop_requested():
                            if self.finalize_operator_cancel_if_requested():
                                last_activity = time.monotonic()
                                continue
                            packet = self.read_packet(ser)
                            force_retry = False
                            if packet:
                                recovery_deadline_s = None
                                self.handle_packet(packet, ser)
                                force_retry = packet[:2] == MAGIC and packet[2] == TYPE_END
                                last_activity = time.monotonic()
                            self.finalize_preview_if_due()
                            if self.complete:
                                self.finalize_to_dir()
                                self.reset_transfer(
                                    clear_checkpoint=not self.retain_checkpoint_after_complete
                                )
                                last_activity = time.monotonic()
                            elif self.finalize_operator_cancel_if_requested():
                                last_activity = time.monotonic()
                            elif self.save_partial_on_timeout and (reason := self.transfer_expiry_reason()) is not None:
                                self.finalize_partial_to_dir(reason, completion_reason="timeout")
                                self.reset_transfer()
                                last_activity = time.monotonic()
                            elif (
                                idle_timeout_s > 0
                                and self.transfer_id is None
                                and (time.monotonic() - last_activity) > idle_timeout_s
                            ):
                                print("idle timeout reached; exiting")
                                self.emit("idle_timeout", "idle timeout reached; exiting")
                                return 0
                            else:
                                self.request_retries_if_due(ser, force=force_retry)
                except serial.SerialException as exc:
                    phase = "io" if opened else "open"
                    reason = f"serial {phase} failure on {self.port}: {exc}"
                    self.persist_checkpoint(reason=reason)
                    self.emit("serial_error", str(exc), error_phase=phase, error=str(exc))
                    if self.stop_requested():
                        return 0
                    if self.port_resolver is None or self.reconnect_timeout_s <= 0:
                        raise
                    if recovery_deadline_s is None:
                        recovery_deadline_s = time.monotonic() + self.reconnect_timeout_s
                        if (
                            self.transfer_timeout_s is not None
                            and (self.last_packet_s > 0.0 or self.transfer_started_s > 0.0)
                        ):
                            progress_reference = self.last_packet_s or self.transfer_started_s
                            recovery_deadline_s = min(
                                recovery_deadline_s,
                                progress_reference + self.transfer_timeout_s,
                            )
                        if self.absolute_transfer_timeout_s is not None and self.transfer_started_s > 0.0:
                            recovery_deadline_s = min(
                                recovery_deadline_s,
                                self.transfer_started_s + self.absolute_transfer_timeout_s,
                            )
                    self.emit("recovering", reason, error_phase=phase, error=str(exc))
                    resolved: str | None = None
                    while not self.stop_requested() and time.monotonic() < recovery_deadline_s:
                        resolved = self.port_resolver()
                        if resolved:
                            break
                        time.sleep(self.reconnect_interval_s)
                    if self.stop_requested():
                        self.persist_checkpoint(reason="receiver stopped during serial recovery")
                        return 0
                    if resolved:
                        self.port = resolved
                        time.sleep(self.reconnect_interval_s)
                        continue

                    expired_reason = (
                        f"serial recovery expired after {self.reconnect_timeout_s:g} seconds: {exc}"
                    )
                    if (
                        self.save_partial_on_disconnect
                        and self.transfer_id is not None
                        and self.total_packets > 0
                    ):
                        self.finalize_partial_to_dir(
                            expired_reason,
                            completion_reason="disconnect",
                        )
                        self.reset_transfer()
                        return 2
                    raise
        except KeyboardInterrupt:
            print("\nstopped by user")
            self.emit("stopped", "stopped by user")
        return 0

    def transfer_expiry_reason(self, now: float | None = None) -> str | None:
        if self.transfer_id is None or self.transfer_started_s <= 0.0:
            return None
        now = time.monotonic() if now is None else now
        if (
            self.absolute_transfer_timeout_s is not None
            and self.absolute_transfer_timeout_s > 0.0
            and now - self.transfer_started_s >= self.absolute_transfer_timeout_s
        ):
            return (
                "absolute transfer deadline reached after "
                f"{self.absolute_transfer_timeout_s:g} seconds"
            )
        progress_reference = self.last_packet_s or self.transfer_started_s
        if (
            self.transfer_timeout_s is not None
            and self.transfer_timeout_s > 0.0
            and now - progress_reference >= self.transfer_timeout_s
        ):
            return f"transfer stalled for {self.transfer_timeout_s:g} seconds without progress"
        return None


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Payload serial port, usually ground Teensy SerialUSB2")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("payload_blob.bin"),
        help="Single-file output path; used only when --output-dir is not given",
    )
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        default=None,
        help="Directory to save every completed transfer in continuous-listen mode",
    )
    parser.add_argument(
        "--ext",
        default=".bin",
        help="Filename extension label for --output-dir files, e.g. .fdp",
    )
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--transfer-timeout",
        type=float,
        default=None,
        help="Save/fail a live transfer after this many seconds without packet progress",
    )
    parser.add_argument(
        "--absolute-transfer-timeout",
        type=float,
        default=None,
        help="Save/fail a live transfer after this total elapsed time regardless of progress",
    )
    parser.add_argument(
        "--save-partial-on-timeout",
        action="store_true",
        help="In --output-dir mode, save positional .partial data and continue listening",
    )
    parser.add_argument(
        "--checkpoint-dir",
        type=pathlib.Path,
        default=None,
        help="Durable active-transfer checkpoint directory",
    )
    parser.add_argument(
        "--checkpoint-max-age",
        type=float,
        default=DEFAULT_CHECKPOINT_MAX_AGE_S,
        help="Reject checkpoints older than this many seconds; 0 disables age rejection",
    )
    parser.add_argument(
        "--idle-timeout",
        type=float,
        default=0.0,
        help="--output-dir mode: exit after this many idle seconds; 0 runs until Ctrl-C",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Print raw-stream heartbeat diagnostics",
    )
    args = parser.parse_args(argv)
    receiver = PayloadReceiver(
        args.port,
        args.baud,
        args.output,
        args.timeout,
        output_dir=args.output_dir,
        ext=args.ext,
        debug=args.debug,
        checkpoint_dir=args.checkpoint_dir,
        checkpoint_max_age_s=args.checkpoint_max_age,
        transfer_timeout_s=args.transfer_timeout,
        absolute_transfer_timeout_s=args.absolute_transfer_timeout,
        save_partial_on_timeout=args.save_partial_on_timeout,
    )
    if args.output_dir is not None:
        return receiver.run_directory(args.idle_timeout)
    return receiver.run()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
