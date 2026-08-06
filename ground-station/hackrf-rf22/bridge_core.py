#!/usr/bin/env python3
"""Hardware-independent lifecycle and byte-stream helpers for the RF22 bridge."""

from __future__ import annotations

import errno
import fcntl
import json
import os
import pty
import time
import tty
from collections import deque
from pathlib import Path


class BridgeAlreadyRunningError(RuntimeError):
    pass


class BridgeBackpressureError(RuntimeError):
    pass


class BridgeLock:
    def __init__(self, path: Path) -> None:
        self.path = path
        self._stream = None

    def acquire(self) -> None:
        if self._stream is not None:
            raise RuntimeError(f"bridge lock is already held: {self.path}")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        stream = self.path.open("a+", encoding="utf-8")
        try:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            stream.seek(0)
            owner = stream.read().strip() or "unknown"
            stream.close()
            raise BridgeAlreadyRunningError(
                f"another HackRF bridge owns {self.path} (owner {owner})"
            ) from exc
        stream.seek(0)
        stream.truncate()
        stream.write(f"pid={os.getpid()} started={time.time():.6f}\n")
        stream.flush()
        self._stream = stream

    def close(self) -> None:
        if self._stream is None:
            return
        try:
            fcntl.flock(self._stream.fileno(), fcntl.LOCK_UN)
        finally:
            self._stream.close()
            self._stream = None

    def __enter__(self) -> BridgeLock:
        self.acquire()
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.close()


class UplinkBatcher:
    """Match the ground Teensy's 12 ms idle / 220-byte UART batching contract."""

    def __init__(self, max_message_bytes: int = 220, idle_flush_s: float = 0.012) -> None:
        if max_message_bytes < 1 or idle_flush_s <= 0:
            raise ValueError("invalid uplink batcher limits")
        self.max_message_bytes = max_message_bytes
        self.idle_flush_s = idle_flush_s
        self._buffer = bytearray()
        self._last_byte_s: float | None = None

    @property
    def pending_bytes(self) -> int:
        return len(self._buffer)

    def feed(self, data: bytes, now_s: float) -> None:
        if data:
            self._buffer.extend(data)
            self._last_byte_s = now_s

    def pop_ready(self, now_s: float, *, force: bool = False) -> list[bytes]:
        messages: list[bytes] = []
        while len(self._buffer) >= self.max_message_bytes:
            messages.append(bytes(self._buffer[: self.max_message_bytes]))
            del self._buffer[: self.max_message_bytes]
        idle = (
            self._buffer
            and self._last_byte_s is not None
            and now_s - self._last_byte_s >= self.idle_flush_s
        )
        if self._buffer and (force or idle):
            messages.append(bytes(self._buffer))
            self._buffer.clear()
        return messages


def _replaceable_pty_symlink(path: Path) -> bool:
    if not path.is_symlink():
        return False
    target = os.readlink(path)
    return target.startswith("/dev/tty") or target.startswith("/dev/pts/")


class VirtualChannel:
    """One stable symlink backed by a raw, nonblocking pseudo-terminal."""

    def __init__(self, channel: int, symlink: Path, *, max_backlog_bytes: int = 1 << 20) -> None:
        if max_backlog_bytes < 1:
            raise ValueError("max_backlog_bytes must be at least one")
        self.channel = channel
        self.symlink = symlink
        self.max_backlog_bytes = max_backlog_bytes
        self.master_fd = -1
        self.slave_fd = -1
        self.slave_name = ""
        self._pending: deque[bytes] = deque()
        self._pending_offset = 0
        self.backlog_bytes = 0
        self.downlink_bytes = 0
        self.uplink_bytes = 0

    def open(self) -> None:
        if self.master_fd >= 0:
            return
        master, slave = pty.openpty()
        try:
            tty.setraw(slave)
            flags = fcntl.fcntl(master, fcntl.F_GETFL)
            fcntl.fcntl(master, fcntl.F_SETFL, flags | os.O_NONBLOCK)
            slave_name = os.ttyname(slave)
            self.symlink.parent.mkdir(parents=True, exist_ok=True)
            if self.symlink.exists() or self.symlink.is_symlink():
                if not _replaceable_pty_symlink(self.symlink):
                    raise FileExistsError(
                        f"refusing to replace non-PTY path: {self.symlink}"
                    )
                self.symlink.unlink()
            self.symlink.symlink_to(slave_name)
        except BaseException:
            os.close(master)
            os.close(slave)
            raise
        self.master_fd = master
        self.slave_fd = slave
        self.slave_name = slave_name

    def queue_downlink(self, message: bytes) -> None:
        if not message:
            return
        if self.backlog_bytes + len(message) > self.max_backlog_bytes:
            raise BridgeBackpressureError(
                f"channel {self.channel} PTY backlog would exceed "
                f"{self.max_backlog_bytes} bytes"
            )
        self._pending.append(bytes(message))
        self.backlog_bytes += len(message)

    def flush_downlink(self) -> None:
        while self._pending:
            current = self._pending[0]
            try:
                written = os.write(self.master_fd, current[self._pending_offset :])
            except BlockingIOError:
                return
            except OSError as exc:
                if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EIO):
                    return
                raise
            if written <= 0:
                return
            self._pending_offset += written
            self.backlog_bytes -= written
            self.downlink_bytes += written
            if self._pending_offset == len(current):
                self._pending.popleft()
                self._pending_offset = 0

    def read_uplink(self, max_bytes: int = 65_536) -> bytes:
        chunks: list[bytes] = []
        remaining = max_bytes
        while remaining > 0:
            try:
                chunk = os.read(self.master_fd, min(4096, remaining))
            except BlockingIOError:
                break
            except OSError as exc:
                if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EIO):
                    break
                raise
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        data = b"".join(chunks)
        self.uplink_bytes += len(data)
        return data

    def close(self) -> None:
        try:
            if self.symlink.is_symlink() and os.readlink(self.symlink) == self.slave_name:
                self.symlink.unlink()
        except FileNotFoundError:
            # Another cleanup path may have already removed the owned link.
            pass
        if self.master_fd >= 0:
            os.close(self.master_fd)
            self.master_fd = -1
        if self.slave_fd >= 0:
            os.close(self.slave_fd)
            self.slave_fd = -1


def atomic_write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    try:
        with temporary.open("w", encoding="utf-8") as stream:
            stream.write(encoded)
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
