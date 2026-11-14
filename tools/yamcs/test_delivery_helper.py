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
