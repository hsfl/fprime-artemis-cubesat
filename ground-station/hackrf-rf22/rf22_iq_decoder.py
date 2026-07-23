#!/usr/bin/env python3
"""Decode the current C3M RFM23BP/RadioHead downlink from HackRF cs8 IQ.

This first receiver intentionally matches the checked-in bench contract:

* HackRF center: 432.5 MHz
* RF carrier: 433.0 MHz
* sample rate: 8 Msps
* GFSK: 125 kbps, 125 kHz deviation
* RF22 sync: 0x2D 0xD4
* satellite -> ground header: A1 A2 C3 01

It decodes and CRC-checks RF22 frames, then reassembles the repository's
0xA5 (CCSDS/GDS) and 0xA6 (payload) RF segments. Live HackRF and PTY support
will build on this verified offline decoder.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterable

import numpy as np

from rf22_protocol import DEFAULT_PROFILE, NetworkProfile, crc16_ibm_msb, load_profile


SYNC_BITS = np.array([int(bit) for bit in f"{0x2D:08b}{0xD4:08b}"], dtype=np.uint8)
EXPECTED_HEADER = DEFAULT_PROFILE.radio_header("downlink")
MAGIC_TO_CHANNEL = {
    DEFAULT_PROFILE.segment_magic[0]: 0,
    DEFAULT_PROFILE.segment_magic[1]: 1,
}
RF_SEGMENT_HEADER_LEN = DEFAULT_PROFILE.segment_header_len
RF_PACKET_MAX_LEN = DEFAULT_PROFILE.packet_max_len
FRAME_MAX_PAYLOAD = DEFAULT_PROFILE.frame_max_payload


@dataclass(frozen=True)
class Rf22Frame:
    timestamp_s: float
    header: bytes
    payload: bytes
    received_crc: int


@dataclass(frozen=True)
class ReassembledMessage:
    timestamp_s: float
    channel: int
    msg_id: int
    data: bytes


class SegmentReassembler:
    def __init__(self, profile: NetworkProfile = DEFAULT_PROFILE) -> None:
        self.profile = profile
        self._states: dict[int, dict[str, object]] = {}
        self._last_completed: dict[int, int] = {}
        self.timeouts = 0
        self.drops = 0
        self.duplicates = 0
        self.completed = 0

    def reset(self) -> None:
        self._states.clear()

    def accept(self, frame: Rf22Frame) -> ReassembledMessage | None:
        packet = frame.payload
        if len(packet) < self.profile.segment_header_len:
            self.drops += 1
            return None

        try:
            channel = self.profile.segment_magic.index(packet[0])
        except ValueError:
            self.drops += 1
            return None

        msg_id, seg_idx, seg_count, chunk_len = packet[1:5]
        if seg_idx == self.profile.ack_segment_index:
            return None
        if seg_count == 0 or seg_idx >= seg_count or chunk_len == 0:
            self.drops += 1
            return None
        if len(packet) != self.profile.segment_header_len + chunk_len:
            self.drops += 1
            return None

        state = self._states.get(channel)
        if state is not None:
            last_timestamp = float(state["last_timestamp"])
            if (frame.timestamp_s - last_timestamp) * 1000 > self.profile.reassembly_timeout_ms:
                self._states.pop(channel, None)
                self.timeouts += 1
                state = None

        if state is None and self._last_completed.get(channel) == msg_id:
            self.duplicates += 1
            return None

        if state is None and seg_idx == 0:
            state = {
                "msg_id": msg_id,
                "seg_count": seg_count,
                "next_idx": 0,
                "data": bytearray(),
                "last_timestamp": frame.timestamp_s,
            }
            self._states[channel] = state

        if state is None:
            self.drops += 1
            return None
        if (
            state["msg_id"] == msg_id
            and state["seg_count"] == seg_count
            and seg_idx < int(state["next_idx"])
        ):
            self.duplicates += 1
            return None
        if (
            state["msg_id"] != msg_id
            or state["seg_count"] != seg_count
            or state["next_idx"] != seg_idx
        ):
            self._states.pop(channel, None)
            self.drops += 1
            if seg_idx == 0:
                state = {
                    "msg_id": msg_id,
                    "seg_count": seg_count,
                    "next_idx": 0,
                    "data": bytearray(),
                    "last_timestamp": frame.timestamp_s,
                }
                self._states[channel] = state
            else:
                return None

        if state["next_idx"] != seg_idx:
            return None

        data = state["data"]
        assert isinstance(data, bytearray)
        data.extend(packet[self.profile.segment_header_len :])
        state["next_idx"] = seg_idx + 1
        state["last_timestamp"] = frame.timestamp_s
        if len(data) > self.profile.frame_max_payload:
            self._states.pop(channel, None)
            self.drops += 1
            return None

        if seg_idx + 1 != seg_count:
            return None

        self._states.pop(channel, None)
        self._last_completed[channel] = msg_id
        self.completed += 1
        return ReassembledMessage(frame.timestamp_s, channel, msg_id, bytes(data))


def _pack_msb_bits(bits: np.ndarray) -> bytes:
    usable = (len(bits) // 8) * 8
    if usable == 0:
        return b""
    return np.packbits(bits[:usable].reshape(-1, 8), axis=1, bitorder="big")[:, 0].tobytes()


def _decode_discriminator(
    fm: np.ndarray,
    block_start_s: float,
    decimated_rate: float,
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> Iterable[Rf22Frame]:
    sync_symbols = 16
    samples_per_symbol = 4
    sync_pm = 2 * SYNC_BITS.astype(np.int16) - 1

    for phase in range(samples_per_symbol):
        bits = (fm[phase::samples_per_symbol] > 0).astype(np.uint8)
        if len(bits) < sync_symbols:
            continue
        corr = np.convolve(
            2 * bits.astype(np.int16) - 1,
            sync_pm[::-1],
            mode="valid",
        )
        for hit in np.flatnonzero(corr == sync_symbols):
            after = bits[hit + sync_symbols :]
            prefix = _pack_msb_bits(after[: 5 * 8])
            if len(prefix) < 5:
                continue

            header = prefix[:4]
            length = prefix[4]
            if (
                header != profile.radio_header("downlink")
                or length == 0
                or length > profile.packet_max_len
            ):
                continue

            total_bytes = 4 + 1 + length + 2
            decoded = _pack_msb_bits(after[: total_bytes * 8])
            if len(decoded) != total_bytes:
                continue

            received_crc = int.from_bytes(decoded[-2:], "big")
            if crc16_ibm_msb(decoded[:-2]) != received_crc:
                continue

            sync_sample = phase + hit * samples_per_symbol
            timestamp_s = block_start_s + sync_sample / decimated_rate
            yield Rf22Frame(timestamp_s, header, decoded[5:-2], received_crc)


def decode_cs8_file(
    path: Path,
    *,
    sample_rate: int = 8_000_000,
    center_hz: int = 432_500_000,
    carrier_hz: int = 433_000_000,
    chunk_seconds: float = 1.0,
    overlap_seconds: float = 0.025,
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> Iterable[Rf22Frame]:
    """Stream-decode a HackRF interleaved signed-I/Q capture."""

    raw = np.memmap(path, dtype=np.int8, mode="r")
    complex_samples = raw.size // 2
    decimation = 16
    decimated_rate = sample_rate / decimation
    if decimated_rate != 500_000:
        raise ValueError("current decoder requires 8 Msps input and 16x decimation")

    chunk = int(chunk_seconds * sample_rate)
    overlap = int(overlap_seconds * sample_rate)
    chunk -= chunk % decimation
    overlap -= overlap % decimation
    offset_hz = carrier_hz - center_hz

    seen: set[tuple[int, bytes]] = set()
    for nominal_start in range(0, complex_samples, chunk):
        start = max(0, nominal_start - overlap)
        stop = min(complex_samples, nominal_start + chunk + overlap)
        start -= start % decimation
        stop -= stop % decimation
        if stop - start < decimation * 8:
            continue

        iq = np.asarray(raw[2 * start : 2 * stop], dtype=np.float32).reshape(-1, 2)
        groups = iq.reshape(-1, decimation, 2)

        within = np.arange(decimation, dtype=np.float64)
        group_starts = start + np.arange(len(groups), dtype=np.float64) * decimation
        within_osc = np.exp(-2j * np.pi * offset_hz * within / sample_rate)
        group_phase = np.exp(-2j * np.pi * offset_hz * group_starts / sample_rate)
        mixed = (
            groups[:, :, 0] @ within_osc + 1j * (groups[:, :, 1] @ within_osc)
        ) / decimation
        mixed *= group_phase

        fm = np.angle(mixed[1:] * np.conj(mixed[:-1]))
        fm -= np.median(fm)
        block_start_s = start / sample_rate
        block_frames = sorted(
            _decode_discriminator(fm, block_start_s, decimated_rate, profile),
            key=lambda item: item.timestamp_s,
        )
        for frame in block_frames:
            # Overlap is only decoding context. Emit frames from the nominal
            # one-second window so messages remain in strict time order.
            if frame.timestamp_s < nominal_start / sample_rate:
                continue
            if (
                nominal_start + chunk < complex_samples
                and frame.timestamp_s >= (nominal_start + chunk) / sample_rate
            ):
                continue
            key = (int(round(frame.timestamp_s * 1000)), frame.header + frame.payload)
            if key in seen:
                continue
            seen.add(key)
            yield frame


class StreamingCs8Decoder:
    """Incremental decoder that delays the overlap tail until the next block."""

    def __init__(
        self,
        *,
        sample_rate: int = 8_000_000,
        center_hz: int = 432_500_000,
        carrier_hz: int = 433_000_000,
        overlap_seconds: float = 0.025,
        profile: NetworkProfile = DEFAULT_PROFILE,
    ) -> None:
        self.sample_rate = sample_rate
        self.center_hz = center_hz
        self.carrier_hz = carrier_hz
        self.profile = profile
        self.decimation = 16
        if sample_rate / self.decimation != 500_000:
            raise ValueError("current decoder requires 8 Msps input and 16x decimation")
        self.overlap_samples = int(overlap_seconds * sample_rate)
        self.overlap_samples -= self.overlap_samples % self.decimation
        self._tail = b""
        self._total_samples = 0
        self._emit_after_s = 0.0

    def reset(self) -> None:
        self._tail = b""
        self._total_samples = 0
        self._emit_after_s = 0.0

    def feed(self, raw_bytes: bytes) -> list[Rf22Frame]:
        if len(raw_bytes) % 2:
            raw_bytes = raw_bytes[:-1]
        current_samples = len(raw_bytes) // 2
        usable_samples = current_samples - (current_samples % self.decimation)
        if usable_samples <= 0:
            return []
        raw_bytes = raw_bytes[: usable_samples * 2]
        combined = self._tail + raw_bytes
        combined_start = self._total_samples - len(self._tail) // 2

        raw = np.frombuffer(combined, dtype=np.int8)
        iq = raw.astype(np.float32).reshape(-1, 2)
        groups = iq.reshape(-1, self.decimation, 2)
        within = np.arange(self.decimation, dtype=np.float64)
        group_starts = combined_start + np.arange(len(groups), dtype=np.float64) * self.decimation
        offset_hz = self.carrier_hz - self.center_hz
        within_osc = np.exp(-2j * np.pi * offset_hz * within / self.sample_rate)
        group_phase = np.exp(-2j * np.pi * offset_hz * group_starts / self.sample_rate)
        mixed = (groups[:, :, 0] @ within_osc + 1j * (groups[:, :, 1] @ within_osc)) / self.decimation
        mixed *= group_phase
        fm = np.angle(mixed[1:] * np.conj(mixed[:-1]))
        fm -= np.median(fm)

        safe_end_sample = self._total_samples + usable_samples - self.overlap_samples
        safe_end_s = max(self._emit_after_s, safe_end_sample / self.sample_rate)
        candidates = sorted(
            _decode_discriminator(
                fm,
                combined_start / self.sample_rate,
                self.sample_rate / self.decimation,
                self.profile,
            ),
            key=lambda frame: frame.timestamp_s,
        )
        emitted: list[Rf22Frame] = []
        seen: set[tuple[int, bytes]] = set()
        for frame in candidates:
            if frame.timestamp_s < self._emit_after_s or frame.timestamp_s >= safe_end_s:
                continue
            # Multiple symbol phases can decode the same burst a few
            # microseconds apart. RF packets cannot legitimately repeat within
            # one millisecond at this modem rate, so collapse those copies.
            key = (int(round(frame.timestamp_s * 1_000)), frame.header + frame.payload)
            if key in seen:
                continue
            seen.add(key)
            emitted.append(frame)

        self._emit_after_s = safe_end_s
        self._total_samples += usable_samples
        combined_samples = len(combined) // 2
        tail_samples = min(self.overlap_samples, combined_samples)
        self._tail = combined[-tail_samples * 2 :]
        return emitted


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path, help="HackRF cs8 capture")
    parser.add_argument("--channel-0-output", type=Path)
    parser.add_argument("--channel-1-output", type=Path)
    parser.add_argument("--network", default=DEFAULT_PROFILE.name)
    args = parser.parse_args()
    profile = load_profile(args.network)

    outputs: dict[int, BinaryIO] = {}
    if args.channel_0_output:
        args.channel_0_output.parent.mkdir(parents=True, exist_ok=True)
        outputs[0] = args.channel_0_output.open("wb")
    if args.channel_1_output:
        args.channel_1_output.parent.mkdir(parents=True, exist_ok=True)
        outputs[1] = args.channel_1_output.open("wb")

    reassembler = SegmentReassembler(profile)
    frame_count = 0
    message_count = 0
    try:
        for frame in decode_cs8_file(args.capture, profile=profile):
            frame_count += 1
            packet = frame.payload
            summary = packet[:5].hex(" ") if len(packet) >= 5 else packet.hex(" ")
            print(
                f"frame t={frame.timestamp_s:.6f}s len={len(packet):02d} "
                f"header={frame.header.hex(' ')} segment={summary} crc=ok"
            )
            message = reassembler.accept(frame)
            if message is None:
                continue
            message_count += 1
            print(
                f"message t={message.timestamp_s:.6f}s channel={message.channel} "
                f"msg_id=0x{message.msg_id:02x} bytes={len(message.data)}"
            )
            output = outputs.get(message.channel)
            if output is not None:
                output.write(message.data)
                output.flush()
    finally:
        for output in outputs.values():
            output.close()

    print(f"summary frames={frame_count} messages={message_count}")
    return 0 if frame_count > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
