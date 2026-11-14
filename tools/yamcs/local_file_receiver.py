#!/usr/bin/env python3
"""Receive stock Fw::FilePackets on disk and publish verified files to Yamcs.

Only the bridge calls the receiver. The CLI retries publication of an already
verified local file; it never issues a spacecraft command.
"""
from __future__ import annotations

import argparse
import errno
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import http.client
import json
import logging
import math
import mimetypes
import os
from pathlib import Path
import queue
import re
import secrets
import shlex
import shutil
import socket
import struct
import sys
import tempfile
import threading
import time
from urllib import parse

from fprime_gds.common.data_types.file_data import FilePacketType
from fprime_gds.common.decoders.file_decoder import FileDecoder
from fprime_gds.common.files.helpers import CFDPChecksum

from yamcs_client import LocalYamcs, loopback_url

LOG = logging.getLogger("n2-local-file-receiver")
FILE_APID = 3
DEFAULT_MAX_FILE_BYTES = 128 * 1024 * 1024
DEFAULT_MAX_SPOOL_BYTES = 2 * 1024 * 1024 * 1024
MAX_SPOOL_ENTRIES = 4096
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,179}\Z")


def publication_retry_command(receipt: Path | str, station: Path, url: str,
                              bucket: str = "fprimeFilesIn", *, windows: bool | None = None) -> str:
    """Render the existing disk-only CLI for POSIX shells or Windows PowerShell."""
    arguments = [sys.executable, str(Path(__file__).with_name("local_file_receiver.py").resolve()),
                 "--station", str(station), "--url", url, "--bucket", bucket, "--retry", str(receipt)]
    if windows is None:
        windows = os.name == "nt"
    if windows:
        return "& " + " ".join("'" + argument.replace("'", "''") + "'" for argument in arguments)
    return shlex.join(arguments)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def atomic_json(path: Path, value: dict) -> None:
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(value, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        sync_directory(path.parent)
    finally:
        temporary.unlink(missing_ok=True)


def sync_directory(directory: Path) -> None:
    """Persist rename entries where the platform permits directory fsync."""
    if os.name != "nt":
        descriptor = os.open(directory, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def read_receipt(path: Path) -> dict:
    if path.is_symlink() or path.stat().st_size > 16 * 1024:
        raise ValueError("invalid local receipt")
    result = json.loads(path.read_text())
    if not isinstance(result, dict):
        raise ValueError("local receipt must be an object")
    if not isinstance(result.get("destination"), str):
        raise ValueError("receipt omitted destination")
    valid_name(result["destination"])
    expected, received = result.get("expected_size"), result.get("received_size")
    if (type(expected) is not int or type(received) is not int or
            expected <= 0 or not 0 <= received <= expected):
        raise ValueError("receipt contains invalid file sizes")
    if result.get("state") in ("finalizing", "saved_locally", "available_in_yamcs"):
        digest = result.get("sha256")
        if received != expected or not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest):
            raise ValueError("verified receipt omitted size/hash")
    return result


def valid_name(value: str) -> str:
    if not NAME.fullmatch(value) or value.endswith((".part", ".receipt.json", ".publish.lock")):
        raise ValueError("destination must be a plain unique filename (letters, digits, _, - and .)")
    return value


def file_sha256(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(64 * 1024), b""):
            size += len(chunk)
            digest.update(chunk)
    return size, digest.hexdigest()


class FileLock:
    """OS-owned advisory lock; process exit releases it on macOS/Linux/Windows."""

    def __init__(self, path: Path):
        if path.is_symlink():
            raise ValueError("lock must not be a symlink")
        self.output = path.open("a+b")
        try:
            if path.stat().st_size == 0:
                self.output.write(b"\0")
                self.output.flush()
            self.output.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.output.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.output.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except Exception:
            self.output.close()
            raise RuntimeError("another process owns this receiver or publication") from None

    def close(self):
        if not self.output.closed:
            self.output.close()


def decode_file_packet(packet: bytes):
    """Use the stock decoder after strict wire-length checks it does not enforce."""
    if len(packet) < 13:
        raise ValueError("short file space packet")
    packet_id, control, data_length = struct.unpack_from(">HHH", packet)
    if packet_id >> 13 or packet_id & 0x1000 or packet_id & 0x7FF != FILE_APID:
        raise ValueError("invalid file space packet header")
    if control >> 14 != 3 or len(packet) != data_length + 7:
        raise ValueError("segmented or truncated file space packet")
    if struct.unpack_from(">H", packet, 6)[0] != FILE_APID:
        raise ValueError("unexpected file descriptor")
    inner = packet[8:]
    kind, _ = struct.unpack_from(">BI", inner)
    if kind == FilePacketType.START.value:
        if len(inner) < 11:
            raise ValueError("short START")
        source_size = inner[9]
        destination_offset = 10 + source_size
        if len(inner) <= destination_offset:
            raise ValueError("truncated START source")
        expected = destination_offset + 1 + inner[destination_offset]
    elif kind == FilePacketType.DATA.value:
        if len(inner) < 11:
            raise ValueError("short DATA")
        expected = 11 + struct.unpack_from(">H", inner, 9)[0]
    elif kind == FilePacketType.END.value:
        expected = 9
    elif kind == FilePacketType.CANCEL.value:
        expected = 5
    else:
        raise ValueError("unknown file packet type")
    if len(inner) != expected:
        raise ValueError("file packet length mismatch")
    return FileDecoder().decode_api(inner)[0]


@dataclass
class Incoming:
    receipt_path: Path
    part_path: Path
    final_path: Path
    output: object
    receipt: dict
    checksum: CFDPChecksum
    sha256: object
    sequence: int
    offset: int
    last_progress: float


class LocalFileReceiver:
    """Single sequential receiver; incomplete files remain unpublished .part files."""

    def __init__(self, directory: Path, *, on_saved=None, idle_timeout: float = 30,
                 max_file_bytes: int = DEFAULT_MAX_FILE_BYTES,
                 max_spool_bytes: int = DEFAULT_MAX_SPOOL_BYTES, clock=time.monotonic):
        if (not math.isfinite(idle_timeout) or idle_timeout <= 0 or
                max_file_bytes <= 0 or max_spool_bytes < max_file_bytes):
            raise ValueError("invalid receiver limits")
        self.directory = Path(directory).resolve()
        self.on_saved = on_saved
        self.idle_timeout = idle_timeout
        self.max_file_bytes = max_file_bytes
        self.max_spool_bytes = max_spool_bytes
        self.clock = clock
        self.active: Incoming | None = None
        self.lock = threading.RLock()
        self.status_path = self.directory / "receiver-status.json"
        self.stopped = False
        self.bridge_metrics = {}
        self.file_error: str | None = None
        self.process_lock: FileLock | None = None
        self.quarantined_receipts = 0
        self._inventory_time = float("-inf")
        self._inventory = {"free_spool_bytes": 0, "saved_files": 0, "pending_publications": 0}
        try:
            self.directory.mkdir(parents=True, exist_ok=True)
            self.process_lock = FileLock(self.directory / ".receiver.lock")
            # A bad historical receipt must not prevent telemetry startup.
            for index, path in enumerate(self.directory.glob("*.receipt.json")):
                if index >= MAX_SPOOL_ENTRIES:
                    raise OSError(errno.ENOSPC, "spool needs operator cleanup")
                try:
                    receipt = read_receipt(path)
                    if receipt.get("state") not in ("receiving", "finalizing", "saved_locally",
                                                     "available_in_yamcs", "failed"):
                        raise ValueError("unknown receipt state")
                    if receipt.get("state") == "receiving":
                        receipt.update(state="failed", error="receiver restarted during reception", updated_at=utc_now())
                        atomic_json(path, receipt)
                    elif receipt.get("state") == "finalizing":
                        self._recover_finalizing(path, receipt)
                except FileNotFoundError:
                    continue
                except (ValueError, KeyError, TypeError) as error:
                    quarantine = path.with_name(path.name + ".quarantined-" + secrets.token_hex(4))
                    path.rename(quarantine)
                    sync_directory(self.directory)
                    self.quarantined_receipts += 1
                    LOG.warning("Quarantined malformed receipt %s: %s", path.name, error)
            self.write_status()
        except OSError as error:
            self._storage_fault(error)
        except Exception:
            if self.process_lock:
                self.process_lock.close()
            raise

    @property
    def file_admission_ready(self) -> bool:
        return not self.stopped and self.file_error is None

    def _storage_fault(self, error: Exception) -> None:
        """Fail file admission while leaving the stock TCP/telemetry path alive."""
        if self.file_error is None:
            self.file_error = str(error)
            LOG.error("File spool unavailable; telemetry continues: %s", error)
        if self.active is not None:
            active, self.active = self.active, None
            try:
                active.output.close()
            except OSError:
                pass
            # Preserve a verified finalizing marker for restart recovery. An
            # interrupted receiving receipt will be failed on the next startup.
            if active.receipt.get("state") != "finalizing":
                active.receipt.update(state="failed", error=self.file_error, received_size=active.offset,
                                      updated_at=utc_now())
                try:
                    atomic_json(active.receipt_path, active.receipt)
                except OSError:
                    pass

    def _refresh_inventory(self) -> None:
        if self.clock() - self._inventory_time < 5:
            return
        total = saved = pending = quarantined = 0
        for index, path in enumerate(self.directory.iterdir()):
            if index >= MAX_SPOOL_ENTRIES:
                raise OSError(errno.ENOSPC, "spool needs operator cleanup")
            if path.is_symlink():
                raise OSError("spool contains a symlink; inspect before restarting")
            try:
                if path.is_file():
                    total += path.stat().st_size
                if ".receipt.json.quarantined-" in path.name:
                    quarantined += 1
                elif path.name.endswith(".receipt.json"):
                    try:
                        state = read_receipt(path).get("state")
                    except (ValueError, KeyError, TypeError):
                        continue  # Startup quarantines; do not block telemetry.
                    saved += state in ("saved_locally", "available_in_yamcs")
                    pending += state == "saved_locally"
            except FileNotFoundError:
                continue
        free = min(shutil.disk_usage(self.directory).free, self.max_spool_bytes - total)
        self._inventory = {"free_spool_bytes": max(0, free), "saved_files": saved,
                           "pending_publications": pending}
        self.quarantined_receipts = quarantined
        self._inventory_time = self.clock()

    def _recover_finalizing(self, path: Path, receipt: dict) -> None:
        """Recover only bytes proven complete by the pre-rename durable marker."""
        try:
            destination = valid_name(receipt["destination"])
            if path.name != destination + ".receipt.json":
                raise ValueError("finalization receipt name mismatch")
            final = self.directory / destination
            part = self.directory / (destination + ".part")
            candidate = final if final.exists() else part
            if final.is_symlink() or part.is_symlink():
                raise ValueError("finalization path must not be a symlink")
            size, digest = file_sha256(candidate)
            if (not 0 < size <= self.max_file_bytes or size != receipt["expected_size"] or
                    size != receipt["received_size"] or digest != receipt["sha256"]):
                raise ValueError("finalization bytes do not match verified marker")
            if candidate == part:
                os.link(part, final)
            if part.exists():
                # Both names can survive a crash between link and unlink.
                if file_sha256(part) != (size, digest):
                    raise ValueError("finalization partial differs from verified file")
                part.unlink()
            sync_directory(self.directory)
            receipt.update(state="saved_locally", recovered_at=utc_now(), updated_at=utc_now())
        except (OSError, ValueError, KeyError) as error:
            receipt.update(state="failed", error=f"interrupted finalization: {error}", updated_at=utc_now())
        atomic_json(path, receipt)

    def status(self) -> dict:
        return {"pid": os.getpid(), "heartbeat_unix": time.time(), "updated_at": utc_now(),
                "state": "stopped" if self.stopped else "receiving" if self.active else "ready",
                "destination": self.active.receipt["destination"] if self.active else None,
                "file_admission_ready": self.file_admission_ready, "file_error": self.file_error,
                "quarantined_receipts": self.quarantined_receipts, **self._inventory,
                "bridge": dict(self.bridge_metrics)}

    def write_status(self) -> None:
        with self.lock:
            try:
                self._refresh_inventory()
                atomic_json(self.status_path, self.status())
            except OSError as error:
                self._storage_fault(error)

    def _spool_capacity(self, size: int) -> None:
        total = 0
        entries = 0
        for entry in self.directory.iterdir():
            entries += 1
            if entries > MAX_SPOOL_ENTRIES or entry.is_symlink():
                raise OSError(errno.ENOSPC, "spool needs operator cleanup")
            try:
                if entry.is_file():
                    total += entry.stat().st_size
            except FileNotFoundError:
                continue  # An uploader may just have replaced its temporary receipt.
        if total + size + 16 * 1024 > self.max_spool_bytes or shutil.disk_usage(self.directory).free < size + 16 * 1024:
            raise OSError(errno.ENOSPC, "spool quota exceeded; retain files and free space before retry")

    def receive(self, packet: bytes) -> bool:
        with self.lock:
            if not self.file_admission_ready:
                return False
            try:
                data = decode_file_packet(packet)
                if data.packetType == FilePacketType.START:
                    self._start(data)
                elif self.active is None:
                    return False  # Mid-stream attachment cannot create a file.
                elif data.seqID != self.active.sequence:
                    raise ValueError("file packet sequence gap, duplicate or reorder")
                elif data.packetType == FilePacketType.DATA:
                    self._data(data)
                elif data.packetType == FilePacketType.END:
                    self._end(data)
                elif data.packetType == FilePacketType.CANCEL:
                    self.abort("cancelled by spacecraft")
                return True
            except OSError as error:
                self._storage_fault(error)
                return False
            except (ValueError, UnicodeError, struct.error) as error:
                self.abort(str(error))
                LOG.warning("File rejected: %s", error)
                return False

    def _start(self, data) -> None:
        if self.active is not None:
            self.abort("superseded by a new START")
            # Reject the overlapping START too: drain and use a fresh request.
            raise ValueError("overlapping file START")
        if data.seqID != 0 or not 0 < data.size <= self.max_file_bytes:
            raise ValueError("invalid START sequence or file size")
        destination = valid_name(data.destPath.decode("utf-8"))
        source_path = data.sourcePath.decode("utf-8")
        if not source_path or any(ord(char) < 32 for char in source_path):
            raise ValueError("invalid source path")
        final = self.directory / destination
        part = self.directory / (destination + ".part")
        receipt_path = self.directory / (destination + ".receipt.json")
        if any(path.exists() or path.is_symlink() for path in (final, part, receipt_path)):
            raise ValueError("destination already exists; choose a fresh destination")
        self._spool_capacity(data.size)
        receipt = {
            "state": "receiving", "source_path": source_path, "destination": destination,
            "expected_size": data.size, "received_size": 0, "started_at": utc_now(),
            "started_unix": time.time(), "updated_at": utc_now(),
        }
        # Exclusive creation makes a second bridge fail closed on the same name.
        output = part.open("xb")
        self.active = Incoming(receipt_path, part, final, output, receipt, CFDPChecksum(),
                               hashlib.sha256(), 1, 0, self.clock())
        atomic_json(receipt_path, receipt)
        self.write_status()
        LOG.info("receiving destination=%s bytes=%d", destination, data.size)

    def _data(self, data) -> None:
        active = self.active
        if (data.offset != active.offset or not data.dataVar or
                active.offset + len(data.dataVar) > active.receipt["expected_size"]):
            raise ValueError("invalid DATA offset or length")
        if active.output.write(data.dataVar) != len(data.dataVar):
            raise OSError("short local file write")
        active.checksum.update(data.dataVar, active.offset)
        active.sha256.update(data.dataVar)
        active.offset += len(data.dataVar)
        active.sequence = (active.sequence + 1) & 0xFFFFFFFF
        active.last_progress = self.clock()
        active.receipt["received_size"] = active.offset

    def _end(self, data) -> None:
        active = self.active
        checksum = data.hashValue[0]  # Stock FileDecoder returns a one-element tuple.
        if active.offset != active.receipt["expected_size"] or checksum != active.checksum.value:
            raise ValueError("END size or checksum mismatch")
        active.output.flush()
        os.fsync(active.output.fileno())
        active.output.close()
        active.receipt.update(state="finalizing", sha256=active.sha256.hexdigest(), checksum=checksum,
                              saved_at=utc_now(), updated_at=utc_now())
        atomic_json(active.receipt_path, active.receipt)
        # Destination uniqueness was checked at START; refuse late collisions too.
        if active.final_path.exists() or active.final_path.is_symlink():
            raise ValueError("final destination already exists")
        os.link(active.part_path, active.final_path)
        active.part_path.unlink()
        sync_directory(self.directory)
        active.receipt.update(state="saved_locally", updated_at=utc_now())
        atomic_json(active.receipt_path, active.receipt)
        self.active = None
        self.write_status()
        LOG.info("saved locally destination=%s bytes=%d sha256=%s",
                 active.receipt["destination"], active.offset, active.receipt["sha256"])
        self._inventory_time = float("-inf")
        if self.on_saved:
            self.on_saved(active.receipt_path)

    def abort(self, reason: str) -> None:
        with self.lock:
            if self.stopped:
                return
            if self.active is not None:
                active, self.active = self.active, None
                try:
                    active.output.close()
                    active.receipt.update(state="failed", error=reason, updated_at=utc_now(),
                                          received_size=active.offset)
                    atomic_json(active.receipt_path, active.receipt)
                except OSError as error:
                    self._storage_fault(error)
                LOG.warning("File incomplete destination=%s received=%d reason=%s",
                            active.receipt["destination"], active.offset, reason)
            self.write_status()

    def tick(self) -> None:
        with self.lock:
            if self.stopped:
                return
            if self.active and self.clock() - self.active.last_progress >= self.idle_timeout:
                self.abort("file reception idle timeout")
            if self.active:
                try:
                    self.active.receipt["updated_at"] = utc_now()
                    atomic_json(self.active.receipt_path, self.active.receipt)
                except OSError as error:
                    self._storage_fault(error)
            self.write_status()

    def close(self) -> None:
        with self.lock:
            if self.stopped:
                return
            self.abort("receiver stopped")
            self.stopped = True
            self.write_status()
            if self.process_lock:
                self.process_lock.close()


@contextmanager
def publication_lock(receipt_path: Path):
    """Prevent overlapping uploads; OS locks recover automatically after exit."""
    lock = receipt_path.with_suffix(receipt_path.suffix + ".publish.lock")
    owner = FileLock(lock)
    try:
        yield
    finally:
        owner.close()


class BucketPublisher:
    """Whole-object HTTP publication with streamed SHA-256 readback."""

    def __init__(self, url: str, *, credentials: Path | None = None,
                 bucket: str = "fprimeFilesIn", timeout: float = 30):
        self.url = loopback_url(url)
        self.endpoint = parse.urlsplit(self.url)
        self.bucket = bucket
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("HTTP timeout must be finite and positive")
        self.timeout = timeout
        self.auth = LocalYamcs(url, credentials) if credentials else None

    def _request(self, method: str, path: str, *, file_path: Path | None = None,
                 expected_size: int | None = None) -> tuple[int, int, str]:
        deadline = time.monotonic() + self.timeout

        def remaining() -> float:
            value = deadline - time.monotonic()
            if value <= 0:
                raise TimeoutError("Yamcs HTTP request exceeded its overall deadline")
            return value

        if self.auth and (self.auth.token is None or time.monotonic() >= self.auth.token_expires):
            self.auth.login(timeout=remaining())
        headers = {"Authorization": "Bearer " + self.auth.token} if self.auth else {}
        if file_path:
            headers["Content-Length"] = str(expected_size)
            headers["Content-Type"] = mimetypes.guess_type(file_path.name)[0] or "application/octet-stream"
        connection_type = http.client.HTTPSConnection if self.endpoint.scheme == "https" else http.client.HTTPConnection
        connection = connection_type(self.endpoint.hostname, self.endpoint.port, timeout=remaining())
        expired = threading.Event()
        transport = [None]

        def expire() -> None:
            expired.set()
            sock = transport[0]
            if sock is not None:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

        timer = threading.Timer(remaining(), expire)
        timer.daemon = True
        timer.start()
        try:
            connection.connect()
            transport[0] = connection.sock
            connection.sock.settimeout(remaining())
            connection.putrequest(method, path)
            for name, value in headers.items():
                connection.putheader(name, value)
            connection.endheaders()
            if file_path:
                with file_path.open("rb") as source:
                    for chunk in iter(lambda: source.read(64 * 1024), b""):
                        connection.sock.settimeout(remaining())
                        connection.send(chunk)
            connection.sock.settimeout(remaining())
            response = connection.getresponse()
            if response.status == 401 and self.auth:
                self.auth.token = None  # Refresh only on a later explicit attempt.
            success = 200 <= response.status < 300
            limit = expected_size if method == "GET" and success and expected_size is not None else 8192
            if success and response.getheader("Content-Length") is not None:
                if int(response.getheader("Content-Length")) > limit:
                    raise RuntimeError("Yamcs HTTP response exceeds expected size")
            digest, size = hashlib.sha256(), 0
            while True:
                remaining()
                if expired.is_set():
                    raise TimeoutError("Yamcs HTTP request exceeded its overall deadline")
                chunk = response.read1(min(64 * 1024, limit + 1 - size))
                if not chunk:
                    break
                size += len(chunk)
                digest.update(chunk)
                if size > limit:
                    if success:
                        raise RuntimeError("Yamcs HTTP response exceeds expected size")
                    break  # Error bodies are diagnostic only; close instead of draining.
            remaining()
            return response.status, size, digest.hexdigest()
        except (OSError, http.client.HTTPException) as error:
            if expired.is_set() or time.monotonic() >= deadline:
                raise TimeoutError("Yamcs HTTP request exceeded its overall deadline") from error
            raise
        finally:
            timer.cancel()
            connection.close()

    def publish(self, receipt_path: Path) -> dict:
        receipt_path = Path(receipt_path).absolute()
        if receipt_path.is_symlink():
            raise ValueError("receipt must not be a symlink")
        with publication_lock(receipt_path):
            receipt = read_receipt(receipt_path)
            if receipt.get("state") not in ("saved_locally", "available_in_yamcs"):
                raise ValueError("only a verified local file may be published")
            destination = valid_name(receipt["destination"])
            if receipt_path.name != destination + ".receipt.json":
                raise ValueError("receipt filename does not match destination")
            local = receipt_path.parent / destination
            if local.is_symlink():
                raise ValueError("local file must not be a symlink")
            size, digest = file_sha256(local)
            if size != receipt["expected_size"] or size != receipt["received_size"] or digest != receipt["sha256"]:
                raise ValueError("local file changed since verified reception")
            path = "/api/storage/buckets/" + parse.quote(self.bucket, safe="") + "/objects/" + parse.quote(destination, safe="")
            try:
                # A failed prior POST may already have completed: inspect before retrying.
                status, remote_size, remote_sha = self._request("GET", path, expected_size=size)
                if status == 404:
                    status, _, _ = self._request("POST", path, file_path=local, expected_size=size)
                    if not 200 <= status < 300:
                        raise RuntimeError(f"Yamcs upload returned HTTP {status}")
                    status, remote_size, remote_sha = self._request("GET", path, expected_size=size)
                if status != 200 or remote_size != size or remote_sha != digest:
                    raise RuntimeError("Yamcs readback differs or destination already holds another object")
                receipt.update(state="available_in_yamcs", bucket=self.bucket,
                               published_at=utc_now(), updated_at=utc_now(),
                               bucket_sha256=remote_sha)
                receipt.pop("publication_error", None)
                LOG.info("available in Yamcs destination=%s bytes=%d sha256=%s", destination, size, digest)
            except Exception as error:
                receipt.update(state="saved_locally", bucket=self.bucket,
                               publication_error=str(error), updated_at=utc_now())
                LOG.warning("Publication failed; local file retained destination=%s reason=%s", destination, error)
                raise
            finally:
                atomic_json(receipt_path, receipt)
            return receipt


class PublicationWorker:
    """One bounded background uploader. Each saved file gets one attempt."""

    def __init__(self, publisher: BucketPublisher):
        self.publisher = publisher
        self.pending: queue.Queue = queue.Queue(maxsize=32)
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, name="file-publication", daemon=True)
        self.thread.start()

    def submit(self, receipt: Path) -> None:
        try:
            self.pending.put_nowait(receipt)
        except queue.Full:
            LOG.warning("Publication queue full; saved local file needs manual retry: %s", receipt.name)

    def _run(self) -> None:
        while not self.stop.is_set():
            try:
                receipt = self.pending.get(timeout=0.2)
            except queue.Empty:
                continue
            try:
                self.publisher.publish(receipt)
            except Exception:
                LOG.exception("File publication needs manual retry: %s", receipt.name)
            finally:
                self.pending.task_done()

    def close(self) -> None:
        self.stop.set()
        self.thread.join(timeout=2)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retry", required=True, type=Path, help="verified .receipt.json to publish again")
    parser.add_argument("--station", type=Path, default=Path.home() / "yamcs-station")
    parser.add_argument("--url", type=loopback_url, default="http://127.0.0.1:8090")
    parser.add_argument("--bucket", default="fprimeFilesIn")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    credentials = args.station.expanduser() / "Yamcs-access.txt"
    publisher = BucketPublisher(args.url, credentials=credentials if credentials.is_file() else None,
                                bucket=args.bucket)
    try:
        result = publisher.publish(args.retry)
    except Exception as error:
        print(f"Publication failed: {error}")
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
