#!/usr/bin/env python3
"""Build an offline HackRF waveform for one C3M RF22 message.

This helper deliberately cannot transmit.  Live RF is owned by the supervised
ground-station bridge so every transmission passes the shared safety policy.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

from rf22_protocol import DEFAULT_PROFILE, NetworkProfile, build_message_packets, load_profile


def bytes_to_bits(data: bytes) -> np.ndarray:
    return np.unpackbits(np.frombuffer(data, dtype=np.uint8), bitorder="big")


def build_rf22_packets(
    message: bytes,
    msg_id: int,
    *,
    channel: int = 0,
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> list[bytes]:
    return build_message_packets(
        message,
        channel=channel,
        msg_id=msg_id & 0xFF,
        direction="uplink",
        profile=profile,
    )


def build_rf22_packet(
    message: bytes,
    msg_id: int,
    *,
    channel: int = 0,
    profile: NetworkProfile = DEFAULT_PROFILE,
) -> bytes:
    """Backward-compatible convenience helper for a single RF segment."""

    packets = build_rf22_packets(message, msg_id, channel=channel, profile=profile)
    if len(packets) != 1:
        raise ValueError(
            f"single-packet helper accepts at most {profile.segment_max_data} message bytes"
        )
    return packets[0]


def modulate(
    packet: bytes,
    *,
    repeat: int = 5,
    offset_hz: int = 500_000,
    leading_silence_s: float = 0.100,
    inter_burst_silence_s: float = 0.100,
    trailing_silence_s: float = 0.100,
) -> bytes:
    if repeat < 1:
        raise ValueError("repeat must be at least 1")
    sample_rate = 8_000_000
    symbol_rate = 125_000
    samples_per_symbol = sample_rate // symbol_rate
    deviation_hz = 125_000
    amplitude = 90.0

    # The live RFM23 waveform shows exactly 32 alternating preamble bits,
    # followed by sync 0x2D,0xD4 and the RadioHead packet body.
    on_air = bytes((0x55, 0x55, 0x55, 0x55, 0x2D, 0xD4)) + packet
    symbols = 2.0 * bytes_to_bits(on_air).astype(np.float64) - 1.0
    nrz = np.repeat(symbols, samples_per_symbol)

    # Gaussian pulse shaping (BT=0.5), spanning four symbols.
    bt = 0.5
    t = np.arange(-2 * samples_per_symbol, 2 * samples_per_symbol + 1) / samples_per_symbol
    taps = np.exp(-2.0 * (np.pi * bt * t) ** 2 / np.log(2.0))
    taps /= taps.sum()
    shaped = np.convolve(nrz, taps, mode="same")
    frequency = offset_hz + deviation_hz * shaped
    phase = np.cumsum(2.0 * np.pi * frequency / sample_rate)
    signal = amplitude * np.exp(1j * phase)
    iq = np.empty(signal.size * 2, dtype=np.int8)
    iq[0::2] = np.rint(signal.real).clip(-127, 127).astype(np.int8)
    iq[1::2] = np.rint(signal.imag).clip(-127, 127).astype(np.int8)

    burst = iq.tobytes()
    leading = bytes(2 * int(max(0.0, leading_silence_s) * sample_rate))
    inter_burst = bytes(2 * int(max(0.0, inter_burst_silence_s) * sample_rate))
    trailing = bytes(2 * int(max(0.0, trailing_silence_s) * sample_rate))
    parts = [leading]
    for index in range(repeat):
        parts.append(burst)
        parts.append(inter_burst if index + 1 < repeat else trailing)
    return b"".join(parts)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("message", type=Path, help="raw channel uplink message (maximum 220 bytes)")
    parser.add_argument("--msg-id", type=lambda value: int(value, 0), default=0x80)
    parser.add_argument("--channel", type=int, choices=(0, 1), default=0)
    parser.add_argument("--network", default=DEFAULT_PROFILE.name)
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--direct", action="store_true", help="tune directly to 433 MHz")
    args = parser.parse_args()

    profile = load_profile(args.network)
    message = args.message.read_bytes()
    packets = build_rf22_packets(
        message,
        args.msg_id & 0xFF,
        channel=args.channel,
        profile=profile,
    )
    waveform = b"".join(
        modulate(
            packet,
            repeat=args.repeat,
            offset_hz=0 if args.direct else 500_000,
        )
        for packet in packets
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(waveform)
    print(
        f"TX_MESSAGE header={profile.radio_header('uplink').hex(' ')} channel={args.channel} "
        f"id=0x{args.msg_id & 0xff:02x} message_bytes={len(message)} "
        f"segments={len(packets)} repeat={args.repeat} waveform_bytes={len(waveform)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
