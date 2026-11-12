"""Shared bounded wire helpers for the local twin's physical-I/O substitutes."""
from __future__ import annotations

import asyncio
from dataclasses import dataclass
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import pty
import tty

MAGIC = b"RF"
MAX_RADIO_BYTES = 49


def crc16(data: bytes) -> int:
    value = 0xFFFF
    for byte in data:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) & 0xFFFF if value & 0x8000 else (value << 1) & 0xFFFF
    return value


@dataclass(frozen=True)
class RadioPacket:
    data: bytes
    to: int = 0xA1
    source: int = 0xA2
    network: int = 0xC3
    version: int = 1

    def encode(self) -> bytes:
        if not 1 <= len(self.data) <= MAX_RADIO_BYTES:
            raise ValueError("radio packet length must be 1..49")
        if any(not 0 <= value <= 255 for value in (self.to, self.source, self.network, self.version)):
            raise ValueError("radio address/network/version must fit U8")
        body = bytes((len(self.data), self.to, self.source, self.network, self.version)) + self.data
        return MAGIC + body + crc16(body).to_bytes(2, "little")


class RadioParser:
    """Incremental parser that bounds storage and recovers from corrupt records."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.crc_drops = 0
        self.framing_drops = 0

    def feed(self, data: bytes) -> list[RadioPacket]:
        self.buffer.extend(data)
        output = []
        while self.buffer:
            start = self.buffer.find(MAGIC)
            if start < 0:
                self.buffer[:] = b"R" if self.buffer.endswith(b"R") else b""
                break
            if start:
                del self.buffer[:start]
            if len(self.buffer) < 7:
                break
            size = self.buffer[2]
            if not 1 <= size <= MAX_RADIO_BYTES:
                self.framing_drops += 1
                del self.buffer[0]
                continue
            total = size + 9
            if len(self.buffer) < total:
                break
            body = bytes(self.buffer[2:total - 2])
            expected = int.from_bytes(self.buffer[total - 2:total], "little")
            if crc16(body) != expected:
                self.crc_drops += 1
                del self.buffer[0]
                continue
            output.append(RadioPacket(body[5:], body[1], body[2], body[3], body[4]))
            del self.buffer[:total]
        return output


def log_event(directory: Path, event: str, **values) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    record = {"time": datetime.now(timezone.utc).isoformat(), "event": event, **values}
    with (directory / "events.jsonl").open("a") as stream:
        stream.write(json.dumps(record, sort_keys=True) + "\n")
    print(json.dumps(record, sort_keys=True), flush=True)


class PtyBridge:
    """Keep a local raw PTY alive while reconnecting its byte stream to TCP."""

    def __init__(self, path: Path, host: str, port: int | None, log: Path) -> None:
        self.path, self.host, self.port, self.log = path, host, port, log
        self.master, self.slave = pty.openpty()
        tty.setraw(self.slave)
        os.set_blocking(self.master, False)
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.is_symlink():
            path.unlink()
        if path.exists():
            raise RuntimeError(f"Refusing to replace existing PTY path {path}")
        path.symlink_to(os.ttyname(self.slave))
        self.stopping = False

    async def run(self) -> None:
        if self.port is None:
            while not self.stopping:
                await asyncio.sleep(0.1)
            return
        while not self.stopping:
            try:
                reader, writer = await asyncio.open_connection(self.host, self.port)
                log_event(self.log, "uart_connected", path=str(self.path), port=self.port)
                tasks = [asyncio.create_task(self._to_tcp(writer)), asyncio.create_task(self._from_tcp(reader))]
                done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
                for task in pending:
                    task.cancel()
                await asyncio.gather(*tasks, return_exceptions=True)
                writer.close()
                await writer.wait_closed()
            except (OSError, ConnectionError) as exc:
                log_event(self.log, "uart_disconnected", path=str(self.path), detail=str(exc))
            await asyncio.sleep(0.25)

    async def _to_tcp(self, writer: asyncio.StreamWriter) -> None:
        while not self.stopping:
            try:
                data = os.read(self.master, 4096)
            except BlockingIOError:
                await asyncio.sleep(0.002)
                continue
            if not data:
                return
            writer.write(data)
            await asyncio.wait_for(writer.drain(), timeout=2)

    async def _from_tcp(self, reader: asyncio.StreamReader) -> None:
        while data := await reader.read(4096):
            offset = 0
            deadline = asyncio.get_running_loop().time() + 2
            while offset < len(data):
                try:
                    offset += os.write(self.master, data[offset:])
                except BlockingIOError:
                    if asyncio.get_running_loop().time() > deadline:
                        log_event(self.log, "uart_overflow", path=str(self.path), dropped=len(data) - offset)
                        break
                    await asyncio.sleep(0.002)

    def close(self) -> None:
        self.stopping = True
        for descriptor in (self.master, self.slave):
            os.close(descriptor)
        if self.path.is_symlink():
            self.path.unlink()
