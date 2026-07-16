import importlib.util
import json
import pathlib
import sys
import tempfile
import threading
import time
import types
import unittest
from unittest import mock


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
UI_PATH = REPO_ROOT / "ground-station" / "c3m-payload-receiver-ui" / "c3m_payload_receiver_ui.py"
APP_JS_PATH = REPO_ROOT / "ground-station" / "c3m-payload-receiver-ui" / "static" / "app.js"
STYLES_PATH = REPO_ROOT / "ground-station" / "c3m-payload-receiver-ui" / "static" / "styles.css"
INDEX_PATH = REPO_ROOT / "ground-station" / "c3m-payload-receiver-ui" / "static" / "index.html"


class DummySerialException(Exception):
    pass


serial_package = types.ModuleType("serial")
serial_package.__path__ = []  # type: ignore[attr-defined]
serial_package.Serial = object  # type: ignore[attr-defined]
serial_package.SerialException = DummySerialException  # type: ignore[attr-defined]
serial_tools_package = types.ModuleType("serial.tools")
serial_tools_package.__path__ = []  # type: ignore[attr-defined]
serial_list_ports_module = types.ModuleType("serial.tools.list_ports")
serial_list_ports_module.comports = lambda: []  # type: ignore[attr-defined]
serial_tools_package.list_ports = serial_list_ports_module  # type: ignore[attr-defined]
serial_package.tools = serial_tools_package  # type: ignore[attr-defined]
sys.modules["serial"] = serial_package
sys.modules["serial.tools"] = serial_tools_package
sys.modules["serial.tools.list_ports"] = serial_list_ports_module


def load_ui_module():
    spec = importlib.util.spec_from_file_location("test_c3m_payload_receiver_ui_module", UI_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {UI_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


ui = load_ui_module()


def wait_for_status(controller, expected: str, timeout_s: float = 4.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        snapshot = controller.snapshot()
        if snapshot["current"]["status"] == expected:
            return snapshot
        time.sleep(0.01)
    snapshot = controller.snapshot()
    raise AssertionError(f"status did not become {expected}: {snapshot['current']}")


def fake_decode(fdp_path: pathlib.Path, outdir: pathlib.Path, dictionary: pathlib.Path | None):
    json_path = outdir / "payload.json"
    csv_path = outdir / "payload.csv"
    png_path = outdir / "payload.png"
    json_path.write_text('{"pixels": 19200}\n', encoding="utf-8")
    csv_path.write_text("1,2,3\n", encoding="utf-8")
    png_path.write_bytes(b"\x89PNG\r\n\x1a\n")
    return {
        "input": str(fdp_path),
        "dictionary": str(dictionary) if dictionary else None,
        "json": str(json_path),
        "csv": str(csv_path),
        "png": str(png_path),
        "width": 160,
        "height": 120,
        "pixels": 19200,
    }


def fake_partial_decode(
    fdp_path: pathlib.Path,
    outdir: pathlib.Path,
    missing_packet_indices: list[int],
    packet_data_bytes: int,
):
    result = fake_decode(fdp_path, outdir, None)
    result.update(
        {
            "partial": True,
            "valid_pixels": 19182,
            "missing_pixels": 18,
            "received_percent": 99.906,
            "min_c": 18.0,
            "max_c": 31.0,
            "mean_c": 22.0,
        }
    )
    return result


def receiver_event(
    kind: str,
    *,
    product_id: int,
    transfer_id: int,
    output_path: pathlib.Path | None = None,
):
    return ui.payload_receiver.ReceiverEvent(
        kind=kind,
        timestamp_s=time.time(),
        message=f"{kind} product={product_id} transfer={transfer_id}",
        port="test-channel-1",
        product_id=product_id,
        transfer_id=transfer_id,
        total_bytes=4,
        received_bytes=4 if kind == "transfer_saved" else 0,
        total_packets=1,
        received_packets=1 if kind == "transfer_saved" else 0,
        missing_packets=0,
        retry_rounds=0,
        expected_crc=0x1234,
        actual_crc=0x1234 if kind == "transfer_saved" else None,
        crc_ok=True if kind == "transfer_saved" else None,
        output_path=str(output_path) if output_path else None,
    )


class C3mPayloadReceiverUiTests(unittest.TestCase):
    def test_timing_targets_use_75_90_120_second_operator_bands(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp))
            self.assertEqual(controller.transfer_timeout_s, 120.0)
            controller.current.update(
                {
                    "status": "receiving",
                    "started_at_s": 1000.0,
                    "total_packets": 1100,
                    "received_packets": 500,
                }
            )
            for elapsed, expected in (
                (75.0, "nominal"),
                (75.1, "degraded"),
                (90.0, "degraded"),
                (119.9, "degraded"),
                (120.0, "delayed"),
            ):
                with self.subTest(elapsed=elapsed), mock.patch.object(
                    ui.time, "time", return_value=1000.0 + elapsed
                ):
                    self.assertEqual(
                        controller.snapshot()["current"]["timing_band"], expected
                    )

        app_js = APP_JS_PATH.read_text(encoding="utf-8")
        index_html = INDEX_PATH.read_text(encoding="utf-8")
        self.assertIn("Past the 75 s nominal target", app_js)
        self.assertIn("Longer than target", app_js)
        self.assertIn("120 s cutoff reached", app_js)
        self.assertIn("nominal ≤75 s · longer at 90 s · cutoff 120 s", index_html)

    def test_ready_after_complete_preserves_terminal_state_until_new_transfer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=fake_decode)
            controller.current.update(
                {
                    "status": "complete",
                    "connected": False,
                    "message": "Payload complete",
                    "product_id": 5,
                    "transfer_id": 5,
                    "total_bytes": 38480,
                    "received_bytes": 38480,
                    "total_packets": 1100,
                    "received_packets": 1100,
                    "missing_packets": 0,
                    "crc_ok": True,
                    "run_id": "c3m_completed_transfer_5",
                }
            )

            controller.on_receiver_event(receiver_event("ready", product_id=0, transfer_id=0))
            reconnected = controller.snapshot()["current"]

            self.assertEqual(reconnected["status"], "complete")
            self.assertTrue(reconnected["connected"])
            self.assertEqual(reconnected["message"], "Ready — last payload complete")
            self.assertEqual(reconnected["transfer_id"], 5)
            self.assertTrue(reconnected["crc_ok"])

            controller.on_receiver_event(receiver_event("transfer_started", product_id=6, transfer_id=6))
            next_transfer = controller.snapshot()["current"]
            self.assertEqual(next_transfer["status"], "receiving")
            self.assertEqual(next_transfer["product_id"], 6)
            self.assertEqual(next_transfer["transfer_id"], 6)
            self.assertIsNone(next_transfer["crc_ok"])

    def test_ready_preserves_other_terminal_states_but_resumes_incomplete_transfer(self) -> None:
        cases = (
            ({"partial": True, "crc_ok": False}, "partial", "Ready — last payload remains partial"),
            ({"partial": False, "crc_ok": False}, "failed", "Ready — last payload failed"),
        )
        for terminal_fields, expected_status, expected_message in cases:
            with self.subTest(expected_status=expected_status), tempfile.TemporaryDirectory() as tmp:
                controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=fake_decode)
                controller.current.update(
                    {
                        "status": "recovering",
                        "product_id": 5,
                        "transfer_id": 5,
                        "total_packets": 1100,
                        "received_packets": 1099 if terminal_fields["partial"] else 1100,
                        "run_id": f"c3m_{expected_status}_transfer_5",
                        **terminal_fields,
                    }
                )
                controller.on_receiver_event(receiver_event("ready", product_id=0, transfer_id=0))
                current = controller.snapshot()["current"]
                self.assertEqual(current["status"], expected_status)
                self.assertEqual(current["message"], expected_message)
                self.assertTrue(current["connected"])

        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=fake_decode)
            controller.current.update(
                {
                    "status": "recovering",
                    "product_id": 6,
                    "transfer_id": 6,
                    "total_packets": 1100,
                    "received_packets": 423,
                    "run_id": None,
                    "crc_ok": None,
                }
            )
            controller.on_receiver_event(receiver_event("ready", product_id=0, transfer_id=0))
            current = controller.snapshot()["current"]
            self.assertEqual(current["status"], "receiving")
            self.assertEqual(current["message"], "Resumed payload transfer")

    def test_archived_history_wires_csv_to_temperature_overlay(self) -> None:
        app_js = APP_JS_PATH.read_text(encoding="utf-8")
        styles = STYLES_PATH.read_text(encoding="utf-8")

        self.assertIn('id="archivedThermalImage"', app_js)
        self.assertIn('id="archivedThermalHover"', app_js)
        self.assertIn('csvUrl: selected.output_urls?.csv', app_js)
        self.assertIn('imageId: "archivedThermalImage"', app_js)
        self.assertIn('outputId: "archivedThermalHover"', app_js)
        self.assertIn("thermal-tooltip", app_js)
        self.assertIn(".thermal-tooltip", styles)

    def test_operator_cancel_controls_are_ground_only_and_confirm_satellite_continues(self) -> None:
        app_js = APP_JS_PATH.read_text(encoding="utf-8")
        index_html = INDEX_PATH.read_text(encoding="utf-8")
        server_source = UI_PATH.read_text(encoding="utf-8")

        self.assertIn('id="cancelTransferButton"', index_html)
        self.assertIn("Stop &amp; save partial", index_html)
        self.assertIn("The satellite continues its current transmission", index_html)
        self.assertIn("window.confirm", app_js)
        self.assertIn('postJson("/api/transfer/cancel")', app_js)
        self.assertIn('parsed.path == "/api/transfer/cancel"', server_source)

    def test_cancel_request_requires_active_header_and_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp))
            with self.assertRaisesRegex(ValueError, "no active payload transfer"):
                controller.cancel_current_transfer()

            controller.current.update(
                {
                    "status": "receiving",
                    "transfer_id": 9,
                    "total_packets": 100,
                }
            )
            self.assertTrue(controller.cancel_current_transfer())
            self.assertFalse(controller.cancel_current_transfer())
            current = controller.snapshot()["current"]
            self.assertEqual(current["status"], "cancelling")
            self.assertEqual(current["completion_reason"], "operator_cancelled")
            self.assertTrue(controller.cancel_event.is_set())

            controller.on_receiver_event(receiver_event("progress", product_id=9, transfer_id=9))
            self.assertEqual(controller.snapshot()["current"]["status"], "cancelling")

    def test_detects_third_triple_serial_port_only_when_unambiguous(self) -> None:
        rows = [
            {
                "device": f"/dev/cu.usbmodem11555330{suffix}",
                "description": "Triple Serial",
                "serial_number": "11555330",
                "location": "0-1",
                "likely_payload": False,
            }
            for suffix in (1, 3, 5)
        ]
        rows.append(
            {
                "device": "/dev/cu.usbmodem115565001",
                "description": "USB Serial",
                "serial_number": "11556500",
                "location": "2-1",
                "likely_payload": False,
            }
        )

        self.assertEqual(ui.detect_payload_port(rows), "/dev/cu.usbmodem115553305")
        self.assertIsNone(ui.detect_payload_port(rows[:2]))

    def test_stable_payload_identity_resolves_after_device_renumbering(self) -> None:
        before = [
            {
                "device": f"/dev/cu.usbmodem11555330{suffix}",
                "description": "Triple Serial",
                "serial_number": "11555330",
                "location": "0-1",
                "likely_payload": False,
            }
            for suffix in (1, 3, 5)
        ]
        identity = ui.stable_port_identity("/dev/cu.usbmodem115553305", before)
        self.assertEqual(identity["interface_ordinal"], 2)
        after = [
            {
                **row,
                "device": f"/dev/cu.usbmodem998877{suffix}",
            }
            for row, suffix in zip(before, (1, 3, 5), strict=True)
        ]
        self.assertEqual(
            ui.resolve_stable_port(identity, after),
            "/dev/cu.usbmodem9988775",
        )
        self.assertIsNone(ui.resolve_stable_port(identity, after[:2]))

    def test_reconnect_never_falls_back_to_stale_path_for_stable_identity(self) -> None:
        rows = [
            {
                "device": f"/dev/cu.usbmodem11555330{suffix}",
                "description": "Triple Serial",
                "serial_number": "11555330",
                "location": "0-1",
                "likely_payload": suffix == 5,
            }
            for suffix in (1, 3, 5)
        ]
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), port_rows_fn=lambda: [])
            controller.port_identity = ui.stable_port_identity(
                "/dev/cu.usbmodem115553305", rows
            )
            controller.current.update(
                {"port": "/dev/cu.usbmodem115553305", "status": "disconnected"}
            )
            with mock.patch.object(controller, "_start_worker") as start_worker:
                controller.reconnect()
            start_worker.assert_not_called()
            self.assertEqual(controller.current["status"], "recovering")
            self.assertFalse(controller.current["connected"])

    def test_checkpoint_rejection_is_operator_visible(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp))
            event = ui.payload_receiver.ReceiverEvent(
                kind="checkpoint_rejected",
                timestamp_s=time.time(),
                message="checkpoint rejected: stale",
                port="test-channel-1",
                product_id=0,
                transfer_id=None,
                total_bytes=0,
                received_bytes=0,
                total_packets=0,
                received_packets=0,
                missing_packets=0,
                retry_rounds=0,
                expected_crc=None,
                error_phase="checkpoint",
                error="checkpoint rejected: stale",
            )
            controller.on_receiver_event(event)
            snapshot = controller.snapshot()
            self.assertEqual(snapshot["current"]["status"], "warning")
            self.assertEqual(
                snapshot["current"]["failure_reason"], "checkpoint rejected: stale"
            )
            self.assertEqual(snapshot["logs"][-1]["level"], "warning")

    def test_ui_restart_loads_checkpoint_and_finishes_same_transfer(self) -> None:
        blob = bytes(index % 251 for index in range(ui.payload_receiver.DATA_BYTES * 20))
        rows = [
            {
                "device": f"/dev/cu.usbmodem998877{suffix}",
                "description": "Triple Serial",
                "serial_number": "11555330",
                "location": "0-1",
                "likely_payload": suffix == 5,
            }
            for suffix in (1, 3, 5)
        ]
        payload_port = "/dev/cu.usbmodem9988775"
        identity = ui.stable_port_identity(payload_port, rows)
        raw = ui.build_channel1_stream(blob, product_id=777, transfer_id=66)
        parser = ui.payload_receiver.PayloadReceiver(
            "parser", 115200, pathlib.Path("/tmp/unused"), 1.0
        )
        parser.rx_buffer += raw
        packets: list[bytes] = []
        while packet := parser.try_extract_packet():
            packets.append(packet)
        self.assertEqual(len(packets), 22)

        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            checkpoint = root / ".incoming" / "active_transfer_checkpoint"
            first = ui.payload_receiver.PayloadReceiver(
                payload_port,
                115200,
                root / "unused.fdp",
                1.0,
                checkpoint_dir=checkpoint,
                source_identity=identity,
            )
            first.handle_header(packets[0])
            for packet in packets[1:6]:
                first.handle_data(packet)

            resumed_serial = ui.ReplaySerial(b"".join(packets[6:]), chunk_size=37)
            controller = ui.ReceiverController(
                root,
                decode_fn=fake_decode,
                port_rows_fn=lambda: rows,
                serial_factory=lambda *_args, **_kwargs: resumed_serial,
            )
            try:
                controller.connect(payload_port)
                snapshot = wait_for_status(controller, "complete", timeout_s=5.0)
            finally:
                controller.stop()

            self.assertEqual(snapshot["current"]["product_id"], 777)
            self.assertEqual(snapshot["current"]["transfer_id"], 66)
            self.assertEqual(snapshot["current"]["received_packets"], 20)
            self.assertTrue(snapshot["current"]["crc_ok"])
            self.assertEqual(snapshot["history"][0]["result"], "complete")
            self.assertFalse(checkpoint.exists())

    def test_replay_completes_and_creates_history_run(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=fake_decode)
            try:
                controller.connect_replay(b"deterministic-fdp-bytes", delay_s=0.0)
                snapshot = wait_for_status(controller, "complete")
            finally:
                controller.stop()

            current = snapshot["current"]
            self.assertTrue(current["crc_ok"])
            self.assertEqual(current["received_packets"], current["total_packets"])
            self.assertEqual(current["received_bytes"], len(b"deterministic-fdp-bytes"))
            self.assertEqual(set(current["outputs"]), {"fdp", "json", "csv", "png"})
            self.assertEqual(len(snapshot["history"]), 1)
            run = snapshot["history"][0]
            self.assertEqual(run["result"], "complete")
            self.assertTrue(run["crc_ok"])
            self.assertEqual(run["decode"]["pixels"], 19200)
            run_json = pathlib.Path(tmp) / run["run_id"] / "run.json"
            self.assertEqual(json.loads(run_json.read_text(encoding="utf-8"))["sha256"], run["sha256"])

    def test_two_consecutive_replays_remain_browsable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=fake_decode)
            try:
                controller.connect_replay(b"first", delay_s=0.0)
                wait_for_status(controller, "complete")
                controller.connect_replay(b"second", delay_s=0.0)
                snapshot = wait_for_status(controller, "complete")
            finally:
                controller.stop()

            self.assertEqual(len(snapshot["history"]), 2)
            self.assertEqual(len({run["run_id"] for run in snapshot["history"]}), 2)
            self.assertTrue(all(run["output_urls"]["png"].startswith("/files/") for run in snapshot["history"]))

    def test_decode_runs_off_receiver_callback_and_cannot_replace_new_transfer(self) -> None:
        decode_started = threading.Event()
        release_decode = threading.Event()

        def slow_decode(fdp_path: pathlib.Path, outdir: pathlib.Path, dictionary: pathlib.Path | None):
            decode_started.set()
            self.assertTrue(release_decode.wait(timeout=2.0))
            return fake_decode(fdp_path, outdir, dictionary)

        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source = root / ".incoming" / "manual" / "Dp_test.fdp"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"test")
            controller = ui.ReceiverController(root, decode_fn=slow_decode)

            controller.on_receiver_event(receiver_event("transfer_started", product_id=1, transfer_id=1))
            started = time.monotonic()
            controller.on_receiver_event(
                receiver_event("transfer_saved", product_id=1, transfer_id=1, output_path=source)
            )
            self.assertLess(time.monotonic() - started, 0.2)
            self.assertTrue(decode_started.wait(timeout=1.0))

            controller.on_receiver_event(receiver_event("transfer_started", product_id=2, transfer_id=2))
            release_decode.set()
            deadline = time.monotonic() + 2.0
            while time.monotonic() < deadline and not controller.history():
                time.sleep(0.01)

            snapshot = controller.snapshot()
            self.assertEqual(len(snapshot["history"]), 1)
            self.assertEqual(snapshot["current"]["product_id"], 2)
            self.assertEqual(snapshot["current"]["status"], "receiving")

    def test_bad_crc_is_preserved_but_never_decoded(self) -> None:
        decode_calls: list[pathlib.Path] = []

        def must_not_decode(fdp_path: pathlib.Path, outdir: pathlib.Path, dictionary: pathlib.Path | None):
            decode_calls.append(fdp_path)
            return fake_decode(fdp_path, outdir, dictionary)

        blob = b"bad-crc-replay"
        expected_crc = ui.payload_receiver.crc16_ccitt(blob) ^ 0xFFFF
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=must_not_decode)
            try:
                controller.connect_replay(blob, delay_s=0.0, expected_crc=expected_crc)
                snapshot = wait_for_status(controller, "failed")
            finally:
                controller.stop()

            self.assertEqual(decode_calls, [])
            self.assertFalse(snapshot["current"]["crc_ok"])
            self.assertEqual(snapshot["history"][0]["result"], "crc_failed")
            self.assertIn("fdp", snapshot["history"][0]["output_urls"])
            self.assertNotIn("png", snapshot["history"][0]["output_urls"])

    def test_resolve_under_rejects_paths_outside_data(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            with self.assertRaises(ValueError):
                ui.resolve_under(root, "../outside")

    def test_stop_never_discards_a_worker_that_still_owns_serial(self) -> None:
        class StuckWorker:
            def is_alive(self) -> bool:
                return True

            def join(self, timeout: float) -> None:
                self.timeout = timeout

        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=fake_decode)
            stuck = StuckWorker()
            controller.thread = stuck  # type: ignore[assignment]

            with self.assertRaisesRegex(RuntimeError, "serial ownership is uncertain"):
                controller.stop()

            self.assertIs(controller.thread, stuck)
            self.assertEqual(controller.snapshot()["current"]["status"], "disconnected")

    def test_reconnect_forces_restart_during_active_delayed_transfer(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(pathlib.Path(tmp), decode_fn=fake_decode)
            controller.current.update(
                {
                    "status": "receiving",
                    "port": "test-channel-1",
                    "timing_band": "delayed",
                }
            )

            with mock.patch.object(controller, "_start_worker") as start_worker:
                controller.reconnect()

            start_worker.assert_called_once_with(port="test-channel-1", replay=None)

    def test_packet_loss_replay_finishes_as_honest_partial_product(self) -> None:
        blob = bytes(index % 251 for index in range(38480))
        with tempfile.TemporaryDirectory() as tmp:
            controller = ui.ReceiverController(
                pathlib.Path(tmp),
                decode_fn=fake_decode,
                partial_decode_fn=fake_partial_decode,
                transfer_timeout_s=2.0,
            )
            try:
                controller.connect_replay(blob, omit_packet_indices={100})
                snapshot = wait_for_status(controller, "partial")
            finally:
                controller.stop()

            current = snapshot["current"]
            self.assertTrue(current["partial"])
            self.assertFalse(current["crc_ok"])
            self.assertEqual(current["missing_packets"], 1)
            self.assertEqual(current["missing_packet_indices"], [100])
            self.assertIn("png", current["outputs"])
            run = snapshot["history"][0]
            self.assertEqual(run["result"], "partial")
            self.assertFalse(run["crc_ok"])
            self.assertEqual(run["missing_packet_indices"], [100])
            self.assertTrue(run["outputs"]["fdp"].endswith(".fdp.partial"))
            self.assertEqual(run["outputs"]["missing_map"], "missing_packets.json")

    def test_operator_cancel_replay_saves_partial_and_records_reason(self) -> None:
        blob = bytes(index % 251 for index in range(ui.payload_receiver.DATA_BYTES * 100))
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            controller = ui.ReceiverController(
                root,
                decode_fn=fake_decode,
                partial_decode_fn=fake_partial_decode,
            )
            try:
                controller.connect_replay(blob, delay_s=0.01)
                deadline = time.monotonic() + 3.0
                while time.monotonic() < deadline:
                    current = controller.snapshot()["current"]
                    if current["status"] == "receiving" and current["received_packets"] >= 3:
                        break
                    time.sleep(0.01)
                else:
                    self.fail("replay did not begin before cancel deadline")

                self.assertTrue(controller.cancel_current_transfer())
                snapshot = wait_for_status(controller, "partial", timeout_s=5.0)
            finally:
                controller.stop()

            current = snapshot["current"]
            self.assertEqual(current["message"], "Partial — stopped by operator")
            self.assertEqual(current["completion_reason"], "operator_cancelled")
            self.assertGreater(current["received_packets"], 0)
            self.assertLess(current["received_packets"], current["total_packets"])
            run = snapshot["history"][0]
            self.assertEqual(run["result"], "partial")
            self.assertEqual(run["completion_reason"], "operator_cancelled")
            self.assertIn("satellite transmission was not interrupted", run["timeout_reason"])
            run_json = root / run["run_id"] / "run.json"
            self.assertEqual(
                json.loads(run_json.read_text(encoding="utf-8"))["completion_reason"],
                "operator_cancelled",
            )


if __name__ == "__main__":
    unittest.main()
