#!/usr/bin/env python3
"""Bounded, operator-hidden gain policy for the known C3M RF22 link."""

from __future__ import annotations

from dataclasses import dataclass


# Ordered from least receiver gain to most.  Keep this deliberately small:
# valid CRC-checked C3M frames, not raw RSSI, are the acceptance signal.
RX_CANDIDATES: tuple[tuple[int, int], ...] = (
    (0, 0),
    (0, 4),
    (0, 8),
    (8, 0),
    (8, 4),
    (8, 8),
    (16, 0),
    (16, 4),
    (16, 8),
    (16, 16),
    (24, 16),
    (32, 24),
    (40, 32),
    (40, 48),
    (40, 62),
)

# Coarse search across the HackRF TX VGA's complete supported range.  A
# delayed application-layer PING response, not an immediate RF ACK, is the
# oracle because HackRF's USB-controlled half-duplex turnaround can miss ACKs.
TX_CANDIDATES: tuple[int, ...] = (0, 8, 16, 24, 32, 40, 47)

# The RF amplifier is a fallback stage, never the starting point.  Each
# direction must exhaust its complete normal-gain search before restarting at
# the lowest candidate with the amplifier enabled.
RF_AMP_SEARCH_STATES: tuple[bool, ...] = (False, True)

# Runtime adaptation is slower than packet timing but fast enough to follow a
# walking test.  CRC-valid frames remain the RX oracle.
ADAPT_RX_SILENCE_S = 1.5
ADAPT_RX_DWELL_S = 0.5
ADAPT_RX_CLIP_FRACTION = 0.001
ADAPT_RX_CLIP_BLOCKS = 2
ADAPT_TX_TIMEOUTS_PER_STEP = 1
ADAPT_TX_SUCCESSES_PER_STEP_DOWN = 6


@dataclass(frozen=True)
class GainState:
    gain: int | tuple[int, int]
    rf_amp_enabled: bool


def _next_state(
    candidates: tuple[int, ...] | tuple[tuple[int, int], ...],
    current: int | tuple[int, int],
    rf_amp_enabled: bool,
) -> tuple[GainState, bool, bool]:
    """Return next state, whether amp fallback began, and whether max was held."""

    index = candidates.index(current)
    if index + 1 < len(candidates):
        return GainState(candidates[index + 1], rf_amp_enabled), False, False
    if not rf_amp_enabled:
        return GainState(candidates[0], True), True, False
    # Never wrap a marginal live link from maximum gain back to minimum.  Hold
    # the strongest state until evidence-backed successes permit backoff.
    return GainState(candidates[-1], True), False, True


def tx_search_attempt_budget(
    current: int, rf_amp_enabled: bool, base_attempts: int
) -> int:
    """Cover every remaining stronger TX state in one operator command."""

    if base_attempts < 1:
        raise ValueError("base_attempts must be at least one")
    states = tuple(
        (gain, amp)
        for amp in RF_AMP_SEARCH_STATES
        for gain in TX_CANDIDATES
    )
    index = states.index((current, rf_amp_enabled))
    attempts_through_maximum = len(states) - index
    return max(base_attempts, attempts_through_maximum)


def _previous_state(
    candidates: tuple[int, ...] | tuple[tuple[int, int], ...],
    current: int | tuple[int, int],
    rf_amp_enabled: bool,
) -> GainState:
    """Return one lower state for overload recovery without crossing upward."""

    index = candidates.index(current)
    if index > 0:
        return GainState(candidates[index - 1], rf_amp_enabled)
    if rf_amp_enabled:
        return GainState(candidates[0], False)
    return GainState(candidates[0], False)


def next_rx_state(
    current: tuple[int, int], rf_amp_enabled: bool
) -> tuple[GainState, bool, bool]:
    return _next_state(RX_CANDIDATES, current, rf_amp_enabled)


def previous_rx_state(
    current: tuple[int, int], rf_amp_enabled: bool
) -> GainState:
    return _previous_state(RX_CANDIDATES, current, rf_amp_enabled)


def next_tx_state(
    current: int, rf_amp_enabled: bool
) -> tuple[GainState, bool, bool]:
    return _next_state(TX_CANDIDATES, current, rf_amp_enabled)


def previous_tx_state(current: int, rf_amp_enabled: bool) -> GainState:
    return _previous_state(TX_CANDIDATES, current, rf_amp_enabled)


@dataclass(frozen=True)
class RxWindow:
    lna_gain_db: int
    vga_gain_db: int
    valid_frames: int
    clipped_samples: int
    dropped_blocks: int
    rf_amp_enabled: bool = False

    @property
    def passed(self) -> bool:
        return (
            self.valid_frames >= 1
            and self.clipped_samples == 0
            and self.dropped_blocks == 0
        )


def select_rx_window(windows: list[RxWindow]) -> RxWindow | None:
    """Return the first clean CRC-valid preset from policy-ordered windows."""

    return next((window for window in windows if window.passed), None)


def validate_policy() -> None:
    if not RX_CANDIDATES:
        raise ValueError("RX calibration requires at least one candidate")
    if not TX_CANDIDATES or tuple(sorted(set(TX_CANDIDATES))) != TX_CANDIDATES:
        raise ValueError("TX candidates must be unique and increasing")
    if TX_CANDIDATES[0] < 0 or TX_CANDIDATES[-1] > 47:
        raise ValueError("automatic TX policy exceeds HackRF TX VGA limits")


validate_policy()


def crc16_ccitt(data: bytes) -> int:
    """CCSDS CRC-16/CCITT: polynomial 0x1021, init 0xffff."""

    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def build_fprime_ping_command(token: int, sequence: int) -> bytes:
    """Build the checked-in deployment's 23-byte MissionApp.PING TC frame.

    This byte contract is regression-locked against commands emitted by the
    current topology dictionary.  A dictionary/profile change must update the
    vector test before automatic calibration is allowed to transmit.
    """

    if not 0 <= token <= 0xFFFFFFFF:
        raise ValueError("PING token must fit in U32")
    if not 0 <= sequence <= 0xFF:
        raise ValueError("PING sequence must fit in one byte")
    frame = (
        bytes.fromhex("20 44 04 16 00 10 00 c0")
        + bytes((sequence,))
        + bytes.fromhex("00 09 00 00 10 00 60 01")
        + token.to_bytes(4, "big")
    )
    return frame + crc16_ccitt(frame).to_bytes(2, "big")
