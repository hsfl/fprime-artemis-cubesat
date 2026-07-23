#!/usr/bin/env python3
"""Repository-backed RF22 link protocol helpers for the HackRF ground adapter."""

from __future__ import annotations

import json
import os
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Literal


REPO_ROOT = Path(__file__).resolve().parents[2]
TRANSPORT_MANIFEST = REPO_ROOT / "config" / "transport_constants.json"
NETWORK_MANIFEST = REPO_ROOT / "config" / "rf_networks.json"

Direction = Literal["uplink", "downlink"]


@dataclass(frozen=True)
class NetworkProfile:
    name: str
    label: str
    network_id: int
    protocol_version: int
    ground_address: int
    satellite_address: int
    frame_max_payload: int
    segment_magic: tuple[int, int]
    ack_segment_index: int
    packet_max_len: int
    segment_header_len: int
    reassembly_timeout_ms: int
    ack_retries: int
    ack_timeout_ms: int

    @property
    def segment_max_data(self) -> int:
        return self.packet_max_len - self.segment_header_len

    def radio_header(self, direction: Direction) -> bytes:
        if direction == "downlink":
            to_address, from_address = self.ground_address, self.satellite_address
        elif direction == "uplink":
            to_address, from_address = self.satellite_address, self.ground_address
        else:
            raise ValueError(f"unsupported RF direction: {direction}")
        return bytes((to_address, from_address, self.network_id, self.protocol_version))

    def magic_for_channel(self, channel: int) -> int:
        if channel not in (0, 1):
            raise ValueError(f"RF channel must be 0 or 1, got {channel}")
        return self.segment_magic[channel]


def load_profile(name: str | None = None) -> NetworkProfile:
    transport = json.loads(TRANSPORT_MANIFEST.read_text(encoding="utf-8"))
    networks = json.loads(NETWORK_MANIFEST.read_text(encoding="utf-8"))
    selected = name or str(transport["rf"]["network"])
    try:
        network = networks["networks"][selected]
    except KeyError as exc:
        available = ", ".join(sorted(networks.get("networks", {})))
        raise ValueError(f"unknown RF network {selected!r}; choose one of: {available}") from exc

    rf = transport["rf"]
    return NetworkProfile(
        name=selected,
        label=str(network["label"]),
        network_id=int(network["id"]),
        protocol_version=int(networks["protocol_version"]),
        ground_address=int(networks["addresses"]["ground"]),
        satellite_address=int(networks["addresses"]["satellite"]),
        frame_max_payload=int(transport["frame"]["max_payload"]),
        segment_magic=(int(rf["segment_magic_ccsds"]), int(rf["segment_magic_payload"])),
        ack_segment_index=int(rf["ack_segment_index"]),
        packet_max_len=int(rf["packet_max_len"]),
        segment_header_len=int(rf["segment_header_len"]),
        reassembly_timeout_ms=int(rf["reassembly_timeout_ms"]),
        ack_retries=int(rf["ack_retries"]),
        ack_timeout_ms=int(rf["ack_timeout_ms"]),
    )


DEFAULT_PROFILE = load_profile()


def crc16_ibm_msb(data: bytes) -> int:
    """RF22 CRC-16/IBM: polynomial 0x8005, init/xorout 0, MSB first."""

    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_segment_payload(
    data: bytes,
    *,
    channel: int,
    msg_id: int,
    segment_index: int,
    segment_count: int,
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> bytes:
    if not 1 <= len(data) <= profile.segment_max_data:
        raise ValueError(f"RF segment data must be 1..{profile.segment_max_data} bytes")
    if not 0 <= msg_id <= 0xFF:
        raise ValueError("message ID must fit in one byte")
    if not 1 <= segment_count <= 0xFF or not 0 <= segment_index < segment_count:
        raise ValueError("invalid RF segment index/count")
    return bytes(
        (
            profile.magic_for_channel(channel),
            msg_id,
            segment_index,
            segment_count,
            len(data),
        )
    ) + data


def build_rf22_frame(
    payload: bytes,
    *,
    direction: Direction,
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> bytes:
    if not 1 <= len(payload) <= profile.packet_max_len:
        raise ValueError(f"RF22 payload must be 1..{profile.packet_max_len} bytes")
    body = profile.radio_header(direction) + bytes((len(payload),)) + payload
    return body + crc16_ibm_msb(body).to_bytes(2, "big")


def build_message_packets(
    message: bytes,
    *,
    channel: int,
    msg_id: int,
    direction: Direction = "uplink",
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> list[bytes]:
    if not 1 <= len(message) <= profile.frame_max_payload:
        raise ValueError(f"message must be 1..{profile.frame_max_payload} bytes")
    segment_count = (len(message) + profile.segment_max_data - 1) // profile.segment_max_data
    packets: list[bytes] = []
    for index in range(segment_count):
        start = index * profile.segment_max_data
        data = message[start : start + profile.segment_max_data]
        segment = build_segment_payload(
            data,
            channel=channel,
            msg_id=msg_id,
            segment_index=index,
            segment_count=segment_count,
            profile=profile,
        )
        packets.append(build_rf22_frame(segment, direction=direction, profile=profile))
    return packets


def ack_payload(
    *, channel: int, msg_id: int, segment_index: int, profile: NetworkProfile = DEFAULT_PROFILE
) -> bytes:
    return bytes(
        (
            profile.magic_for_channel(channel),
            msg_id & 0xFF,
            profile.ack_segment_index,
            segment_index & 0xFF,
            0,
        )
    )


def matches_ack(
    payload: bytes,
    *,
    channel: int,
    msg_id: int,
    segment_index: int,
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> bool:
    return payload == ack_payload(
        channel=channel,
        msg_id=msg_id,
        segment_index=segment_index,
        profile=profile,
    )


class MessageIdStore:
    """Atomically reserve per-channel IDs so a crash cannot reuse a command ID."""

    def __init__(self, path: Path, *, seed: int | None = None) -> None:
        self.path = path
        self.seed = (time.time_ns() if seed is None else seed) & 0xFF

    def _read(self) -> dict[int, int]:
        try:
            text = self.path.read_text(encoding="utf-8").strip()
        except FileNotFoundError:
            return {0: self.seed, 1: (self.seed + 0x80) & 0xFF}
        try:
            decoded = json.loads(text)
        except json.JSONDecodeError:
            try:
                legacy = int(text, 0) & 0xFF
            except ValueError as exc:
                raise ValueError(f"invalid message-ID state: {self.path}") from exc
            return {0: legacy, 1: (legacy + 0x80) & 0xFF}
        if isinstance(decoded, int):
            legacy = decoded & 0xFF
            return {0: legacy, 1: (legacy + 0x80) & 0xFF}
        if not isinstance(decoded, dict):
            raise ValueError(f"invalid message-ID state: {self.path}")
        channels = decoded.get("next_by_channel", {})
        return {
            0: int(channels.get("0", self.seed)) & 0xFF,
            1: int(channels.get("1", (self.seed + 0x80) & 0xFF)) & 0xFF,
        }

    def _write(self, values: dict[int, int]) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(f".{self.path.name}.{os.getpid()}.tmp")
        encoded = json.dumps(
            {"schema": 1, "next_by_channel": {str(key): value for key, value in values.items()}},
            sort_keys=True,
        ) + "\n"
        try:
            with temporary.open("w", encoding="utf-8") as stream:
                stream.write(encoded)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.path)
        finally:
            temporary.unlink(missing_ok=True)

    def reserve(self, channel: int) -> int:
        if channel not in (0, 1):
            raise ValueError(f"RF channel must be 0 or 1, got {channel}")
        values = self._read()
        reserved = values[channel]
        values[channel] = (reserved + 1) & 0xFF
        self._write(values)
        return reserved
