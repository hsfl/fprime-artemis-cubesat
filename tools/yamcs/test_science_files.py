"""CSV operator tools preserve verified copies and require explicit retirement."""
import binascii
import hashlib
import json
from pathlib import Path
import tempfile
import time
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


if __name__ == "__main__":
    unittest.main()
