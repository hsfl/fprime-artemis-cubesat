#!/usr/bin/env python3

from __future__ import annotations

import contextlib
import csv
import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import collect_hil_proof as collector
from rf_autocal import RX_CANDIDATES
from rf22_protocol import DEFAULT_PROFILE
from rf_safety import BASELINE_RF_PATH_LABEL


PAYLOAD_BYTES = b"science!"
PRODUCT_ID = 42
TRANSFER_ID = 7
TOTAL_PACKETS = 2


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def event_rows(overrides: dict[str, str] | None = None) -> list[list[str]]:
    descriptions = {
        "missionApp.Pong": "MissionApp pong token=43135 count=1",
        "missionApp.ModeChanged": "Mission mode changed to BASE",
        "sohApp.Snapshot": "SOH snapshot ready",
        "missionApp.CollectionScheduled": "Collection scheduled in 10s",
        "scienceApp.CollectionTriggered": (
            "Science collection triggered delay=10s"
        ),
        "scienceApp.ScienceProductReady": (
            f"Science product ready size={len(PAYLOAD_BYTES)}"
        ),
        "storageManager.ScienceStored": (
            f"Stored science product count=1 size={len(PAYLOAD_BYTES)}"
        ),
        "commsApp.DownlinkRequested": (
            f"Downlink requested for {len(PAYLOAD_BYTES)} bytes"
        ),
        "storageManager.DownlinkPrepared": (
            f"Prepared science downlink size={len(PAYLOAD_BYTES)}"
        ),
        "payloadDownlinkApp.PayloadDownlinkStarted": (
            f"Payload downlink started product={PRODUCT_ID} bytes={len(PAYLOAD_BYTES)} "
            f"packets={TOTAL_PACKETS}"
        ),
        "payloadDownlinkApp.PayloadDownlinkComplete": (
            f"Payload downlink complete transfer={TRANSFER_ID} "
            f"packetsSent={TOTAL_PACKETS}"
        ),
        "commsApp.DownlinkFinished": (
            f"Downlink finished for {len(PAYLOAD_BYTES)} bytes"
        ),
    }
    descriptions.update(overrides or {})
    names = [
        "cmdDisp.OpCodeDispatched",
        "cmdDisp.OpCodeCompleted",
        *descriptions,
    ]
    rows = []
    for index, name in enumerate(names):
        description = descriptions.get(name, f"command event {index}")
        rows.append(
            [
                f"2026-07-22T00:00:{index:02d}Z",
                "EVENT",
                name,
                "0",
                "0",
                description,
            ]
        )
    return rows


def write_event_rows(path: Path, rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerows(rows)


def write_events(path: Path, overrides: dict[str, str] | None = None) -> None:
    write_event_rows(path, event_rows(overrides))


def build_fixture(root: Path) -> dict[str, object]:
    supervisor_run = root / "supervisor-run"
    payload_dir = root / "payload-run"
    payload_run = payload_dir / "run.json"
    payload_file = payload_dir / "payload.fdp"
    event_log = supervisor_run / "gds" / "event.log"
    payload_file.parent.mkdir(parents=True, exist_ok=True)
    payload_file.write_bytes(PAYLOAD_BYTES)
    payload_sha = hashlib.sha256(PAYLOAD_BYTES).hexdigest()

    manifest = {
        "schema": 1,
        "state": "stopped",
        "network": DEFAULT_PROFILE.name,
        "hackrf_serial": "test-hackrf",
        "tx_enabled": True,
        "tx_mode": "ack",
        "tx_gain": 16,
        "tx_leading_ms": 100.0,
        "rx_lna_gain": 0,
        "rx_vga_gain": 8,
        "gain_control": "fixed",
        "rf_path_label": BASELINE_RF_PATH_LABEL,
        "tx_safety_confirmed": True,
        "elevated_tx_gain_confirmed": True,
        "rf_amp_enabled": False,
        "rx_rf_amp_enabled": False,
        "tx_rf_amp_enabled": False,
        "antenna_power_enabled": False,
        "git_branch": "codex/test",
        "git_commit": "a" * 40,
        "started_at_s": 1.0,
        "updated_at_s": 2.0,
        "children": {},
    }
    bridge = {
        "schema": 1,
        "radio_state": "stopped",
        "hackrf_mode": "closed",
        "network": DEFAULT_PROFILE.name,
        "network_id": DEFAULT_PROFILE.network_id,
        "serial": "test-hackrf",
        "rf_contract": {
            "downlink_header": DEFAULT_PROFILE.radio_header("downlink").hex(),
            "uplink_header": DEFAULT_PROFILE.radio_header("uplink").hex(),
        },
        "tx_mode": "ack",
        "tx_enabled": True,
        "tx_gain": 16,
        "tx_leading_ms": 100.0,
        "rx_lna_gain": 0,
        "rx_vga_gain": 8,
        "gain_control": "fixed",
        "rf_path_label": BASELINE_RF_PATH_LABEL,
        "tx_safety_confirmed": True,
        "elevated_tx_gain_confirmed": True,
        "rf_amp_enabled": False,
        "rx_rf_amp_enabled": False,
        "tx_rf_amp_enabled": False,
        "antenna_power_enabled": False,
        "rx_blocks": 100,
        "rx_bytes": 1_000_000,
        "rx_iq": {
            "sampled_complex_samples": 500_000,
            "clipped_complex_samples": 0,
            "clipped_fraction": 0.0,
            "last_block_clipped_fraction": 0.0,
            "max_block_clipped_fraction": 0.0,
            "peak_abs": 72,
            "last_block_complex_rms_dbfs": -12.5,
        },
        "rf22_frames": 30,
        "messages": {"0": 8, "1": 18},
        "message_bytes": {"0": 512, "1": len(PAYLOAD_BYTES)},
        # One channel-0 command succeeded after one timeout/retry. One
        # channel-1 repair message used the ACK-free degraded path.
        "tx_messages": {"0": 1, "1": 1},
        "tx_segments": 2,
        "tx_attempts": 3,
        "ack_received": 1,
        "ack_timeouts": 1,
        "degraded_segments": 1,
        "tx_failures": 0,
        "hackrf_rx_dropped_blocks": 0,
        "reassembly": {"completed": 26, "duplicates": 1, "drops": 0, "timeouts": 0},
        "tx_queue": {
            "messages": 0,
            "bytes": 0,
            "high_water_messages": 1,
            "high_water_bytes": 16,
            "rejections": 0,
        },
        "pty": {
            "0": {"backlog_bytes": 0, "downlink_bytes": 512, "uplink_bytes": 16},
            "1": {
                "backlog_bytes": 0,
                "downlink_bytes": len(PAYLOAD_BYTES),
                "uplink_bytes": 4,
            },
        },
        "reconnects": 0,
        "last_error": None,
    }
    payload = {
        "run_id": "hil-test",
        "result": "complete",
        "failure_reason": None,
        "product_id": PRODUCT_ID,
        "transfer_id": TRANSFER_ID,
        "crc_ok": True,
        "expected_crc": 0x1234,
        "actual_crc": 0x1234,
        "partial": False,
        "completion_reason": "complete",
        "timeout_reason": None,
        "total_bytes": len(PAYLOAD_BYTES),
        "received_bytes": len(PAYLOAD_BYTES),
        "total_packets": TOTAL_PACKETS,
        "received_packets": TOTAL_PACKETS,
        "missing_packets": 0,
        "retry_rounds": 1,
        "elapsed_seconds": 70.0,
        "decode": {"result": "complete", "input": "payload.fdp"},
        "outputs": {"fdp": "payload.fdp"},
        "sha256": payload_sha,
    }

    write_json(supervisor_run / "run-manifest.json", manifest)
    write_json(supervisor_run / "bridge-status.json", bridge)
    write_json(payload_run, payload)
    write_events(event_log)
    return {
        "supervisor_run": supervisor_run,
        "manifest_path": supervisor_run / "run-manifest.json",
        "bridge_path": supervisor_run / "bridge-status.json",
        "payload_run": payload_run,
        "payload_file": payload_file,
        "event_log": event_log,
        "manifest": manifest,
        "bridge": bridge,
        "payload": payload,
        "payload_sha": payload_sha,
    }


def collect(fixture: dict[str, object], **kwargs: object) -> dict[str, object]:
    return collector.collect_proof(
        fixture["supervisor_run"],
        fixture["payload_run"],
        **kwargs,
    )


def failed_required_gates(summary: dict[str, object]) -> set[str]:
    return {
        name
        for name, gate in summary["gates"].items()
        if gate["required"] and not gate["passed"]
    }


class PassingProofTests(unittest.TestCase):
    def test_ack_retry_and_one_channel_one_repair_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            summary = collect(
                fixture,
                pi_sha256=fixture["payload_sha"],
                pi_path="/data/payload.fdp",
            )

            self.assertEqual(summary["overall"]["status"], "pass")
            self.assertEqual(summary["overall"]["failures"], [])
            self.assertTrue(summary["gates"]["command_uplink"]["passed"])
            self.assertTrue(summary["gates"]["rf_safety_provenance"]["passed"])
            self.assertTrue(summary["gates"]["rf_gain_policy"]["passed"])
            self.assertTrue(summary["gates"]["rx_iq_no_clipping"]["passed"])
            ack = summary["bridge"]["command_ack"]
            self.assertEqual(ack["tx_attempts"], 3)
            self.assertEqual(ack["ack_received"], 1)
            self.assertEqual(ack["ack_timeouts"], 1)
            self.assertEqual(ack["degraded_segments"], 1)
            self.assertEqual(summary["payload"]["retry_rounds"], 1)
            self.assertTrue(summary["gates"]["ordered_demo_flow"]["passed"])
            self.assertTrue(summary["gates"]["payload_pi_sha"]["passed"])
            self.assertTrue(
                all(
                    check["passed"]
                    for check in summary["payload"]["event_consistency"].values()
                )
            )

    def test_completed_automatic_gain_selection_passes_policy_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            manifest = fixture["manifest"]
            bridge = fixture["bridge"]
            selected = {"tx_gain": 32, "rx_lna_gain": 0, "rx_vga_gain": 0}
            manifest.update(selected | {"gain_control": "automatic"})
            bridge.update(
                selected
                | {
                    "gain_control": "automatic",
                    "calibration": {
                        "enabled": True,
                        "state": "complete",
                        "selected": {
                            "tx_gain_db": 32,
                            "rx_lna_gain_db": 0,
                            "rx_vga_gain_db": 0,
                            "rx_rf_amp_enabled": False,
                            "tx_rf_amp_enabled": False,
                        },
                        "rx_windows": [
                            {
                                "lna_gain_db": 0,
                                "vga_gain_db": 0,
                                "valid_frames": 1,
                                "clipped_samples": 0,
                                "dropped_blocks": 0,
                                "rf_amp_enabled": False,
                                "passed": True,
                            }
                        ],
                        "tx_probes": [
                            {"gain_db": 24, "rf_amp_enabled": False, "pong": False},
                            {"gain_db": 32, "rf_amp_enabled": False, "pong": True},
                        ],
                    },
                }
            )
            write_json(fixture["manifest_path"], manifest)
            write_json(fixture["bridge_path"], bridge)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "pass")
            self.assertTrue(summary["gates"]["rf_gain_policy"]["passed"])

    def test_proven_rx_amp_fallback_passes_policy_gate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            manifest = fixture["manifest"]
            bridge = fixture["bridge"]
            amp_fields = {
                "rf_amp_enabled": True,
                "rx_rf_amp_enabled": True,
                "tx_rf_amp_enabled": False,
            }
            selected = {"tx_gain": 32, "rx_lna_gain": 0, "rx_vga_gain": 0}
            manifest.update(selected | amp_fields | {"gain_control": "automatic"})
            bridge.update(
                selected
                | amp_fields
                | {
                    "gain_control": "automatic",
                    "calibration": {
                        "enabled": True,
                        "state": "complete",
                        "selected": {
                            "tx_gain_db": 32,
                            "rx_lna_gain_db": 0,
                            "rx_vga_gain_db": 0,
                            "rx_rf_amp_enabled": True,
                            "tx_rf_amp_enabled": False,
                        },
                        "rx_windows": [
                            {
                                "lna_gain_db": lna,
                                "vga_gain_db": vga,
                                "rf_amp_enabled": False,
                                "passed": False,
                            }
                            for lna, vga in RX_CANDIDATES
                        ]
                        + [
                            {
                                "lna_gain_db": 0,
                                "vga_gain_db": 0,
                                "rf_amp_enabled": True,
                                "passed": True,
                            }
                        ],
                        "tx_probes": [
                            {
                                "gain_db": 32,
                                "rf_amp_enabled": False,
                                "pong": True,
                            }
                        ],
                    },
                }
            )
            write_json(fixture["manifest_path"], manifest)
            write_json(fixture["bridge_path"], bridge)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "pass")
            self.assertTrue(summary["gates"]["rf_gain_policy"]["passed"])


class RejectionTests(unittest.TestCase):
    def test_rf_safety_provenance_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            bridge = fixture["bridge"]
            bridge["rf_amp_enabled"] = True
            write_json(fixture["bridge_path"], bridge)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "fail")
            self.assertEqual(
                failed_required_gates(summary),
                {"rf_safety_provenance", "rf_gain_policy"},
            )

    def test_nonbaseline_rf_settings_are_rejected(self) -> None:
        cases = (
            ("tx gain", {"tx_gain": 47}),
            ("rx gain", {"rx_lna_gain": 16, "rx_vga_gain": 20}),
            ("path", {"rf_path_label": "different-antenna"}),
            ("settle lead", {"tx_leading_ms": 5.0}),
            ("gain control", {"gain_control": "automatic"}),
        )
        for label, updates in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory() as directory:
                fixture = build_fixture(Path(directory))
                manifest = fixture["manifest"]
                bridge = fixture["bridge"]
                manifest.update(updates)
                bridge.update(updates)
                write_json(fixture["manifest_path"], manifest)
                write_json(fixture["bridge_path"], bridge)

                summary = collect(fixture)

                self.assertEqual(summary["overall"]["status"], "fail")
                self.assertFalse(summary["gates"]["rf_gain_policy"]["passed"])
                self.assertTrue(summary["gates"]["rf_safety_provenance"]["passed"])

    def test_degraded_command_mode_cannot_pass_proof(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            manifest = fixture["manifest"]
            bridge = fixture["bridge"]
            manifest["tx_mode"] = "degraded-repeat"
            bridge["tx_mode"] = "degraded-repeat"
            write_json(fixture["manifest_path"], manifest)
            write_json(fixture["bridge_path"], bridge)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "fail")
            self.assertFalse(summary["gates"]["rf_gain_policy"]["passed"])
            self.assertFalse(summary["gates"]["command_uplink"]["passed"])

    def test_rx_iq_clipping_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            bridge = fixture["bridge"]
            bridge["rx_iq"]["clipped_complex_samples"] = 1
            bridge["rx_iq"]["clipped_fraction"] = 0.000002
            write_json(fixture["bridge_path"], bridge)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "fail")
            self.assertEqual(failed_required_gates(summary), {"rx_iq_no_clipping"})

    def test_impossible_ack_aggregate_counters_are_rejected(self) -> None:
        cases = (
            ("segment accounting", {"tx_segments": 5}),
            ("attempt accounting", {"tx_attempts": 2}),
        )
        for label, updates in cases:
            with self.subTest(case=label), tempfile.TemporaryDirectory() as directory:
                fixture = build_fixture(Path(directory))
                bridge = fixture["bridge"]
                bridge.update(updates)
                write_json(fixture["bridge_path"], bridge)

                summary = collect(fixture)

                self.assertEqual(summary["overall"]["status"], "fail")
                self.assertFalse(summary["gates"]["command_uplink"]["passed"])
                self.assertEqual(failed_required_gates(summary), {"command_uplink"})

    def test_negative_ack_aggregate_counter_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            bridge = fixture["bridge"]
            # These values satisfy the algebraic checks unless the collector
            # explicitly rejects the negative degraded-segment counter.
            bridge.update(
                {
                    "tx_segments": 1,
                    "tx_attempts": 2,
                    "ack_received": 2,
                    "ack_timeouts": 1,
                    "degraded_segments": -1,
                }
            )
            write_json(fixture["bridge_path"], bridge)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "fail")
            self.assertFalse(summary["gates"]["command_uplink"]["passed"])
            self.assertEqual(failed_required_gates(summary), {"command_uplink"})

    def test_decode_failed_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            payload = fixture["payload"]
            payload["decode"] = {"result": "decode_failed", "input": "payload.fdp"}
            write_json(fixture["payload_run"], payload)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "fail")
            self.assertFalse(summary["gates"]["payload_result_complete"]["passed"])
            self.assertFalse(summary["gates"]["payload_decode"]["passed"])

    def test_crc_mismatch_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            payload = fixture["payload"]
            payload["actual_crc"] = payload["expected_crc"] + 1
            write_json(fixture["payload_run"], payload)

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "fail")
            self.assertFalse(summary["gates"]["payload_crc"]["passed"])

    def test_local_and_pi_hash_mismatches_are_rejected(self) -> None:
        for mismatch in ("recorded", "pi"):
            with self.subTest(mismatch=mismatch), tempfile.TemporaryDirectory() as directory:
                fixture = build_fixture(Path(directory))
                kwargs = {}
                expected_gate = "payload_local_sha"
                if mismatch == "recorded":
                    payload = fixture["payload"]
                    payload["sha256"] = "0" * 64
                    write_json(fixture["payload_run"], payload)
                else:
                    kwargs["pi_sha256"] = "0" * 64
                    expected_gate = "payload_pi_sha"

                summary = collect(fixture, **kwargs)

                self.assertEqual(summary["overall"]["status"], "fail")
                self.assertFalse(summary["gates"][expected_gate]["passed"])

    def test_each_missing_required_artifact_is_rejected(self) -> None:
        cases = (
            ("manifest_path", "run_manifest"),
            ("bridge_path", "bridge_status"),
            ("payload_run", "payload_run"),
            ("event_log", "gds_event_log"),
            ("payload_file", "payload_file"),
        )
        for path_key, source_label in cases:
            with self.subTest(artifact=source_label), tempfile.TemporaryDirectory() as directory:
                fixture = build_fixture(Path(directory))
                fixture[path_key].unlink()

                summary = collect(fixture)

                self.assertEqual(summary["overall"]["status"], "fail")
                self.assertIn(
                    f"missing required artifact: {source_label}",
                    summary["overall"]["failures"],
                )


class EventProductConsistencyTests(unittest.TestCase):
    def test_fully_reversed_demo_chain_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            write_event_rows(fixture["event_log"], list(reversed(event_rows())))

            summary = collect(fixture)

            self.assertEqual(summary["overall"]["status"], "fail")
            self.assertFalse(summary["gates"]["ordered_demo_flow"]["passed"])
            self.assertEqual(failed_required_gates(summary), {"ordered_demo_flow"})
            self.assertTrue(
                all(
                    gate["passed"]
                    for gate in summary["events"]["gates"].values()
                    if gate["required"]
                )
            )

    def test_ordered_chain_binds_selected_product_and_transfer(self) -> None:
        cases = ("product", "transfer")
        for identity in cases:
            with self.subTest(identity=identity), tempfile.TemporaryDirectory() as directory:
                fixture = build_fixture(Path(directory))
                rows = event_rows()
                started_name = "payloadDownlinkApp.PayloadDownlinkStarted"
                complete_name = "payloadDownlinkApp.PayloadDownlinkComplete"
                started_index = next(
                    index for index, row in enumerate(rows) if row[2] == started_name
                )
                complete_index = next(
                    index for index, row in enumerate(rows) if row[2] == complete_name
                )

                if identity == "product":
                    exact_started = list(rows[started_index])
                    rows[started_index][5] = (
                        f"product={PRODUCT_ID + 1} bytes={len(PAYLOAD_BYTES)} "
                        f"packets={TOTAL_PACKETS}"
                    )
                    # Preserve an exact product event for the independent
                    # consistency gate, but put it after the only matching
                    # transfer completion so it cannot form a valid chain.
                    rows.insert(complete_index + 1, exact_started)
                else:
                    exact_complete = list(rows[complete_index])
                    rows[complete_index][5] = (
                        f"transfer={TRANSFER_ID + 1} packetsSent={TOTAL_PACKETS}"
                    )
                    # Preserve an exact transfer event before the selected
                    # product starts, where it cannot complete that chain.
                    rows.insert(started_index, exact_complete)

                write_event_rows(fixture["event_log"], rows)
                summary = collect(fixture)

                self.assertEqual(summary["overall"]["status"], "fail")
                self.assertFalse(summary["gates"]["ordered_demo_flow"]["passed"])
                self.assertTrue(
                    all(
                        check["passed"]
                        for check in summary["payload"]["event_consistency"].values()
                    )
                )

    def test_all_three_event_products_must_match_exactly(self) -> None:
        cases = (
            (
                "payloadDownlinkApp.PayloadDownlinkStarted",
                f"product={PRODUCT_ID + 1} bytes={len(PAYLOAD_BYTES)} packets={TOTAL_PACKETS}",
                "payload_event_started_matches_payload",
            ),
            (
                "payloadDownlinkApp.PayloadDownlinkComplete",
                f"transfer={TRANSFER_ID + 1} packetsSent={TOTAL_PACKETS}",
                "payload_event_complete_matches_payload",
            ),
            (
                "commsApp.DownlinkFinished",
                f"Downlink finished for {len(PAYLOAD_BYTES) + 1} bytes",
                "payload_event_finished_bytes_match_payload",
            ),
        )
        for event_name, description, gate_name in cases:
            with self.subTest(event=event_name), tempfile.TemporaryDirectory() as directory:
                fixture = build_fixture(Path(directory))
                write_events(fixture["event_log"], {event_name: description})

                summary = collect(fixture)

                self.assertEqual(summary["overall"]["status"], "fail")
                self.assertFalse(summary["gates"][gate_name]["passed"])
                consistency = summary["payload"]["event_consistency"]
                self.assertEqual(
                    sum(bool(check["passed"]) for check in consistency.values()),
                    2,
                )
                failed_check = consistency[gate_name.removeprefix("payload_event_")]
                self.assertIsNotNone(failed_check["observed"])
                self.assertNotEqual(
                    failed_check["observed"], failed_check["expected"]
                )


class OutputAndCliTests(unittest.TestCase):
    def test_atomic_write_preserves_old_file_when_replace_fails(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "proof.json"
            path.write_text("old\n", encoding="utf-8")

            with (
                mock.patch.object(
                    collector.os,
                    "replace",
                    side_effect=OSError("replace failed"),
                ),
                self.assertRaisesRegex(OSError, "replace failed"),
            ):
                collector._atomic_write_text(path, "new\n")

            self.assertEqual(path.read_text(encoding="utf-8"), "old\n")
            self.assertEqual(list(Path(directory).glob(".*.tmp")), [])

    def test_cli_returns_zero_for_pass_and_one_for_failed_proof(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fixture = build_fixture(Path(directory))
            argv = [
                str(fixture["supervisor_run"]),
                "--payload-run",
                str(fixture["payload_run"]),
            ]
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                self.assertEqual(collector.main(argv), 0)
            self.assertIn("HIL_PROOF_PASS", stdout.getvalue())
            for name in ("proof-summary.json", "proof-summary.md"):
                self.assertTrue((fixture["supervisor_run"] / name).is_file())
            self.assertEqual(
                json.loads(
                    (fixture["supervisor_run"] / "proof-summary.json").read_text(
                        encoding="utf-8"
                    )
                )["overall"]["status"],
                "pass",
            )

            payload = fixture["payload"]
            payload["sha256"] = "0" * 64
            write_json(fixture["payload_run"], payload)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                self.assertEqual(collector.main(argv), 1)
            self.assertIn("HIL_PROOF_FAIL", stdout.getvalue())
            self.assertEqual(
                json.loads(
                    (fixture["supervisor_run"] / "proof-summary.json").read_text(
                        encoding="utf-8"
                    )
                )["overall"]["status"],
                "fail",
            )
            self.assertEqual(
                list(fixture["supervisor_run"].glob(".proof-summary.*.tmp")),
                [],
            )

    def test_cli_returns_two_when_supervisor_directory_is_missing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "missing"
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                status = collector.main(
                    [str(missing), "--payload-run", str(missing / "run.json")]
                )
            self.assertEqual(status, 2)
            self.assertIn("does not exist", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
