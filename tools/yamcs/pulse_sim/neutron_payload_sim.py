#!/usr/bin/env python3
"""Deterministic synthetic pulse source for the board-free N2 Yamcs profile."""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import sys
from pathlib import Path
from typing import Any


SCHEMA = "n2-pulses-v1"
DEFAULT_SEED = 12345
MAX_SEED = (1 << 32) - 1
MAX_DURATION_SECONDS = 120
SAMPLES_PER_EVENT = 64
SAMPLE_INTERVAL_NS = 10
ADC_MAX = (1 << 12) - 1
BASELINE_SAMPLES = 8
INTEGRATION_START = 8
TAIL_START = 20
INTEGRATION_END = 64


def parse_duration(value: str) -> int:
    try:
        duration = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("duration must be an integer from 1 to 120") from exc
    if not 1 <= duration <= MAX_DURATION_SECONDS:
        raise argparse.ArgumentTypeError("duration must be an integer from 1 to 120")
    return duration


def read_seed() -> int:
    raw_seed = os.environ.get("N2_LOCAL_PULSE_SEED")
    if raw_seed is None:
        return DEFAULT_SEED
    try:
        seed = int(raw_seed, 10)
    except ValueError as exc:
        raise ValueError("N2_LOCAL_PULSE_SEED must be an integer from 0 to 4294967295") from exc
    if not 0 <= seed <= MAX_SEED:
        raise ValueError("N2_LOCAL_PULSE_SEED must be an integer from 0 to 4294967295")
    return seed


def _adc_sample(value: float) -> int:
    return min(ADC_MAX, max(0, int(round(value))))


def _pulse_shape(sample_index: int, amplitude: float, slow_tail_weight: float) -> float:
    """Return a finite-rise pulse followed by a mixed 3/12-sample decay."""
    elapsed = sample_index - INTEGRATION_START
    if elapsed < 0:
        return 0.0

    rise_end = 3
    if elapsed <= rise_end:
        return amplitude * (1.0 - math.exp(-(elapsed + 1) / 1.5)) / (1.0 - math.exp(-(rise_end + 1) / 1.5))

    decay_samples = elapsed - rise_end
    fast = math.exp(-decay_samples / 3.0)
    slow = math.exp(-decay_samples / 12.0)
    return amplitude * ((1.0 - slow_tail_weight) * fast + slow_tail_weight * slow)


def _build_event(event_id: int, timestamp_ns: int, slow_tail_weight: float, rng: random.Random) -> dict[str, Any]:
    # A broad, positive peak distribution keeps the two pulse populations
    # measurable while baseline and sample noise provide synthetic background.
    amplitude = min(1050.0, max(180.0, rng.gauss(520.0, 155.0)))
    baseline = 1000.0 + rng.gauss(0.0, 7.0)
    samples = [
        _adc_sample(
            baseline
            + rng.gauss(0.0, 2.0)
            + _pulse_shape(index, amplitude, slow_tail_weight)
        )
        for index in range(SAMPLES_PER_EVENT)
    ]
    return {
        "id": event_id,
        "timestampNs": timestamp_ns,
        "channel": 0,
        "samples": samples,
    }


def build_product(duration_seconds: int, seed: int) -> dict[str, Any]:
    if not 1 <= duration_seconds <= MAX_DURATION_SECONDS:
        raise ValueError("duration must be an integer from 1 to 120")
    if not 0 <= seed <= MAX_SEED:
        raise ValueError("seed must be an integer from 0 to 4294967295")

    event_count = 2 * duration_seconds
    exposure_ns = duration_seconds * 1_000_000_000
    rng = random.Random(seed)
    timestamps = sorted(rng.sample(range(exposure_ns), event_count))
    tail_weights = [0.1] * (event_count // 2) + [0.45] * (event_count // 2)
    rng.shuffle(tail_weights)
    events = [
        _build_event(
            event_id=index + 1,
            timestamp_ns=timestamps[index],
            # Mix the balanced populations through the exposure without
            # serializing a pulse-class label into the product.
            slow_tail_weight=tail_weights[index],
            rng=rng,
        )
        for index in range(event_count)
    ]
    return {
        "schema": SCHEMA,
        "simulated": True,
        "durationSeconds": duration_seconds,
        "seed": seed,
        "sampleIntervalNs": SAMPLE_INTERVAL_NS,
        "adcBits": 12,
        "baselineSamples": BASELINE_SAMPLES,
        "integrationStart": INTEGRATION_START,
        "tailStart": TAIL_START,
        "integrationEnd": INTEGRATION_END,
        "events": events,
    }


def serialize_product(product: dict[str, Any]) -> bytes:
    return (json.dumps(product, sort_keys=True, separators=(",", ":")) + "\n").encode("ascii")


def write_unique_product(output_dir: Path, duration_seconds: int, seed: int, payload: bytes) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    stem = f"neutron_capture_{duration_seconds:03d}s_{seed:010d}"
    for suffix in range(10_000):
        name = f"{stem}.json" if suffix == 0 else f"{stem}_{suffix:03d}.json"
        output_path = output_dir / name
        try:
            descriptor = os.open(output_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        except FileExistsError:
            continue
        try:
            with os.fdopen(descriptor, "wb") as stream:
                written = stream.write(payload)
                if written != len(payload):
                    raise OSError(f"short write: wrote {written} of {len(payload)} bytes")
        except Exception:
            output_path.unlink(missing_ok=True)
            raise
        return output_path
    raise RuntimeError(f"could not allocate a unique capture filename in {output_dir}")


def emit_summary(summary: dict[str, Any], output_format: str) -> None:
    if output_format == "json":
        print(json.dumps(summary, sort_keys=True))
    else:
        for key in sorted(summary):
            print(f"{key}={summary[key]}")


def capture(args: argparse.Namespace) -> int:
    seed = read_seed()
    product = build_product(args.duration_seconds, seed)
    payload = serialize_product(product)
    output_path = write_unique_product(args.output_dir, args.duration_seconds, seed, payload)
    event_count = len(product["events"])
    emit_summary(
        {
            "duration_seconds": args.duration_seconds,
            "event_count": event_count,
            "exposure_window_seconds": args.duration_seconds,
            "output_bytes": len(payload),
            "output_path": str(output_path),
            "rows": event_count,
            "saa_rows": 0,
            "seed": seed,
            "total_counts": event_count,
        },
        args.output_format,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    default_root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    capture_parser = subparsers.add_parser("capture", help="Generate one bounded synthetic pulse exposure")
    # The F Prime driver converts its minute command to an exposure in seconds.
    # Keep seconds at this boundary to preserve the v1 product schema.
    capture_parser.add_argument("--duration-seconds", required=True, type=parse_duration)
    # These options preserve the simulator launch interface. Pulse generation
    # is intentionally independent of the old CSV dataset and cursor.
    capture_parser.add_argument("--dataset", default=default_root / "neutron_data.csv", type=Path)
    capture_parser.add_argument("--cursor", default=default_root / ".neutron_payload_cursor.json", type=Path)
    capture_parser.add_argument("--output-dir", default=default_root / "captures", type=Path)
    capture_parser.add_argument("--end-policy", choices=("wrap", "clamp", "error", "short"), default="wrap")
    capture_parser.add_argument("--format", dest="output_format", choices=("kv", "json"), default="kv")
    capture_parser.set_defaults(func=capture)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except Exception as exc:
        print(f"error={exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
