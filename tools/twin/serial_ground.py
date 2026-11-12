"""Explicit Ground Teensy TripleSerial transport, below the shared Yamcs service.

CDC0/Serial is raw CCSDS; CDC2/SerialUSB2 is raw science (including RETRY).
The Teensy already owns RF segmentation and ACKs: never apply GroundLink here.
No enumeration, command console, RF setup, or automatic port selection occurs.
"""
from __future__ import annotations

import asyncio
import os
from pathlib import Path


class SerialEndpoint:
    def __init__(self, channel, path, receive, disconnected, log, baud=115200):
        self.channel, self.path = channel, path
        self.receive, self.disconnected, self.log = receive, disconnected, log
        self.baud = baud
        self.port = None
        self.stopping = False
        self.tx_lock = asyncio.Lock()

    def open(self):
        import serial
        options = dict(port=self.path, baudrate=self.baud, timeout=0, write_timeout=0.5,
                       xonxoff=False, rtscts=False, dsrdtr=False)
        if os.name == "posix":
            options["exclusive"] = True
        self.port = serial.Serial(**options)
        self.log("serial_connected", channel=self.channel, port=self.path)

    def close(self):
        port, self.port = self.port, None
        if port is not None:
            port.close()

    async def failed(self, detail):
        self.close()
        self.log("serial_disconnected", channel=self.channel, port=self.path, detail=str(detail))
        await self.disconnected(self.channel, str(detail))

    async def run(self):
        while not self.stopping:
            try:
                if self.port is None:
                    self.open()
                data = self.port.read(4096)
                if data:
                    await self.receive(self.channel, data)
                else:
                    await asyncio.sleep(0.005)
            except (OSError, ValueError) as exc:
                await self.failed(exc)
                await asyncio.sleep(0.25)

    async def send(self, data):
        if not data or len(data) > 4096:
            return False
        async with self.tx_lock:
            port = self.port
            if port is None or not port.is_open:
                return False
            def write_all():
                offset = 0
                while offset < len(data):
                    written = port.write(data[offset:])
                    if written <= 0:
                        raise OSError("serial write made no progress")
                    offset += written
            try:
                await asyncio.to_thread(write_all)
                return port is self.port and port.is_open
            except (OSError, ValueError) as exc:
                if self.port is port:
                    await self.failed(exc)
                return False


class SerialGround:
    def __init__(self, ccsds_port, science_port, receive, disconnected, log, baud=115200):
        if not ccsds_port or not science_port:
            raise ValueError("Serial HIL requires explicit CCSDS and science ports")
        if Path(ccsds_port).resolve() == Path(science_port).resolve():
            raise ValueError("CCSDS and science must use distinct serial ports")
        # Reject aliases of the same existing character device/PTY as well.
        try:
            if os.path.samefile(ccsds_port, science_port):
                raise ValueError("CCSDS and science resolve to the same device")
        except FileNotFoundError:
            pass
        self.endpoints = [SerialEndpoint(0, ccsds_port, receive, disconnected, log, baud),
                          SerialEndpoint(1, science_port, receive, disconnected, log, baud)]

    def open(self):
        try:
            for endpoint in self.endpoints:
                endpoint.open()
        except Exception:
            self.close()
            raise

    async def run(self):
        await asyncio.gather(*(endpoint.run() for endpoint in self.endpoints))

    async def transmit(self, channel, data):
        return channel in (0, 1) and await self.endpoints[channel].send(data)

    def close(self):
        for endpoint in self.endpoints:
            endpoint.stopping = True
            endpoint.close()
