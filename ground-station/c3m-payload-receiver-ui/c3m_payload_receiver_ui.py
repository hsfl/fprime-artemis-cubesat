#!/usr/bin/env python3
"""Local web UI for C3M channel-1 payload receive, proof, and review."""

from __future__ import annotations

import argparse
import copy
import dataclasses
import datetime as dt
import hashlib
import importlib.util
import json
import mimetypes
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import webbrowser
from collections import deque
from collections.abc import Callable
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from types import ModuleType
from typing import Any
from urllib.parse import unquote, urlparse

from serial.tools import list_ports


APP_DIR = Path(__file__).resolve().parent
STATIC_DIR = APP_DIR / "static"


def find_repo_root(start: Path) -> Path:
    for candidate in [start, *start.parents]:
        if (candidate / "ArtemisRpiTeensy_N2").is_dir() and (candidate / "ground-station").is_dir():
            return candidate
    raise RuntimeError(f"repository root not found from {start}")


REPO_ROOT = find_repo_root(APP_DIR)
DEFAULT_DATA_DIR = REPO_ROOT / "data"
PAYLOAD_RECEIVER_PATH = REPO_ROOT / "ArtemisRpiTeensy_N2" / "tools" / "payload_receiver.py"
LEPTON_VIEWER_PATH = REPO_ROOT / "ground-station" / "lepton-dp-viewer" / "lepton_dp_viewer.py"


def load_module(name: str, path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


payload_receiver = load_module("c3m_payload_receiver_engine", PAYLOAD_RECEIVER_PATH)
lepton_viewer = load_module("c3m_lepton_viewer", LEPTON_VIEWER_PATH)


def utc_iso(timestamp_s: float | None = None) -> str:
    timestamp_s = time.time() if timestamp_s is None else timestamp_s
    return dt.datetime.fromtimestamp(timestamp_s, dt.timezone.utc).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_relative_url(run_id: str, filename: str) -> str:
    return f"/files/{run_id}/{filename}"


def serial_port_rows() -> list[dict[str, str | bool | None]]:
    rows: list[dict[str, str | bool | None]] = []
    for item in list_ports.comports():
        device = item.device
        if not (
            device.startswith("/dev/cu.usbmodem")
            or device.startswith("/dev/tty.usbmodem")
            or device.startswith("/dev/ttyACM")
            or device.startswith("/dev/ttyUSB")
            or device.upper().startswith("COM")
        ):
            continue
        rows.append(
            {
                "device": device,
                "description": item.description or "USB serial",
                "serial_number": item.serial_number,
                "location": item.location,
                "likely_payload": False,
            }
        )
    rows.sort(key=lambda row: str(row["device"]))
    detected = detect_payload_port(rows)
    for row in rows:
        row["likely_payload"] = row["device"] == detected
    return rows


def detect_payload_port(rows: list[dict[str, str | bool | None]]) -> str | None:
    """Return the third ground Triple Serial port only when grouping is clear."""

    groups: dict[str, list[dict[str, str | bool | None]]] = {}
    ungrouped_triple: list[dict[str, str | bool | None]] = []
    for row in rows:
        serial_number = str(row.get("serial_number") or "")
        description = str(row.get("description") or "")
        if serial_number:
            key = f"serial:{serial_number}"
        elif "Triple Serial" in description:
            device = str(row["device"])
            mac_match = re.match(r"^(.*usbmodem\d+)(?:01|03|05)$", device)
            if mac_match:
                key = f"mac:{mac_match.group(1)}"
            else:
                ungrouped_triple.append(row)
                continue
        else:
            continue
        groups.setdefault(key, []).append(row)

    if len(ungrouped_triple) == 3:
        groups["triple-only"] = ungrouped_triple

    candidates: list[str] = []
    for group in groups.values():
        if len(group) < 3:
            continue
        ordered = sorted((str(row["device"]) for row in group), key=device_sort_key)
        candidates.append(ordered[-1])
    return candidates[0] if len(candidates) == 1 else None


def device_sort_key(device: str) -> tuple[str, int]:
    match = re.match(r"^(.*?)(\d+)$", device)
    return (match.group(1), int(match.group(2))) if match else (device, -1)


def build_channel1_stream(
    blob: bytes,
    *,
    product_id: int = 314549,
    transfer_id: int = 42,
    expected_crc: int | None = None,
) -> bytes:
    """Build a deterministic raw receiver stream for local UI replay."""

    data_bytes = payload_receiver.DATA_BYTES
    total_packets = (len(blob) + data_bytes - 1) // data_bytes
    file_crc = payload_receiver.crc16_ccitt(blob) if expected_crc is None else expected_crc

    header = bytearray(payload_receiver.MAGIC)
    header += bytes([payload_receiver.TYPE_HEADER, transfer_id])
    header += struct.pack("<I", product_id)
    header += struct.pack("<I", len(blob))
    header += struct.pack("<H", total_packets)
    header += bytes([data_bytes])
    header += struct.pack("<H", file_crc)

    packets = [bytes(header)]
    for index in range(total_packets):
        chunk = blob[index * data_bytes : (index + 1) * data_bytes]
        packet = bytearray(payload_receiver.MAGIC)
        packet += bytes([payload_receiver.TYPE_DATA, transfer_id])
        packet += struct.pack("<H", index)
        packet += bytes([len(chunk)])
        packet += chunk
        packet += struct.pack("<H", payload_receiver.crc16_ccitt(packet))
        packets.append(bytes(packet))

    end = bytearray(payload_receiver.MAGIC)
    end += bytes([payload_receiver.TYPE_END, transfer_id])
    end += struct.pack("<H", total_packets)
    end += struct.pack("<H", payload_receiver.crc16_ccitt(blob))
    packets.append(bytes(end))
    return b"".join(packets)


class ReplaySerial:
    """Small serial-compatible raw stream source for no-hardware validation."""

    def __init__(self, stream: bytes, delay_s: float = 0.0, chunk_size: int = 37) -> None:
        self.buffer = bytearray(stream)
        self.delay_s = delay_s
        self.chunk_size = chunk_size
        self.writes: list[bytes] = []

    def __enter__(self) -> ReplaySerial:
        return self

    def __exit__(self, *_: object) -> None:
        return None

    def read(self, size: int) -> bytes:
        if not self.buffer:
            if self.delay_s:
                time.sleep(min(self.delay_s, 0.05))
            return b""
        if self.delay_s:
            time.sleep(self.delay_s)
        count = min(size, self.chunk_size, len(self.buffer))
        result = bytes(self.buffer[:count])
        del self.buffer[:count]
        return result

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)


def default_current_state() -> dict[str, Any]:
    return {
        "status": "starting",
        "connected": False,
        "port": None,
        "message": "Starting payload receiver",
        "failure_reason": None,
        "product_id": None,
        "transfer_id": None,
        "total_bytes": 0,
        "received_bytes": 0,
        "total_packets": 0,
        "received_packets": 0,
        "missing_packets": 0,
        "retry_rounds": 0,
        "expected_crc": None,
        "actual_crc": None,
        "crc_ok": None,
        "started_at_s": None,
        "completed_at_s": None,
        "elapsed_seconds": 0.0,
        "estimated_remaining_seconds": None,
        "timing_band": "nominal",
        "run_id": None,
        "outputs": {},
        "decode": None,
    }


class ReceiverController:
    def __init__(
        self,
        data_dir: Path,
        *,
        baud: int = 115200,
        dictionary: Path | None = None,
        decode_fn: Callable[[Path, Path, Path | None], dict[str, Any]] | None = None,
    ) -> None:
        self.data_dir = data_dir.resolve()
        self.incoming_dir = self.data_dir / ".incoming"
        self.baud = baud
        self.dictionary = dictionary.resolve() if dictionary is not None else None
        self.decode_fn = decode_fn or self._decode_lepton
        self.lock = threading.RLock()
        self.worker_lock = threading.RLock()
        self.current = default_current_state()
        self.logs: deque[dict[str, Any]] = deque(maxlen=160)
        self.thread: threading.Thread | None = None
        self.stop_event = threading.Event()
        self.last_replay: tuple[bytes, float, int | None] | None = None
        self.worker_generation = 0
        self.transfer_sequence = 0
        self.finalize_lock = threading.Lock()
        self.data_dir.mkdir(parents=True, exist_ok=True)

    def _decode_lepton(self, fdp_path: Path, outdir: Path, dictionary: Path | None) -> dict[str, Any]:
        namespace = argparse.Namespace(
            bin_file=fdp_path,
            dictionary=dictionary,
            outdir=outdir,
            summary=False,
            no_png=False,
            no_show=True,
        )
        return lepton_viewer.decode_product(namespace)

    def _append_log(self, message: str, timestamp_s: float | None = None, level: str = "info") -> None:
        if not message:
            return
        self.logs.append(
            {
                "timestamp": utc_iso(timestamp_s),
                "message": message,
                "level": level,
            }
        )

    def set_port_required(self, message: str = "Choose the Channel 1 payload serial port to begin listening.") -> None:
        with self.lock:
            self.current = default_current_state()
            self.current.update(
                {
                    "status": "select_port",
                    "message": message,
                    "failure_reason": message,
                }
            )

    def connect(self, port: str) -> None:
        with self.lock:
            if self.current.get("port") == port and self.current.get("status") not in {
                "select_port",
                "disconnected",
                "failed",
            }:
                return
            self.last_replay = None
        self._start_worker(port=port, replay=None)

    def connect_replay(self, blob: bytes, *, delay_s: float = 0.0, expected_crc: int | None = None) -> None:
        self.last_replay = (bytes(blob), delay_s, expected_crc)
        self._start_worker(port="Local channel-1 replay", replay=self.last_replay)

    def reconnect(self) -> None:
        with self.lock:
            port = self.current.get("port")
            replay = self.last_replay
        if replay is not None:
            self.connect_replay(replay[0], delay_s=replay[1], expected_crc=replay[2])
        elif isinstance(port, str) and port:
            self._start_worker(port=port, replay=None)
        else:
            self.set_port_required()

    def _start_worker(self, port: str, replay: tuple[bytes, float, int | None] | None) -> None:
        with self.worker_lock:
            self.stop()
            self.stop_event = threading.Event()
            with self.lock:
                self.worker_generation += 1
                generation = self.worker_generation
                self.current = default_current_state()
                self.current.update(
                    {
                        "status": "starting",
                        "port": port,
                        "message": f"Opening {port}",
                    }
                )
                self.logs.clear()
                self._append_log(f"Opening payload receiver on {port}")
            self.thread = threading.Thread(
                target=self._run_receiver,
                args=(port, replay, self.stop_event, generation),
                name="c3m-payload-receiver",
                daemon=True,
            )
            self.thread.start()

    def stop(self) -> None:
        with self.worker_lock:
            self.stop_event.set()
            thread = self.thread
            if thread is not None and thread.is_alive() and thread is not threading.current_thread():
                thread.join(timeout=1.0)
                if thread.is_alive():
                    message = "Payload receiver did not stop; channel-1 serial ownership is uncertain"
                    with self.lock:
                        self.current.update(
                            {
                                "status": "disconnected",
                                "connected": False,
                                "message": "Payload receiver could not reset",
                                "failure_reason": message,
                            }
                        )
                        self._append_log(message, level="error")
                    raise RuntimeError(message)
            self.thread = None

    def _run_receiver(
        self,
        port: str,
        replay: tuple[bytes, float, int | None] | None,
        stop_event: threading.Event,
        generation: int,
    ) -> None:
        serial_factory: Callable[..., object] | None = None
        idle_timeout = 0.0
        if replay is not None:
            raw = build_channel1_stream(replay[0], expected_crc=replay[2])
            replay_serial = ReplaySerial(raw, delay_s=replay[1])
            serial_factory = lambda *_args, **_kwargs: replay_serial
            idle_timeout = 0.5

        worker_incoming_dir = self.incoming_dir / f"worker_{generation}"
        receiver = payload_receiver.PayloadReceiver(
            port,
            self.baud,
            worker_incoming_dir / "payload.fdp",
            120.0,
            output_dir=worker_incoming_dir,
            ext=".fdp",
            debug=False,
            on_event=lambda event: self.on_receiver_event(event, generation),
            serial_factory=serial_factory,
            stop_requested=stop_event.is_set,
        )
        try:
            receiver.run_directory(idle_timeout_s=idle_timeout)
        except Exception as exc:
            if stop_event.is_set():
                return
            with self.lock:
                if generation != self.worker_generation:
                    return
                self.current.update(
                    {
                        "status": "disconnected",
                        "connected": False,
                        "message": "Payload receiver disconnected",
                        "failure_reason": str(exc),
                    }
                )
                self._append_log(str(exc), level="error")

    def on_receiver_event(self, event: Any, generation: int | None = None) -> None:
        event_data = dataclasses.asdict(event)
        finalize = event.kind == "transfer_saved"
        transfer_sequence: int | None = None
        with self.lock:
            if generation is not None and generation != self.worker_generation:
                return
            self.current["port"] = event.port
            if event.kind in {
                "transfer_started",
                "progress",
                "retry_requested",
                "crc_checked",
                "transfer_saved",
                "incomplete",
            }:
                self.current.update(
                    {
                        "product_id": event.product_id,
                        "transfer_id": event.transfer_id,
                        "total_bytes": event.total_bytes,
                        "received_bytes": event.received_bytes,
                        "total_packets": event.total_packets,
                        "received_packets": event.received_packets,
                        "missing_packets": event.missing_packets,
                        "retry_rounds": event.retry_rounds,
                        "expected_crc": event.expected_crc,
                    }
                )

            if event.kind == "ready":
                self.current.update(
                    {
                        "status": "ready",
                        "connected": True,
                        "message": "Ready — awaiting downlink",
                        "failure_reason": None,
                    }
                )
            elif event.kind == "transfer_started":
                self.transfer_sequence += 1
                self.current.update(
                    {
                        "status": "receiving",
                        "connected": True,
                        "message": "Receiving payload",
                        "failure_reason": None,
                        "started_at_s": event.timestamp_s,
                        "completed_at_s": None,
                        "elapsed_seconds": 0.0,
                        "estimated_remaining_seconds": None,
                        "actual_crc": None,
                        "crc_ok": None,
                        "run_id": None,
                        "outputs": {},
                        "decode": None,
                    }
                )
                self._append_log(event.message, event.timestamp_s)
            elif event.kind == "progress":
                self.current.update({"status": "receiving", "message": "Receiving payload"})
                if event.received_packets % 50 == 0 or event.received_packets == event.total_packets:
                    self._append_log(
                        f"Progress: {event.received_packets}/{event.total_packets} packets",
                        event.timestamp_s,
                    )
            elif event.kind == "retry_requested":
                self.current.update(
                    {
                        "status": "retrying",
                        "message": "Retrying missing packets",
                    }
                )
                self._append_log(event.message, event.timestamp_s, level="warning")
            elif event.kind == "crc_checked":
                self.current.update(
                    {
                        # The finalizer owns the terminal state after the blob
                        # is safely moved and its run manifest is written.
                        "status": "verifying",
                        "message": "CRC passed" if event.crc_ok else "CRC failed",
                        "actual_crc": event.actual_crc,
                        "crc_ok": event.crc_ok,
                        "failure_reason": None if event.crc_ok else event.message,
                    }
                )
                self._append_log(event.message, event.timestamp_s, level="info" if event.crc_ok else "error")
            elif event.kind == "incomplete":
                self.current.update(
                    {
                        "status": "failed",
                        "message": "Downlink incomplete",
                        "failure_reason": event.message,
                    }
                )
                self._append_log(event.message, event.timestamp_s, level="error")
            elif event.kind == "serial_error":
                self.current.update(
                    {
                        "status": "disconnected",
                        "connected": False,
                        "message": "Payload receiver disconnected",
                        "failure_reason": event.error or event.message,
                    }
                )
                self._append_log(event.error or event.message, event.timestamp_s, level="error")
            elif event.kind == "idle_timeout":
                self.current["connected"] = False
                if self.current["status"] not in {"complete", "failed"}:
                    self.current.update(
                        {
                            "status": "disconnected",
                            "message": "Local replay complete",
                        }
                    )

            if finalize:
                transfer_sequence = self.transfer_sequence
                event_data["started_at_s"] = self.current.get("started_at_s") or event.timestamp_s

        if finalize:
            thread = threading.Thread(
                target=self._finalize_transfer,
                args=(event_data, generation, transfer_sequence),
                name=f"c3m-payload-finalize-{event.transfer_id}",
                daemon=True,
            )
            thread.start()

    def _next_run_dir(self, timestamp_s: float, transfer_id: int | None) -> Path:
        stamp = dt.datetime.fromtimestamp(timestamp_s, dt.timezone.utc).strftime("%Y%m%d_%H%M%S")
        base = f"c3m_{stamp}_transfer_{transfer_id if transfer_id is not None else 'unknown'}"
        candidate = self.data_dir / base
        suffix = 1
        while True:
            try:
                candidate.mkdir(parents=True)
                return candidate
            except FileExistsError:
                candidate = self.data_dir / f"{base}_{suffix:03d}"
                suffix += 1

    def _is_current_transfer(self, generation: int | None, transfer_sequence: int | None) -> bool:
        return (
            (generation is None or generation == self.worker_generation)
            and transfer_sequence == self.transfer_sequence
        )

    def _finalize_transfer(
        self,
        event: dict[str, Any],
        generation: int | None,
        transfer_sequence: int | None,
    ) -> None:
        # Decode can take seconds. Serialize it off the receiver thread so the
        # next channel-1 header can be accepted immediately without allowing an
        # older decode to overwrite a newer transfer's live state.
        try:
            with self.finalize_lock:
                self._finalize_transfer_locked(event, generation, transfer_sequence)
        except Exception as exc:
            message = f"Could not finalize payload: {exc}"
            with self.lock:
                if self._is_current_transfer(generation, transfer_sequence):
                    self.current.update(
                        {
                            "status": "failed",
                            "message": "Payload finalization failed",
                            "failure_reason": message,
                        }
                    )
                    self._append_log(message, level="error")

    def _finalize_transfer_locked(
        self,
        event: dict[str, Any],
        generation: int | None,
        transfer_sequence: int | None,
    ) -> None:
        started_at = event.get("started_at_s") or event["timestamp_s"]
        source = Path(str(event["output_path"])).resolve()
        crc_ok = event.get("crc_ok") is True
        run_dir = self._next_run_dir(float(event["timestamp_s"]), event.get("transfer_id"))
        target = run_dir / ("payload.fdp" if crc_ok else "payload.fdp.badcrc")
        shutil.move(str(source), str(target))
        digest = sha256_file(target)
        with self.lock:
            if self._is_current_transfer(generation, transfer_sequence):
                self.current.update(
                    {
                        "status": "decoding" if crc_ok else "verifying",
                        "message": "Decoding thermal product" if crc_ok else "CRC failed",
                        "run_id": run_dir.name,
                    }
                )

        summary: dict[str, Any] | None = None
        failure_reason: str | None = None
        result = "crc_failed"
        if crc_ok:
            try:
                summary = self.decode_fn(target, run_dir, self.dictionary)
                result = "complete"
            except SystemExit as exc:
                failure_reason = f"decoder exited with status {exc.code}"
                result = "decode_failed"
            except Exception as exc:
                failure_reason = str(exc)
                result = "decode_failed"
        else:
            failure_reason = (
                f"CRC mismatch: actual=0x{int(event.get('actual_crc') or 0):04x} "
                f"expected=0x{int(event.get('expected_crc') or 0):04x}"
            )

        completed_at = time.time()
        output_paths: dict[str, str] = {"fdp": target.name}
        if summary is not None:
            for key in ("json", "csv", "png"):
                value = summary.get(key)
                if value:
                    output_paths[key] = Path(str(value)).name

        run_info: dict[str, Any] = {
            "run_id": run_dir.name,
            "result": result,
            "failure_reason": failure_reason,
            "started_at": utc_iso(float(started_at)),
            "completed_at": utc_iso(completed_at),
            "elapsed_seconds": round(completed_at - float(started_at), 3),
            "serial_port": event.get("port"),
            "baud": self.baud,
            "product_id": event.get("product_id"),
            "transfer_id": event.get("transfer_id"),
            "total_bytes": event.get("total_bytes"),
            "received_bytes": event.get("received_bytes"),
            "total_packets": event.get("total_packets"),
            "received_packets": event.get("received_packets"),
            "missing_packets": event.get("missing_packets"),
            "retry_rounds": event.get("retry_rounds"),
            "expected_crc": event.get("expected_crc"),
            "actual_crc": event.get("actual_crc"),
            "crc_ok": crc_ok,
            "sha256": digest,
            "outputs": output_paths,
            "decode": summary,
        }
        temporary = run_dir / "run.json.tmp"
        temporary.write_text(json.dumps(run_info, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        temporary.replace(run_dir / "run.json")

        output_urls = {key: safe_relative_url(run_dir.name, value) for key, value in output_paths.items()}
        with self.lock:
            if not self._is_current_transfer(generation, transfer_sequence):
                return
            if result == "complete":
                message = "Payload complete"
            elif result == "crc_failed":
                message = "CRC failed"
            else:
                message = "Payload received — decode failed"
            self.current.update(
                {
                    "status": "complete" if result == "complete" else "failed",
                    "message": message,
                    "failure_reason": failure_reason,
                    "completed_at_s": completed_at,
                    "elapsed_seconds": run_info["elapsed_seconds"],
                    "run_id": run_dir.name,
                    "outputs": output_urls,
                    "decode": summary,
                }
            )
            if result == "complete":
                self._append_log("Decode complete", completed_at)
            elif failure_reason:
                self._append_log(failure_reason, completed_at, level="error")

    def history(self) -> list[dict[str, Any]]:
        runs: list[dict[str, Any]] = []
        for path in self.data_dir.glob("c3m_*/run.json"):
            try:
                payload = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                continue
            run_id = path.parent.name
            outputs = payload.get("outputs") or {}
            payload["run_id"] = run_id
            payload["output_urls"] = {
                key: safe_relative_url(run_id, str(filename))
                for key, filename in outputs.items()
            }
            runs.append(payload)
        runs.sort(key=lambda item: str(item.get("completed_at") or item.get("started_at") or ""), reverse=True)
        return runs

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            current = copy.deepcopy(self.current)
            logs = list(self.logs)
        started_at = current.get("started_at_s")
        completed_at = current.get("completed_at_s")
        if isinstance(started_at, (int, float)):
            end = float(completed_at) if isinstance(completed_at, (int, float)) else time.time()
            elapsed = max(0.0, end - float(started_at))
            current["elapsed_seconds"] = round(elapsed, 1)
            received = int(current.get("received_packets") or 0)
            total = int(current.get("total_packets") or 0)
            if received >= 50 and total > received and elapsed >= 2.0:
                rate = received / elapsed
                current["estimated_remaining_seconds"] = round((total - received) / rate, 1) if rate > 0 else None
            else:
                current["estimated_remaining_seconds"] = None
            if elapsed > 120:
                current["timing_band"] = "delayed"
            elif elapsed > 60:
                current["timing_band"] = "degraded"
            else:
                current["timing_band"] = "nominal"
        current["progress_fraction"] = (
            min(1.0, int(current.get("received_packets") or 0) / int(current.get("total_packets") or 1))
            if int(current.get("total_packets") or 0) > 0
            else 0.0
        )
        return {
            "current": current,
            "logs": logs,
            "history": self.history(),
            "ports": serial_port_rows(),
            "server_time": utc_iso(),
        }


def resolve_under(root: Path, relative: str) -> Path:
    root = root.resolve()
    target = (root / relative).resolve()
    try:
        target.relative_to(root)
    except ValueError as exc:
        raise ValueError("path is outside the data directory") from exc
    return target


def open_folder(path: Path) -> None:
    if sys.platform == "darwin":
        subprocess.Popen(["open", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return
    if os.name == "nt":
        os.startfile(path)  # type: ignore[attr-defined]
        return
    opener = shutil.which("wslview") or shutil.which("xdg-open")
    if opener is None:
        raise RuntimeError("no supported folder opener found")
    subprocess.Popen([opener, str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


class ReceiverServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], controller: ReceiverController, quiet: bool = False):
        super().__init__(address, ReceiverHandler)
        self.controller = controller
        self.quiet = quiet


class ReceiverHandler(BaseHTTPRequestHandler):
    server: ReceiverServer

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        try:
            if parsed.path == "/":
                self.send_path(STATIC_DIR / "index.html")
            elif parsed.path == "/static/styles.css":
                self.send_path(STATIC_DIR / "styles.css")
            elif parsed.path == "/static/app.js":
                self.send_path(STATIC_DIR / "app.js")
            elif parsed.path == "/api/state":
                self.send_json(self.server.controller.snapshot())
            elif parsed.path.startswith("/files/"):
                relative = unquote(parsed.path.removeprefix("/files/"))
                self.send_path(resolve_under(self.server.controller.data_dir, relative))
            else:
                self.send_error(404)
        except (OSError, ValueError) as exc:
            self.send_json({"error": str(exc)}, status=404)
        except Exception as exc:
            self.send_json({"error": str(exc)}, status=500)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        try:
            self.require_same_origin_json()
            payload = self.read_json()
            if parsed.path == "/api/connect":
                port = str(payload.get("port") or "")
                allowed = {str(row["device"]) for row in serial_port_rows()}
                if port not in allowed:
                    raise ValueError("select an available payload serial port")
                self.server.controller.connect(port)
                self.send_json({"ok": True})
            elif parsed.path == "/api/reconnect":
                self.server.controller.reconnect()
                self.send_json({"ok": True})
            elif parsed.path == "/api/open-folder":
                if self.client_address[0] not in {"127.0.0.1", "::1"}:
                    raise ValueError("open-folder is localhost only")
                run_id = str(payload.get("run_id") or "")
                target = resolve_under(self.server.controller.data_dir, run_id)
                if not target.is_dir():
                    raise ValueError("run directory not found")
                open_folder(target)
                self.send_json({"ok": True})
            else:
                self.send_error(404)
        except (OSError, ValueError) as exc:
            self.send_json({"error": str(exc)}, status=400)
        except Exception as exc:
            self.send_json({"error": str(exc)}, status=500)

    def require_same_origin_json(self) -> None:
        content_type = self.headers.get("Content-Type", "").split(";", 1)[0].strip().lower()
        if content_type != "application/json":
            raise ValueError("POST requests require application/json")
        origin = self.headers.get("Origin")
        if origin:
            parsed = urlparse(origin)
            if parsed.scheme not in {"http", "https"} or parsed.netloc != self.headers.get("Host", ""):
                raise ValueError("cross-origin requests are not allowed")

    def read_json(self) -> dict[str, Any]:
        length = min(int(self.headers.get("Content-Length", "0")), 65536)
        if length == 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def send_json(self, payload: dict[str, Any], status: int = 200) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_path(self, path: Path) -> None:
        if not path.is_file():
            raise FileNotFoundError(path)
        data = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt: str, *args: object) -> None:
        if not self.server.quiet:
            super().log_message(fmt, *args)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Ground Teensy channel-1 payload serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA_DIR)
    parser.add_argument("--dictionary", type=Path, help="F Prime topology dictionary used for Lepton decode")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument(
        "--allow-remote",
        action="store_true",
        help="Allow a non-loopback --host (unsafe on untrusted networks)",
    )
    parser.add_argument("--web-port", type=int, default=8064)
    parser.add_argument("--no-open", action="store_true", help="Do not open the web app automatically")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--replay-fdp", type=Path, help="Replay one local .fdp through the raw channel-1 receiver")
    parser.add_argument("--replay-delay-ms", type=float, default=2.0)
    parser.add_argument("--replay-bad-crc", action="store_true", help="Intentionally fail replay whole-file CRC")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.host not in {"127.0.0.1", "::1", "localhost"} and not args.allow_remote:
        parser.error("non-loopback --host requires --allow-remote")
    controller = ReceiverController(
        args.data_dir,
        baud=args.baud,
        dictionary=args.dictionary,
    )
    if args.replay_fdp is not None:
        blob = args.replay_fdp.read_bytes()
        expected_crc = (payload_receiver.crc16_ccitt(blob) ^ 0xFFFF) if args.replay_bad_crc else None
        controller.connect_replay(
            blob,
            delay_s=max(0.0, args.replay_delay_ms) / 1000.0,
            expected_crc=expected_crc,
        )
    elif args.port:
        controller.connect(args.port)
    else:
        detected = detect_payload_port(serial_port_rows())
        if detected is not None:
            controller.connect(detected)
        else:
            controller.set_port_required()

    server = ReceiverServer((args.host, args.web_port), controller, quiet=args.quiet)
    url = f"http://{args.host}:{server.server_port}/"
    print(f"C3M payload receiver web app: {url}")
    print(f"Payload history: {controller.data_dir}")
    if not args.no_open:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nC3M payload receiver stopped")
    finally:
        try:
            controller.stop()
        finally:
            server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
