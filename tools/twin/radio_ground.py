"""Ground radio link codec. Wire constants come from the owning JSON manifests."""
from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
import json
from pathlib import Path
import time

from common import RadioPacket


@dataclass
class Assembly:
    message: int = -1
    count: int = 0
    next_segment: int = 0
    data: bytearray = field(default_factory=bytearray)
    updated: float = 0
    completed_message: int = -1


class GroundLink:
    def __init__(self, root: Path, send_packet, deliver_frame) -> None:
        transport = json.loads((root / "config/transport_constants.json").read_text())
        networks = json.loads((root / "config/rf_networks.json").read_text())
        self.rf = transport["rf"]
        self.network = networks["networks"][self.rf["network"]]["id"]
        self.ground = networks["addresses"]["ground"]
        self.satellite = networks["addresses"]["satellite"]
        self.version = networks["protocol_version"]
        self.magics = (self.rf["segment_magic_ccsds"], self.rf["segment_magic_payload"])
        self.assemblies = [Assembly(), Assembly()]
        self.next_message = [0, 0]
        self.acks: dict[tuple[int, int, int], asyncio.Future] = {}
        self.send_packet, self.deliver_frame = send_packet, deliver_frame
        self.tx_lock = asyncio.Lock()
        self.counters = {"frames_in": 0, "frames_out": 0, "duplicates": 0, "drops": 0, "ack_timeouts": 0}

    def envelope(self, data: bytes) -> RadioPacket:
        return RadioPacket(data, self.satellite, self.ground, self.network, self.version)

    async def receive(self, packet: RadioPacket) -> None:
        if (packet.to != self.ground or packet.source != self.satellite or packet.network != self.network
                or packet.version != self.version or len(packet.data) < 5 or packet.data[0] not in self.magics):
            self.counters["drops"] += 1
            return
        data = packet.data
        channel = self.magics.index(data[0])
        message, segment, count, length = data[1:5]
        if segment == 255:
            future = self.acks.get((channel, message, count))
            if length == 0 and len(data) == 5 and future is not None and not future.done():
                future.set_result(True)
            return
        if not 1 <= count <= 5 or segment >= count or not 1 <= length <= 44 or len(data) != length + 5:
            self.counters["drops"] += 1
            return
        assembly = self.assemblies[channel]
        now = time.monotonic()
        if assembly.next_segment and now - assembly.updated > self.rf["reassembly_timeout_ms"] / 1000:
            assembly.next_segment = 0
            assembly.data.clear()
        duplicate = ((assembly.next_segment == 0 and message == assembly.completed_message)
                     or (message == assembly.message and count == assembly.count and segment < assembly.next_segment))
        if self.rf["ack_directions"]["satellite_to_ground"]["ccsds" if channel == 0 else "payload"]:
            await self.send_packet(self.envelope(bytes((data[0], message, 255, segment, 0))))
        if duplicate:
            self.counters["duplicates"] += 1
            return
        if message != assembly.message or count != assembly.count or segment != assembly.next_segment:
            assembly.next_segment = 0
            assembly.data.clear()
            if segment != 0:
                self.counters["drops"] += 1
                return
            assembly.message, assembly.count = message, count
        assembly.data.extend(data[5:])
        assembly.next_segment += 1
        assembly.updated = now
        if len(assembly.data) > 220:
            assembly.next_segment = 0
            assembly.data.clear()
            self.counters["drops"] += 1
        elif assembly.next_segment == count:
            body = bytes(assembly.data)
            assembly.completed_message = message
            assembly.next_segment = 0
            assembly.data.clear()
            self.counters["frames_in"] += 1
            await self.deliver_frame(channel, body)

    async def transmit(self, channel: int, data: bytes) -> bool:
        if channel not in (0, 1) or not 1 <= len(data) <= 220:
            return False
        async with self.tx_lock:
            message = self.next_message[channel]
            self.next_message[channel] = (message + 1) & 255
            chunks = [data[offset:offset + 44] for offset in range(0, len(data), 44)]
            ack = self.rf["ack_directions"]["ground_to_satellite"]["ccsds" if channel == 0 else "payload"]
            for index, chunk in enumerate(chunks):
                frame = bytes((self.magics[channel], message, index, len(chunks), len(chunk))) + chunk
                success = False
                for attempt in range(self.rf["ack_retries"] + 1):
                    future = asyncio.get_running_loop().create_future()
                    key = (channel, message, index)
                    if ack:
                        self.acks[key] = future
                    await self.send_packet(self.envelope(frame))
                    if not ack:
                        success = True
                    else:
                        try:
                            await asyncio.wait_for(future, self.rf["ack_timeout_ms"] / 1000)
                            success = True
                        except asyncio.TimeoutError:
                            self.counters["ack_timeouts"] += 1
                        finally:
                            self.acks.pop(key, None)
                    if success:
                        break
                if not success:
                    return False
                if index + 1 < len(chunks):
                    await asyncio.sleep(self.rf["inter_segment_gap_ms"] / 1000)
            self.counters["frames_out"] += 1
            return True
