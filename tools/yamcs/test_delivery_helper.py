"""Admission and independent receipt checks for the actual SendFile helper."""
from contextlib import redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

import stock_downlink as helper
from file_delivery_config import configure_file_delivery


class DeliveryHelperTests(unittest.TestCase):
    def test_receiver_admission_requires_live_idle_receiver(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            status = {"pid": os.getpid(), "heartbeat_unix": time.time(), "state": "ready"}
            path = directory / "receiver-status.json"
            path.write_text(json.dumps(status))
            helper.require_receiver_ready(directory)
            for change in ({"state": "receiving", "destination": "other.csv"},
                           {"state": "ready", "heartbeat_unix": time.time() - 30}):
                path.write_text(json.dumps(dict(status, **change)))
                with self.assertRaises(RuntimeError):
                    helper.require_receiver_ready(directory)

    def test_stale_or_wrong_source_receipt_cannot_complete_new_request(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            receipt = {"state": "available_in_yamcs", "destination": "fresh.csv",
                       "source_path": "different.csv", "started_unix": time.time()}
            path = directory / "fresh.csv.receipt.json"
            path.write_text(json.dumps(receipt))
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                helper.await_local_receipt(directory, "source.csv", "fresh.csv", time.time(),
                                           1, 1, "http://127.0.0.1:8090", "test", {})

    def test_windows_receiver_admission_never_calls_os_kill_and_requires_positive_pid(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            path = directory / "receiver-status.json"
            status = {"pid": 1234, "heartbeat_unix": time.time(), "state": "ready"}
            path.write_text(json.dumps(status))
            with patch.object(helper.os, "name", "nt"), \
                    patch.object(helper.os, "kill", side_effect=AssertionError("Windows must not use os.kill")):
                self.assertEqual(helper.require_receiver_ready(directory)["pid"], 1234)
                for invalid_pid in (0, -1, True, 1.5, "1234"):
                    path.write_text(json.dumps(dict(status, pid=invalid_pid)))
                    with self.subTest(pid=invalid_pid), self.assertRaises(RuntimeError):
                        helper.require_receiver_ready(directory)

    def test_helper_uses_one_sendfile_and_checks_bucket_against_local_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = b"one,two\n3,4\n"
            oracle = root / "oracle.csv"
            oracle.write_bytes(data)
            receipt = {"state": "available_in_yamcs", "sha256": hashlib.sha256(data).hexdigest()}

            def received(*args):
                args[-1]["local_receipt"] = receipt
                return receipt

            with patch.object(helper, "require_receiver_ready"), \
                    patch.object(helper, "existing_object", return_value=False), \
                    patch.object(helper, "issue", return_value={"id": "actual-sendfile"}) as issued, \
                    patch.object(helper, "await_local_receipt", side_effect=received), \
                    patch.object(helper, "api_bytes", return_value=data), \
                    patch.object(helper, "api", side_effect=AssertionError("native transfer API must not be used")), \
                    redirect_stdout(io.StringIO()):
                result = helper.main(["--source-file", "onboard/source.csv", "--expected-file", str(oracle),
                                      "--destination", "fresh.csv", "--station", str(root),
                                      "--run-dir", str(root / "evidence")])
            self.assertEqual(result, 0)
            self.assertEqual(issued.call_count, 1)
            self.assertEqual(issued.call_args.args[-2:], (
                "FileHandling.fileDownlink.SendFile",
                {"sourceFileName": "onboard/source.csv", "destFileName": "fresh.csv"}))
            evidence = json.loads(next((root / "evidence").glob("*.json")).read_text())
            self.assertTrue(evidence["received"]["checks"]["local_file_sha256"])
            self.assertTrue(evidence["received"]["integrity_verified"])

    def test_integrity_mismatch_retains_bytes_and_reconciles_without_publication_retry(self):
        data = b"one,two\n3,4\n"
        wrong_crc = (helper.binascii.crc_hqx(data, 0xFFFF) + 1) & 0xFFFF
        for expectation, failed_check in ((["--expected-sha256", "0" * 64], "expected_sha256"),
                                           (["--expected-crc16", str(wrong_crc)], "expected_crc16_ccitt")):
            with self.subTest(failed_check=failed_check), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                spool = root / "received-files"
                spool.mkdir()
                (spool / "receiver-status.json").write_text(json.dumps({
                    "pid": os.getpid(), "heartbeat_unix": time.time(), "state": "ready"}))

                def receive_after_send(*args):
                    (spool / "fresh.csv").write_bytes(data)
                    (spool / "fresh.csv.receipt.json").write_text(json.dumps({
                        "state": "available_in_yamcs", "destination": "fresh.csv",
                        "source_path": "onboard/source.csv", "started_unix": time.time(),
                        "received_size": len(data), "sha256": hashlib.sha256(data).hexdigest()}))
                    return {"id": "one-sendfile"}

                output = io.StringIO()
                with patch.object(helper, "existing_object", return_value=False), \
                        patch.object(helper, "issue", side_effect=receive_after_send) as issued, \
                        patch.object(helper, "api_bytes", return_value=data), redirect_stdout(output):
                    result = helper.main(["--source-file", "onboard/source.csv", "--expected-size", str(len(data)),
                                          *expectation, "--destination", "fresh.csv", "--station", str(root),
                                          "--run-dir", str(root / "evidence")])
                self.assertEqual(result, 1)
                self.assertEqual(issued.call_count, 1, "Integrity failure must not trigger a spacecraft resend")
                evidence = json.loads(next((root / "evidence").glob("*.json")).read_text())
                self.assertEqual(evidence["status"], "FAIL")
                self.assertEqual(evidence["failure_kind"], "integrity_mismatch")
                self.assertFalse(evidence["received"]["integrity_verified"])
                self.assertEqual(evidence["failed_checks"], [failed_check])
                self.assertEqual(Path(evidence["received"]["saved_path"]).read_bytes(), data)
                self.assertEqual((spool / "fresh.csv").read_bytes(), data)
                self.assertIn("Reconcile catalog/source", evidence["next_action"])
                self.assertNotIn("--retry", evidence["next_action"])
                self.assertIn("do not republish or resend", evidence["next_action"])
                self.assertIn("FAIL: fresh.csv", output.getvalue())
                self.assertIn("Unverified", output.getvalue())
                self.assertIn("observed SHA-256 " + hashlib.sha256(data).hexdigest(), output.getvalue())
                self.assertNotIn("Verified", output.getvalue())

    def test_publication_failure_keeps_disk_only_retry_action(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            spool = root / "received-files"
            spool.mkdir()
            data = b"retained CSV\n"
            (spool / "receiver-status.json").write_text(json.dumps({
                "pid": os.getpid(), "heartbeat_unix": time.time(), "state": "ready"}))

            def receive_after_send(*args):
                (spool / "fresh.csv").write_bytes(data)
                (spool / "fresh.csv.receipt.json").write_text(json.dumps({
                    "state": "saved_locally", "publication_error": "HTTP 503",
                    "destination": "fresh.csv", "source_path": "onboard/source.csv",
                    "started_unix": time.time(), "received_size": len(data),
                    "sha256": hashlib.sha256(data).hexdigest()}))
                return {"id": "one-sendfile"}

            output = io.StringIO()
            with patch.object(helper, "existing_object", return_value=False), \
                    patch.object(helper, "issue", side_effect=receive_after_send) as issued, \
                    patch.object(helper, "api_bytes", side_effect=AssertionError("Publication has not completed")), \
                    redirect_stdout(output):
                result = helper.main(["--source-file", "onboard/source.csv", "--expected-size", str(len(data)),
                                      "--expected-sha256", hashlib.sha256(data).hexdigest(), "--destination", "fresh.csv",
                                      "--station", str(root), "--run-dir", str(root / "evidence")])
            self.assertEqual(result, 1)
            self.assertEqual(issued.call_count, 1)
            evidence = json.loads(next((root / "evidence").glob("*.json")).read_text())
            self.assertEqual(evidence["status"], "FAIL")
            self.assertIn("local_file_receiver.py", evidence["next_action"])
            self.assertIn("--retry", evidence["next_action"])
            self.assertNotIn("failure_kind", evidence)
            self.assertEqual((spool / "fresh.csv").read_bytes(), data)
            self.assertNotIn("Verified", output.getvalue())

    def test_destination_retains_acquisition_identity(self):
        name = helper.default_destination("science/daq_1113_run_7_product_23.csv", 23)
        self.assertTrue(name.startswith("daq_1113_run_7_product_23-"))
        self.assertTrue(name.endswith(".csv"))
        helper.source_bytes_limit(name, "destination")

    def test_configuration_preserves_larger_operator_limits(self):
        import yaml
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "etc").mkdir()
            config = {"services": [{"class": "org.yamcs.http.HttpServer", "args": {
                "port": 8123, "maxContentLength": 256 * 1024 * 1024}}],
                "buckets": [{"name": "fprimeFilesIn", "path": "operator-path",
                             "maxSize": 4 * 1024 * 1024 * 1024, "maxObjects": 6000}]}
            path = root / "etc" / "yamcs.yaml"
            path.write_text(yaml.safe_dump(config))
            configure_file_delivery(root)
            self.assertEqual(yaml.safe_load(path.read_text()), config)


if __name__ == "__main__":
    unittest.main()
