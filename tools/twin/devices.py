#!/usr/bin/env python3
"""Local physical-I/O environment: UART, power, real PDU helper, and packet RF."""
from __future__ import annotations

import argparse
import asyncio
import contextlib
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import signal

from common import RadioPacket, RadioParser, log_event


def nmea(sentence: str) -> bytes:
    checksum = 0
    for byte in sentence.encode("ascii"):
        checksum ^= byte
    return f"${sentence}*{checksum:02X}\r\n".encode("ascii")


async def paced_write(writer, data: bytes, baud: int) -> None:
    for offset in range(0, len(data), 32):
        chunk = data[offset:offset + 32]
        await asyncio.sleep(len(chunk) * 10 / baud)
        writer.write(chunk)
        await asyncio.wait_for(writer.drain(), 2)


class Devices:
    def __init__(self, host: str = "0.0.0.0", directory: Path = Path("/data")) -> None:
        self.host, self.directory = host, directory
        self.stop = asyncio.Event()
        self.clients: dict[str, asyncio.StreamWriter] = {}
        self.power_clients: set[asyncio.StreamWriter] = set()
        self.power = False
        self.faults = {"pc_connected": True, "radio_connected": True, "drop_science_packet_once": -1,
                       "drop_science_packet_always": -1, "radio_delay_ms": 0}
        self.dropped_once = False
        self.radio_lock = asyncio.Lock()
        self.counts = {"pc_bytes": 0, "radio_packets": 0, "radio_dropped": 0}

    async def client(self, name: str, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        old = self.clients.get(name)
        if old is not None:
            old.close()
        self.clients[name] = writer
        log_event(self.directory, "endpoint_connected", endpoint=name)
        try:
            if name in ("pc-fc", "pc-pi"):
                peer = "pc-pi" if name == "pc-fc" else "pc-fc"
                while data := await reader.read(512):
                    target = self.clients.get(peer)
                    if self.power and self.faults["pc_connected"] and target is not None:
                        # 8N1: ten wire bits for each byte, with backpressure.
                        await asyncio.sleep(len(data) * 10 / 115200)
                        target.write(data)
                        await asyncio.wait_for(target.drain(), 2)
                        self.counts["pc_bytes"] += len(data)
            elif name == "gps":
                while not reader.at_eof():
                    now = datetime.now(timezone.utc)
                    stamp = now.strftime("%H%M%S") + ".00"
                    date = now.strftime("%d%m%y")
                    await paced_write(writer, nmea(f"GPGGA,{stamp},2125.0000,N,15800.0000,W,1,08,0.9,100.0,M,0.0,M,,"), 9600)
                    await paced_write(writer, nmea(f"GPRMC,{stamp},A,2125.0000,N,15800.0000,W,0.0,0.0,{date},,,A"), 9600)
                    await asyncio.sleep(1)
            elif name == "power":
                while line := await reader.readline():
                    if len(line) > 64:
                        break
                    if line.strip() in (b"POWER ON", b"POWER OFF"):
                        await self.set_power(line.strip() == b"POWER ON")
            elif name == "pdu":
                await self.pdu(reader, writer)
            else:
                parser = RadioParser()
                while data := await reader.read(4096):
                    for packet in parser.feed(data):
                        await self.radio(name, packet)
        except (ConnectionError, OSError, asyncio.TimeoutError) as exc:
            log_event(self.directory, "endpoint_error", endpoint=name, detail=str(exc))
        finally:
            if self.clients.get(name) is writer:
                del self.clients[name]
            writer.close()
            with contextlib.suppress(ConnectionError, OSError):
                await writer.wait_closed()
            log_event(self.directory, "endpoint_disconnected", endpoint=name)

    async def pdu(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        binary = Path(os.environ.get("PDU_HOST_BINARY", "/build/pdu/pdu-host"))
        if not binary.is_file():
            log_event(self.directory, "pdu_unavailable", detail=f"Missing real PDU host helper {binary}")
            while await reader.read(1024):
                pass
            return
        process = await asyncio.create_subprocess_exec(str(binary), stdin=asyncio.subprocess.PIPE,
                                                       stdout=asyncio.subprocess.PIPE)
        assert process.stdin is not None and process.stdout is not None

        async def incoming() -> None:
            while data := await reader.read(128):
                await paced_write(process.stdin, data, 9600)

        async def outgoing() -> None:
            while data := await process.stdout.read(128):
                await paced_write(writer, data, 9600)

        tasks = [asyncio.create_task(incoming()), asyncio.create_task(outgoing())]
        try:
            await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            process.terminate()
            await process.wait()

    async def set_power(self, enabled: bool) -> None:
        self.power = enabled
        line = b"POWER ON\n" if enabled else b"POWER OFF\n"
        for writer in tuple(self.power_clients):
            try:
                writer.write(line)
                await writer.drain()
            except (ConnectionError, OSError):
                self.power_clients.discard(writer)
        log_event(self.directory, "pi_power_changed", enabled=enabled)

    async def power_listener(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        self.power_clients.add(writer)
        writer.write(b"POWER ON\n" if self.power else b"POWER OFF\n")
        await writer.drain()
        try:
            await reader.read()
        finally:
            self.power_clients.discard(writer)
            writer.close()

    async def radio(self, source: str, packet: RadioPacket) -> None:
        self.counts["radio_packets"] += 1
        target_name = "radio-ground" if source == "radio-fc" else "radio-fc"
        target = self.clients.get(target_name)
        reason = None
        if not self.faults["radio_connected"] or target is None:
            reason = "disconnected"
        data = packet.data
        # Science DATA fits one 44-byte RF segment with the current protocol.
        if source == "radio-fc" and len(data) >= 12 and data[0] == 0xA6 and data[5:8] == b"N2\x02":
            index = int.from_bytes(data[9:11], "little")
            if index == self.faults["drop_science_packet_always"]:
                reason = "injected_permanent_science_loss"
            elif index == self.faults["drop_science_packet_once"] and not self.dropped_once:
                self.dropped_once = True
                reason = "injected_once_science_loss"
        if reason:
            self.counts["radio_dropped"] += 1
            log_event(self.directory, "radio_drop", source=source, reason=reason, data=data.hex())
            return
        async with self.radio_lock:
            await asyncio.sleep((len(data) + 8) * 8 / 125000 + self.faults["radio_delay_ms"] / 1000)
            assert target is not None
            target.write(packet.encode())
            await asyncio.wait_for(target.drain(), 2)
        log_event(self.directory, "radio_forward", source=source, data=data.hex())

    async def admin(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        status, result = 200, {}
        try:
            header = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 2)
            if len(header) > 8192:
                raise ValueError("HTTP header too large")
            first, *lines = header.decode("ascii").split("\r\n")
            method, path, _ = first.split()
            headers = dict(line.split(":", 1) for line in lines if ":" in line)
            length = int(next((value for key, value in headers.items() if key.lower() == "content-length"), "0"))
            if not 0 <= length <= 4096:
                raise ValueError("HTTP body too large")
            body = await asyncio.wait_for(reader.readexactly(length), 2) if length else b"{}"
            if method == "GET" and path in ("/health", "/state"):
                result = {"power": self.power, "endpoints": sorted(self.clients), "faults": self.faults,
                          "counts": self.counts, "pdu_helper_present": Path("/build/pdu/pdu-host").is_file()}
            elif method == "POST" and path == "/faults":
                changes = json.loads(body)
                if not isinstance(changes, dict) or changes.keys() - self.faults.keys():
                    raise ValueError("Unknown fault key")
                for key, value in changes.items():
                    if key.endswith("connected"):
                        if not isinstance(value, bool):
                            raise ValueError(f"{key} must be boolean")
                    elif not isinstance(value, int) or isinstance(value, bool) or not -1 <= value <= 10000:
                        raise ValueError(f"{key} outside allowed range")
                self.faults.update(changes)
                self.dropped_once = False
                result = {"faults": self.faults}
                log_event(self.directory, "faults_changed", **changes)
            else:
                status, result = 404, {"error": "Not found"}
        except (ValueError, OSError, asyncio.TimeoutError, asyncio.IncompleteReadError) as exc:
            status, result = 400, {"error": str(exc)}
        raw = json.dumps(result).encode()
        writer.write(f"HTTP/1.1 {status} {'OK' if status == 200 else 'Error'}\r\nContent-Type: application/json\r\nContent-Length: {len(raw)}\r\nConnection: close\r\n\r\n".encode() + raw)
        with contextlib.suppress(ConnectionError, OSError):
            await writer.drain()
        writer.close()

    async def run(self) -> None:
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            loop.add_signal_handler(sig, self.stop.set)
        servers = []
        for name, port in {"pc-fc": 6100, "pc-pi": 6101, "gps": 6102, "pdu": 6103,
                           "radio-fc": 6104, "power": 6105, "radio-ground": 6107}.items():
            async def connected(reader, writer, endpoint=name):
                await self.client(endpoint, reader, writer)
            servers.append(await asyncio.start_server(connected, self.host, port, limit=8192))
        servers.append(await asyncio.start_server(self.power_listener, self.host, 6106, limit=128))
        servers.append(await asyncio.start_server(self.admin, self.host, 6200, limit=8192))
        log_event(self.directory, "device_environment_ready")
        try:
            await self.stop.wait()
        finally:
            for server in servers:
                server.close()
            await asyncio.gather(*(server.wait_closed() for server in servers))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--data", type=Path, default=Path("/data"))
    args = parser.parse_args()
    asyncio.run(Devices(args.host, args.data).run())
