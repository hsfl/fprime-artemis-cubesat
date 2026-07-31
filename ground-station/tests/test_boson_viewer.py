from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import struct
import sys
import tempfile
import types
import unittest
import zlib
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[2]
BOSON_PATH = REPO_ROOT / "ground-station" / "boson-viewer" / "boson_viewer.py"
UI_PATH = REPO_ROOT / "ground-station" / "c3m-payload-receiver-ui" / "c3m_payload_receiver_ui.py"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def load_ui_module():
    serial_package = types.ModuleType("serial")
    serial_package.__path__ = []  # type: ignore[attr-defined]
    serial_package.Serial = object  # type: ignore[attr-defined]
    serial_package.SerialException = type("DummySerialException", (Exception,), {})  # type: ignore[attr-defined]
    serial_tools_package = types.ModuleType("serial.tools")
    serial_tools_package.__path__ = []  # type: ignore[attr-defined]
    serial_list_ports = types.ModuleType("serial.tools.list_ports")
    serial_list_ports.comports = lambda: []  # type: ignore[attr-defined]
    serial_tools_package.list_ports = serial_list_ports  # type: ignore[attr-defined]
    serial_package.tools = serial_tools_package  # type: ignore[attr-defined]
    sys.modules["serial"] = serial_package
    sys.modules["serial.tools"] = serial_tools_package
    sys.modules["serial.tools.list_ports"] = serial_list_ports
    return load_module("test_standard_fdp_payload_receiver_ui", UI_PATH)


boson = load_module("test_standard_fdp_boson_viewer", BOSON_PATH)
ui = load_ui_module()


def deterministic_pixels(seed: int = 0) -> list[int]:
    return [(seed + index * 17 + index // 97) & 0xFFFF for index in range(boson.NUM_PIXELS)]


def build_fdp(container_id: int, record_id: int, pixels: list[int]) -> bytes:
    record = (
        struct.pack(">I", record_id)
        + struct.pack(">HBII", 2, 0, 1_700_000_000, 123_456)
        + b"".join(struct.pack(">H", pixel) for pixel in pixels)
    )
    header_without_hash = (
        struct.pack(">HII", boson.FW_PACKET_DP, container_id, 10)
        + struct.pack(">HBII", 2, 0, 1_700_000_000, 123_456)
        + struct.pack(">B", 0)
        + bytes(32)
        + struct.pack(">B", 0)
        + struct.pack(">I", len(record))
    )
    assert len(header_without_hash) == 59
    return (
        header_without_hash
        + struct.pack(">I", zlib.crc32(header_without_hash) & 0xFFFFFFFF)
        + record
        + struct.pack(">I", zlib.crc32(record) & 0xFFFFFFFF)
    )


def mock_decoded_pixels(pixels: list[int]) -> dict[str, object]:
    return {
        "Header": {},
        "Records": [
            {
                "Record": {"record_type_name": "Components.BosonImageRecordType"},
                "Data": {
                    "timeTag": {"seconds": 1_700_000_000, "useconds": 123_456},
                    "value": pixels,
                },
            }
        ],
    }


def receiver_event(ui_module, *, source: Path, product_id: int, expected_crc: int = 0x1234):
    return ui_module.payload_receiver.ReceiverEvent(
        kind="transfer_saved",
        timestamp_s=1_700_000_100.0,
        message="standard FDP product",
        port="test-channel-1",
        product_id=product_id,
        transfer_id=8,
        total_bytes=source.stat().st_size,
        received_bytes=source.stat().st_size,
        total_packets=1,
        received_packets=1,
        missing_packets=0,
        retry_rounds=0,
        expected_crc=expected_crc,
        actual_crc=expected_crc,
        crc_ok=True,
        output_path=str(source),
    )


class StandardFdpBosonViewerTests(unittest.TestCase):
    def test_complete_decode_uses_fprime_dp_and_preserves_raw_counts(self) -> None:
        pixels = deterministic_pixels(3)
        blob = build_fdp(boson.BOSON_CONTAINER_ID, boson.BOSON_CONTAINER_ID, pixels)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "capture.fdp"
            source.write_bytes(blob)
            dictionary = root / "dictionary.json"
            dictionary.write_text("{}\n", encoding="utf-8")

            def fake_fprime_dp(_source: Path, _dictionary: Path, output: Path) -> None:
                output.write_text(json.dumps(mock_decoded_pixels(pixels)), encoding="utf-8")

            args = types.SimpleNamespace(
                bin_file=source,
                dictionary=dictionary,
                outdir=root / "decoded",
                no_png=True,
                no_show=True,
            )
            with mock.patch.object(boson, "run_fprime_dp_decode", side_effect=fake_fprime_dp) as decode:
                summary = boson.decode_product(args)

            decode.assert_called_once_with(source.resolve(), dictionary, (root / "decoded").resolve() / "payload.json")
            self.assertEqual(summary["product"], "boson")
            self.assertEqual(summary["format"], "fdp")
            self.assertEqual(summary["units"], "raw_counts")
            self.assertEqual(summary["width"], 320)
            self.assertEqual(summary["height"], 256)
            self.assertEqual(summary["first_raw_counts"], pixels[0])
            self.assertEqual(
                summary["center_raw_counts"],
                pixels[(boson.HEIGHT // 2) * boson.WIDTH + (boson.WIDTH // 2)],
            )
            self.assertEqual(summary["min_raw_counts"], min(pixels))
            self.assertEqual(summary["max_raw_counts"], max(pixels))
            self.assertNotIn("min_c", summary)
            self.assertIn("Components.BosonImageRecordType", (root / "decoded" / "payload.json").read_text())

    def test_partial_decode_marks_missing_packet_bytes_unknown_without_shifting(self) -> None:
        pixels = deterministic_pixels(9)
        blob = bytearray(build_fdp(boson.BOSON_CONTAINER_ID, boson.BOSON_CONTAINER_ID, pixels))
        packet_data_bytes = 35
        target_pixel = 1234
        missing_packet = (boson.PIXEL_DATA_OFFSET + target_pixel * 2) // packet_data_bytes
        packet_start = missing_packet * packet_data_bytes
        packet_end = min(len(blob), packet_start + packet_data_bytes)
        blob[packet_start:packet_end] = b"\x00" * (packet_end - packet_start)

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "capture.fdp.partial"
            source.write_bytes(blob)
            summary = boson.decode_partial_product(
                source,
                root / "decoded",
                [missing_packet],
                packet_data_bytes,
            )
            payload = json.loads((root / "decoded" / "payload.json").read_text(encoding="utf-8"))

        self.assertIsNone(payload["pixels_raw_counts"][target_pixel])
        self.assertEqual(payload["pixels_raw_counts"][target_pixel + 100], pixels[target_pixel + 100])
        self.assertEqual(summary["units"], "raw_counts")
        self.assertEqual(summary["missing_packet_indices"], [missing_packet])
        self.assertGreater(summary["missing_pixels"], 0)
        self.assertNotIn("min_c", summary)

    def test_verified_internal_container_id_dispatch_ignores_channel_product_id(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            boson_path = root / "boson.fdp"
            boson_path.write_bytes(struct.pack(">HI", boson.FW_PACKET_DP, boson.BOSON_CONTAINER_ID) + b"payload")
            lepton_path = root / "lepton.fdp"
            lepton_path.write_bytes(struct.pack(">HI", ui.FW_PACKET_DP, ui.LEPTON_CONTAINER_ID) + b"payload")

            controller = ui.ReceiverController(root / "data")
            self.assertEqual(controller._classify_product(boson_path, 7), "boson")
            self.assertEqual(controller._classify_product(lepton_path, 999_999), "lepton")

    def test_unknown_internal_id_is_archived_as_fdp_without_decode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "incoming.fdp"
            source.write_bytes(build_fdp(0xDEADBEEF, 0xDEADBEEF, [1]))
            controller = ui.ReceiverController(root / "data")
            controller.transfer_sequence = 1
            event = receiver_event(ui, source=source, product_id=42)
            with contextlib.redirect_stdout(io.StringIO()):
                controller._finalize_transfer_locked({**vars(event), "started_at_s": event.timestamp_s}, None, 1)

            run = controller.history()[0]
            self.assertEqual(run["result"], "unknown_product")
            self.assertEqual(run["product_kind"], "unknown")
            self.assertIn("fdp", run["outputs"])
            self.assertTrue((root / "data" / run["run_id"] / "payload.fdp").is_file())
            self.assertNotIn("json", run["output_urls"])

    def test_lepton_default_behavior_is_celsius_and_uses_offset_78(self) -> None:
        self.assertEqual(ui.lepton_viewer.PIXEL_DATA_OFFSET, 78)
        pixels = [30_000] * ui.lepton_viewer.NUM_PIXELS
        blob = bytearray(build_fdp(ui.LEPTON_CONTAINER_ID, ui.LEPTON_CONTAINER_ID, pixels))
        packet_data_bytes = 35
        missing_packet = (ui.lepton_viewer.PIXEL_DATA_OFFSET + 2) // packet_data_bytes
        start = missing_packet * packet_data_bytes
        blob[start : start + packet_data_bytes] = b"\x00" * packet_data_bytes
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "lepton.fdp.partial"
            source.write_bytes(blob)
            summary = ui.lepton_viewer.decode_partial_product(source, root / "decoded", [missing_packet], packet_data_bytes)
        self.assertEqual(summary["width"], 160)
        self.assertEqual(summary["height"], 120)
        self.assertIn("min_c", summary)
        self.assertNotIn("min_raw_counts", summary)


if __name__ == "__main__":
    unittest.main()
