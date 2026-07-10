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


if __name__ == "__main__":
    unittest.main()
