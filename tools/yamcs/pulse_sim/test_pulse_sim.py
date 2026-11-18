from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from neutron_payload_sim import (
    MAX_SEED,
    build_product,
    read_seed,
    serialize_product,
)


SCRIPT = Path(__file__).with_name("neutron_payload_sim.py")


def tail_fraction(event: dict[str, object]) -> float:
    samples = event["samples"]
    assert isinstance(samples, list)
    baseline = sum(samples[:8]) / 8
    integrated = sum(samples[8:64]) - baseline * 56
    tail = sum(samples[20:64]) - baseline * 44
    return tail / integrated


class PulseProductTests(unittest.TestCase):
    def test_product_is_deterministic_and_within_contract(self) -> None:
        first = serialize_product(build_product(30, 12345))
        second = serialize_product(build_product(30, 12345))
        self.assertEqual(first, second)
        self.assertLess(len(first), 1_000_000)
        self.assertLess(len(serialize_product(build_product(120, 12345))), 1_000_000)

        product = json.loads(first)
        self.assertEqual(
            set(product),
            {
                "schema",
                "simulated",
                "durationSeconds",
                "seed",
                "sampleIntervalNs",
                "adcBits",
                "baselineSamples",
                "integrationStart",
                "tailStart",
                "integrationEnd",
                "events",
            },
        )
        self.assertEqual(product["schema"], "n2-pulses-v1")
        self.assertTrue(product["simulated"])
        self.assertEqual(product["durationSeconds"], 30)
        self.assertEqual(product["seed"], 12345)
        self.assertEqual(product["sampleIntervalNs"], 10)
        self.assertEqual(product["adcBits"], 12)
        self.assertEqual(product["baselineSamples"], 8)
        self.assertEqual(product["integrationStart"], 8)
        self.assertEqual(product["tailStart"], 20)
        self.assertEqual(product["integrationEnd"], 64)
        self.assertEqual(len(product["events"]), 60)

        events = product["events"]
        exposure_ns = 30 * 1_000_000_000
        self.assertEqual([event["id"] for event in events], list(range(1, 61)))
        timestamps = [event["timestampNs"] for event in events]
        self.assertEqual(timestamps, sorted(timestamps))
        self.assertTrue(all(0 <= timestamp < exposure_ns for timestamp in timestamps))
        for event in events:
            self.assertEqual(set(event), {"id", "timestampNs", "channel", "samples"})
            self.assertEqual(event["channel"], 0)
            self.assertEqual(len(event["samples"]), 64)
            self.assertTrue(all(type(value) is int and 0 <= value <= 4095 for value in event["samples"]))
            self.assertFalse({"truth", "label", "keV", "FOM"}.intersection(event))

    def test_short_and_long_tail_populations_are_interleaved_and_separable(self) -> None:
        product = build_product(30, 12345)
        ratios = [tail_fraction(event) for event in product["events"]]
        short_count = sum(ratio < 0.16 for ratio in ratios)
        long_count = sum(ratio > 0.20 for ratio in ratios)
        self.assertGreaterEqual(short_count, 25)
        self.assertGreaterEqual(long_count, 25)
        for offset in range(0, len(ratios), 10):
            exposure_slice = ratios[offset : offset + 10]
            self.assertTrue(any(ratio < 0.16 for ratio in exposure_slice))
            self.assertTrue(any(ratio > 0.20 for ratio in exposure_slice))

    def test_duration_and_seed_bounds(self) -> None:
        for duration in (0, 121):
            with self.assertRaises(ValueError):
                build_product(duration, 12345)
        for seed in (-1, MAX_SEED + 1):
            with self.assertRaises(ValueError):
                build_product(1, seed)

    def test_optional_seed_environment_value_is_bounded(self) -> None:
        with patch.dict(os.environ, {"N2_LOCAL_PULSE_SEED": "42"}):
            self.assertEqual(read_seed(), 42)
        for value in ("not-an-integer", "-1", str(MAX_SEED + 1)):
            with patch.dict(os.environ, {"N2_LOCAL_PULSE_SEED": value}):
                with self.assertRaises(ValueError):
                    read_seed()

    def test_cli_ignores_legacy_csv_inputs_and_writes_unique_json_products(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = Path(temporary_directory)
            output_dir = temporary_path / "captures"
            missing_dataset = temporary_path / "no-such-dataset.csv"
            cursor_path = temporary_path / "cursor.json"
            command = [
                sys.executable,
                str(SCRIPT),
                "capture",
                "--duration-seconds",
                "60",
                "--dataset",
                str(missing_dataset),
                "--cursor",
                str(cursor_path),
                "--output-dir",
                str(output_dir),
                "--end-policy",
                "wrap",
                "--format",
                "kv",
            ]
            environment = os.environ.copy()
            environment.pop("N2_LOCAL_PULSE_SEED", None)
            outputs = [subprocess.run(command, capture_output=True, text=True, env=environment, check=True).stdout for _ in range(2)]
            summaries = [dict(line.split("=", 1) for line in output.splitlines()) for output in outputs]
            first_path = Path(summaries[0]["output_path"])
            second_path = Path(summaries[1]["output_path"])
            first_bytes = first_path.read_bytes()
            self.assertNotEqual(first_path, second_path)
            self.assertEqual(first_bytes, second_path.read_bytes())
            self.assertTrue(first_path.name.startswith("neutron_capture_"))
            self.assertTrue(first_path.name.endswith(".json"))
            self.assertEqual(int(summaries[0]["rows"]), 120)
            self.assertEqual(int(summaries[0]["total_counts"]), 120)
            self.assertEqual(int(summaries[0]["saa_rows"]), 0)
            self.assertEqual(int(summaries[0]["output_bytes"]), len(first_bytes))
            self.assertEqual(int(summaries[0]["exposure_window_seconds"]), 60)
            self.assertFalse(cursor_path.exists())
            self.assertLess(len(first_bytes), 1_000_000)


if __name__ == "__main__":
    unittest.main()
