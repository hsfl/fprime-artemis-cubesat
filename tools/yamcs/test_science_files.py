"""CSV operator tools preserve verified copies and require explicit retirement."""
import binascii
from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import tempfile
import time
import shlex
import unittest
from unittest.mock import patch

import science_files


def fixture(root):
    spool = root / "spool"
    spool.mkdir()
    data = b"1,2\n3,4\n"
    source = spool / "daq_123_run_4_product_7.csv"
    source.write_bytes(data)
    digest = hashlib.sha256(data).hexdigest()
    receipt = {"state": "available_in_yamcs", "source_path": "onboard/product_7.csv",
               "destination": source.name, "expected_size": len(data), "received_size": len(data),
               "sha256": digest, "bucket_sha256": digest}
    path = spool / (source.name + ".receipt.json")
    path.write_text(json.dumps(receipt))
    return spool, source, path, receipt, data


class ScienceFileTests(unittest.TestCase):
    def test_archive_preview_then_verified_move(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            spool, source, path, receipt, data = fixture(root)
            result = science_files.archive(spool, root / "archive", False)
            self.assertEqual(result["files"], [source.name])
            self.assertTrue(source.exists())
            self.assertTrue(path.exists())
            self.assertFalse((root / "archive").exists())
            result = science_files.archive(spool, root / "archive", True)
            archived = Path(result["archive_directory"])
            self.assertEqual((archived / source.name).read_bytes(), data)
            self.assertEqual(json.loads((archived / path.name).read_text()), receipt)
            self.assertFalse(source.exists())
            self.assertFalse(path.exists())

    def test_unpublished_or_changed_files_remain_in_spool(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            spool, source, path, receipt, _ = fixture(root)
            receipt["state"] = "saved_locally"
            path.write_text(json.dumps(receipt))
            result = science_files.archive(spool, root / "archive", True)
            self.assertEqual(result["files"], [])
            self.assertTrue(source.exists())
            receipt["state"] = "available_in_yamcs"
            path.write_text(json.dumps(receipt))
            source.write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "does not match"):
                science_files.verified_csv(path)

    def test_archive_copy_failure_preserves_original(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            spool, source, path, _, data = fixture(root)
            with patch.object(science_files.shutil, "copyfileobj", side_effect=OSError("disk full")):
                with self.assertRaises(OSError):
                    science_files.archive(spool, root / "archive", True)
            self.assertEqual(source.read_bytes(), data)
            self.assertTrue(path.exists())

    def test_retirement_requires_matching_catalog_and_previews_by_default(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, source, path, receipt, data = fixture(root)
            selected = {"source_file": receipt["source_path"], "event": {"extra": {
                "productBytes": str(len(data)), "sourceCrc": str(binascii.crc_hqx(data, 0xFFFF))}}}
            event = {"type": "ArtemisRpiTeensyDeployment.storageManager.ProductRetirementPreview",
                     "receptionTime": "2026-11-13T00:00:01Z", "extra": {"productId": "7"}}
            with patch.object(science_files.downlink, "resolve_product", return_value=selected), \
                    patch.object(science_files.downlink, "issue", return_value={
                        "generationTime": "2026-11-13T00:00:00Z"}) as issued, \
                    patch.object(science_files.downlink, "archive_events", return_value=[event]):
                result = science_files.retire(path, 7, False, "http://127.0.0.1:8090", "test", "realtime")
                self.assertEqual(result["mode"], "preview")
                self.assertEqual(issued.call_args.args[-1], {"productId": 7, "confirm": 0})
                self.assertEqual(source.read_bytes(), data)
                selected["source_file"] = "onboard/different.csv"
                with self.assertRaisesRegex(ValueError, "differs"):
                    science_files.retire(path, 7, True, "http://127.0.0.1:8090", "test", "realtime")
                self.assertEqual(issued.call_count, 1)

    def test_archive_must_leave_active_spool(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            spool, *_ = fixture(root)
            with self.assertRaises(ValueError):
                science_files.archive(spool, spool / "archive", True)

    def test_status_never_admits_a_stale_bridge(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "receiver-status.json").write_text(json.dumps({"pid": 1,
                "heartbeat_unix": time.time() - 60, "state": "ready", "file_admission_ready": True}))
            with patch.object(science_files, "flight_capacity", return_value={}):
                result = science_files.status(root, "http://127.0.0.1:8090", "test", "realtime")
            self.assertEqual(result["bridge"]["state"], "unavailable")
            self.assertFalse(result["bridge"]["file_admission_ready"])

    def test_lost_retirement_response_is_unknown_and_preserves_ground_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, source, path, receipt, data = fixture(root)
            selected = {"source_file": receipt["source_path"], "event": {"extra": {
                "productBytes": str(len(data)), "sourceCrc": str(binascii.crc_hqx(data, 0xFFFF))}}}
            with patch.object(science_files.downlink, "resolve_product", return_value=selected), \
                    patch.object(science_files.downlink, "issue", side_effect=OSError("lost response")) as issue:
                with self.assertRaisesRegex(science_files.RetirementUnknown, "may be retired"):
                    science_files.retire(path, 7, True, "http://127.0.0.1:8090", "test", "realtime")
                self.assertEqual(issue.call_count, 1)
            self.assertEqual(source.read_bytes(), data)
            self.assertTrue(path.exists())

    def test_human_status_shows_fault_counts_and_executable_disk_retry(self):
        with tempfile.TemporaryDirectory(prefix="science status ") as temporary:
            station = Path(temporary)
            receipt = str(station / "received-files" / "pending file.csv.receipt.json")
            report = {"bridge": {"state": "ready", "file_admission_ready": True,
                "saved_files": 3, "pending_publications": 1, "quarantined_receipts": 2},
                "flight_storage": {}, "bad_receipts": [{"receipt": "bad.receipt.json", "error": "invalid receipt"}],
                "csv_files": [{"state": "saved_locally", "destination": "pending file.csv", "received_size": 4,
                               "expected_size": 4, "retry_receipt": receipt, "bucket": "science archive"}]}
            output = io.StringIO()
            with patch.object(science_files, "status", return_value=report), redirect_stdout(output):
                self.assertEqual(science_files.main(["--station", str(station), "--url",
                    "http://127.0.0.1:8123", "status"]), 0)
            lines = output.getvalue().splitlines()
            self.assertIn("Spool: 3 saved | 1 pending publication | 2 quarantined receipts", lines)
            self.assertIn("Receipt fault: bad.receipt.json: invalid receipt", lines)
            command = next(line.split(": ", 1)[1] for line in lines if "Publish retry (ground only):" in line)
            # This suite runs on the macOS/Linux developer host; exercise paths with spaces.
            if science_files.os.name != "nt":
                arguments = shlex.split(command)
                self.assertEqual(arguments[-2:], ["--retry", receipt])
                self.assertEqual(arguments[arguments.index("--url") + 1], "http://127.0.0.1:8123")
                self.assertEqual(arguments[arguments.index("--bucket") + 1], "science archive")
                self.assertEqual(arguments[arguments.index("--station") + 1], str(station.resolve()))


if __name__ == "__main__":
    unittest.main()
