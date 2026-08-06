#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import numpy as np

from bridge_core import BridgeBackpressureError
from live_rx_bridge import HackRfGroundBridge
from rf_autocal import RX_CANDIDATES, RxWindow
from rf22_protocol import DEFAULT_PROFILE
from rf_safety import TxSafetyError


def bridge_args(
    directory: Path,
    *,
    max_tx_queue_messages: int = 2,
    max_tx_queue_bytes: int = 5,
) -> argparse.Namespace:
    return argparse.Namespace(
        network=DEFAULT_PROFILE.name,
        gds_symlink=directory / "gds-port",
        payload_symlink=directory / "payload-port",
        max_pty_backlog=1024,
        uplink_idle_ms=12.0,
        rx_overlap_ms=25.0,
        message_id_state=directory / "message-ids.json",
        serial="test",
        enable_tx=True,
        tx_safety_confirmed=True,
        allow_elevated_tx_gain=False,
        rf_path_label="unit-test-load",
        tx_mode="ack",
        tx_leading_ms=100.0,
        max_tx_queue_messages=max_tx_queue_messages,
        max_tx_queue_bytes=max_tx_queue_bytes,
        uplink_capture_dir=directory / "capture",
        metrics_file=directory / "metrics.json",
        rx_lna_gain=8,
        rx_vga_gain=8,
        tx_gain=0,
        rx_queue_blocks=2,
    )


class TxQueueTests(unittest.TestCase):
    def test_rx_amp_fallback_only_runs_after_normal_search_is_exhausted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = HackRfGroundBridge(bridge_args(Path(directory)))
            bridge._write_metrics = mock.MagicMock()
            bridge.log = mock.MagicMock()
            calls: list[tuple[int, int, bool]] = []

            def measure(lna: int, vga: int, amp: bool) -> RxWindow:
                calls.append((lna, vga, amp))
                passed = amp and (lna, vga) == RX_CANDIDATES[0]
                return RxWindow(lna, vga, int(passed), 0, 0, amp)

            bridge._measure_rx_candidate = mock.MagicMock(side_effect=measure)
            selected = bridge._calibrate_rx()

            self.assertTrue(selected.rf_amp_enabled)
            self.assertEqual(
                calls[: len(RX_CANDIDATES)],
                [(lna, vga, False) for lna, vga in RX_CANDIDATES],
            )
            self.assertEqual(calls[len(RX_CANDIDATES)], (0, 0, True))

    def test_rx_amp_is_not_tried_when_normal_gain_works(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = HackRfGroundBridge(bridge_args(Path(directory)))
            bridge._write_metrics = mock.MagicMock()
            bridge.log = mock.MagicMock()
            bridge._measure_rx_candidate = mock.MagicMock(
                return_value=RxWindow(0, 0, 1, 0, 0, False)
            )

            selected = bridge._calibrate_rx()

            self.assertFalse(selected.rf_amp_enabled)
            bridge._measure_rx_candidate.assert_called_once_with(0, 0, False)
            self.assertFalse(
                any(call.args[0] == "AUTO_RX_AMP_FALLBACK" for call in bridge.log.call_args_list)
            )

    def test_constructor_rejects_programmatic_tx_safety_bypass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            args = bridge_args(Path(directory))
            args.tx_safety_confirmed = False
            with self.assertRaises(TxSafetyError):
                HackRfGroundBridge(args)

    def test_constructor_rejects_degraded_channel_zero_mode(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            args = bridge_args(Path(directory))
            args.tx_mode = "degraded-repeat"
            with self.assertRaisesRegex(TxSafetyError, "fixed to 'ack'"):
                HackRfGroundBridge(args)

    def test_fifo_queue_tracks_bytes_and_high_water(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = HackRfGroundBridge(bridge_args(Path(directory)))
            bridge._enqueue_tx(0, b"abc")
            bridge._enqueue_tx(1, b"de")

            queue_metrics = bridge.metrics["tx_queue"]
            self.assertIsInstance(queue_metrics, dict)
            self.assertEqual(bridge.metrics["rx_lna_gain"], 8)
            self.assertEqual(bridge.metrics["rx_vga_gain"], 8)
            self.assertEqual(
                queue_metrics,
                {
                    "messages": 2,
                    "bytes": 5,
                    "max_messages": 2,
                    "max_bytes": 5,
                    "high_water_messages": 2,
                    "high_water_bytes": 5,
                    "rejections": 0,
                },
            )
            self.assertEqual(bridge._dequeue_tx(), (0, b"abc"))
            self.assertEqual(queue_metrics["messages"], 1)
            self.assertEqual(queue_metrics["bytes"], 2)
            self.assertEqual(queue_metrics["high_water_messages"], 2)
            self.assertEqual(queue_metrics["high_water_bytes"], 5)

    def test_iq_metrics_record_rail_clipping_and_headroom(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = HackRfGroundBridge(bridge_args(Path(directory)))
            block = np.array(
                [0, 0, 127, 0, -128, 1, 10, -10], dtype=np.int8
            ).tobytes()

            bridge._update_rx_iq_metrics(block)

            metrics = bridge.metrics["rx_iq"]
            self.assertEqual(metrics["sampled_complex_samples"], 4)
            self.assertEqual(metrics["clipped_complex_samples"], 2)
            self.assertEqual(metrics["clipped_fraction"], 0.5)
            self.assertEqual(metrics["last_block_clipped_fraction"], 0.5)
            self.assertEqual(metrics["max_block_clipped_fraction"], 0.5)
            self.assertEqual(metrics["peak_abs"], 128)
            self.assertIsInstance(metrics["last_block_complex_rms_dbfs"], float)

    def test_message_and_byte_overflow_fail_without_mutating_queue(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = HackRfGroundBridge(bridge_args(Path(directory)))
            bridge._enqueue_tx(0, b"abc")
            bridge._enqueue_tx(1, b"de")
            before = list(bridge.tx_queue)

            with self.assertRaisesRegex(BridgeBackpressureError, "TX queue rejected"):
                bridge._enqueue_tx(0, b"f")
            self.assertEqual(list(bridge.tx_queue), before)
            self.assertEqual(bridge.tx_queue_bytes, 5)
            self.assertEqual(bridge.metrics["tx_queue"]["rejections"], 1)

            byte_limited = HackRfGroundBridge(
                bridge_args(
                    Path(directory),
                    max_tx_queue_messages=3,
                    max_tx_queue_bytes=4,
                )
            )
            byte_limited._enqueue_tx(0, b"abc")
            with self.assertRaisesRegex(BridgeBackpressureError, "TX queue rejected"):
                byte_limited._enqueue_tx(1, b"de")
            self.assertEqual(list(byte_limited.tx_queue), [(0, b"abc")])
            self.assertEqual(byte_limited.tx_queue_bytes, 3)
            self.assertEqual(byte_limited.metrics["tx_queue"]["rejections"], 1)

    def test_overflow_is_a_fatal_run_result_persisted_in_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bridge = HackRfGroundBridge(
                bridge_args(root, max_tx_queue_messages=1)
            )
            bridge._open_runtime = mock.MagicMock()
            bridge.log = mock.MagicMock()

            def overflow() -> None:
                bridge._enqueue_tx(0, b"abc")
                bridge._enqueue_tx(1, b"de")

            bridge._connected_loop = mock.MagicMock(side_effect=overflow)
            fake_device = mock.MagicMock()
            fake_device.mode = "rx"
            fake_device.rx_dropped_blocks = 0
            device_context = mock.MagicMock()
            device_context.__enter__.return_value = fake_device

            with mock.patch("live_rx_bridge.HackRFDevice", return_value=device_context):
                self.assertEqual(bridge.run(), 1)

            self.assertEqual(bridge.metrics["radio_state"], "failed")
            self.assertIn("TX queue rejected", str(bridge.metrics["last_error"]))
            self.assertEqual(bridge.metrics["tx_queue"]["rejections"], 1)
            persisted = json.loads(
                (root / "metrics.json").read_text(encoding="utf-8")
            )
            self.assertEqual(persisted["radio_state"], "failed")
            self.assertEqual(persisted["tx_queue"]["rejections"], 1)


def partial_bridge(directory: Path) -> HackRfGroundBridge:
    bridge = HackRfGroundBridge.__new__(HackRfGroundBridge)
    bridge.args = SimpleNamespace(
        uplink_capture_dir=directory / "capture",
        gds_symlink=directory / "gds-port",
        payload_symlink=directory / "payload-port",
        metrics_file=directory / "metrics.json",
    )
    bridge.profile = SimpleNamespace(name="test")
    bridge.channels = {0: mock.MagicMock(), 1: mock.MagicMock()}
    bridge._capture_streams = {}
    bridge.metrics = {"radio_state": "starting", "last_error": None}
    bridge.log = mock.MagicMock()
    bridge._write_metrics = mock.MagicMock()
    return bridge


class PartialRuntimeCleanupTests(unittest.TestCase):
    def test_channel_open_failure_closes_every_channel(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = partial_bridge(Path(directory))
            bridge.channels[1].open.side_effect = OSError("second PTY failed")

            with self.assertRaisesRegex(OSError, "second PTY failed"):
                bridge.run()

            bridge.channels[0].close.assert_called_once_with()
            bridge.channels[1].close.assert_called_once_with()
            self.assertEqual(bridge.metrics["radio_state"], "failed")
            self.assertEqual(bridge.metrics["last_error"], "second PTY failed")
            bridge._write_metrics.assert_called_once_with(force=True)

    def test_partial_capture_open_closes_prior_stream_and_all_channels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = partial_bridge(Path(directory))
            first_stream = mock.MagicMock()
            with (
                mock.patch.object(
                    Path,
                    "open",
                    side_effect=[first_stream, OSError("second capture failed")],
                ),
                self.assertRaisesRegex(OSError, "second capture failed"),
            ):
                bridge.run()

            first_stream.close.assert_called_once_with()
            bridge.channels[0].close.assert_called_once_with()
            bridge.channels[1].close.assert_called_once_with()
            self.assertEqual(bridge._capture_streams, {})
            self.assertEqual(bridge.metrics["radio_state"], "failed")

    def test_cleanup_attempts_all_resources_before_reporting_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            bridge = partial_bridge(Path(directory))
            stream_zero = mock.MagicMock()
            stream_one = mock.MagicMock()
            stream_zero.close.side_effect = OSError("capture close failed")
            bridge._capture_streams = {0: stream_zero, 1: stream_one}
            bridge.channels[0].close.side_effect = OSError("PTY close failed")

            with self.assertRaisesRegex(RuntimeError, "runtime cleanup failed"):
                bridge._close_runtime()

            stream_zero.close.assert_called_once_with()
            stream_one.close.assert_called_once_with()
            bridge.channels[0].close.assert_called_once_with()
            bridge.channels[1].close.assert_called_once_with()
            bridge._write_metrics.assert_called_once_with(force=True)
            self.assertEqual(bridge.metrics["radio_state"], "cleanup_failed")


if __name__ == "__main__":
    unittest.main()
