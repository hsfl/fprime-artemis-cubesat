import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import crc16
from science_ground import Publisher, validate_received


def manifest(blob):
    return {"product_id": 7, "transfer_id": 3, "bytes": len(blob), "crc16": crc16(blob),
            "sha256": hashlib.sha256(blob).hexdigest(), "crc_ok": True, "partial": False,
            "retry_rounds": 0}


class ScienceAdmissionTests(unittest.TestCase):
    def test_raw_fdp_and_receipt_handoff_without_analysis(self):
        blob = struct.pack(">HI", 5, 123) + b"opaque-product-bytes"
        objects = {}
        class Storage:
            def list_buckets(self): return [types.SimpleNamespace(name="artemis-science")]
            def upload_object(self, bucket, name, stream, **kwargs): objects[name] = stream.read()
            def download_object(self, bucket, name): return objects[name]
        class Client:
            def __init__(self, url):
                self.ctx = types.SimpleNamespace(session=types.SimpleNamespace(request=lambda *a, **kw: None))
            def get_storage_client(self): return Storage()
            def send_event(self, *args, **kwargs): pass
            def close(self): pass
        fake = types.ModuleType("yamcs.client")
        fake.YamcsClient = Client
        with tempfile.TemporaryDirectory() as temporary, patch.dict(sys.modules, {"yamcs.client": fake}):
            publisher = object.__new__(Publisher)
            publisher.url, publisher.run_id, publisher.dictionary_sha = "http://example.invalid", "run", "dict"
            publisher.directory = Path(temporary)
            (publisher.directory / "archived").mkdir()
            receipt = publisher.publish("key", blob, manifest(blob))
            self.assertEqual(objects[receipt["object"]], blob)
            self.assertEqual((publisher.directory / receipt["local_ground_file"]).read_bytes(), blob)
            self.assertEqual({p.suffix for p in (publisher.directory / "archived").iterdir()}, {".fdp", ".json"})
            self.assertNotIn("pixels", receipt)
            self.assertTrue(receipt["verified_ground_receipt"])

    def test_partial_bad_crc_and_wrong_product_never_pass(self):
        blob = struct.pack(">HI", 5, 123) + b"frame"
        good = manifest(blob)
        validate_received(blob, good)
        for override in ({"partial": True}, {"crc_ok": False}, {"crc16": 0}, {"bytes": 1}):
            with self.assertRaises(ValueError):
                validate_received(blob, {**good, **override})
        with self.assertRaises(ValueError):
            validate_received(b"not an FDP", manifest(b"not an FDP"))

    def test_yamcs_readback_tampering_rejected_before_receipt(self):
        blob = struct.pack(">HI", 5, 123) + b"frame"
        class Storage:
            def list_buckets(self): return [types.SimpleNamespace(name="artemis-science")]
            def upload_object(self, *args, **kwargs): pass
            def download_object(self, *args): return b"changed"
        class Client:
            def __init__(self, url):
                self.ctx = types.SimpleNamespace(session=types.SimpleNamespace(request=lambda *a, **kw: None))
            def get_storage_client(self): return Storage()
            def close(self): pass
        fake = types.ModuleType("yamcs.client")
        fake.YamcsClient = Client
        publisher = object.__new__(Publisher)
        publisher.url = "http://example.invalid"
        publisher.run_id = "test"
        publisher.dictionary_sha = "dictionary"
        with patch.dict(sys.modules, {"yamcs.client": fake}):
            with self.assertRaisesRegex(ValueError, "readback"):
                publisher.publish("key", blob, manifest(blob))


if __name__ == "__main__":
    unittest.main()
