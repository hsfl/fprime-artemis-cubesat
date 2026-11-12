import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from artifacts import verify
from build import working_source_digest


class ArtifactPairTests(unittest.TestCase):
    def test_digest_tracks_templates_bindings_toolchain_and_build_adapter(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("fprime-artemis-core/Top/topology.fpp.in", "fprime-artemis-core/Top/packets.fppi.in",
                         "fprime-artemis-core/native/sensor.yaml", "fprime-artemis-core/config/header.h.in",
                         "twin/native-linux.cmake", "tools/cross/cmake/platform.cmake", "tools/twin/pdu_host.c",
                         "config/transport_constants.json"):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                before = working_source_digest(root)
                path.write_text("original")
                added = working_source_digest(root)
                self.assertNotEqual(before, added, name)
                path.write_text("changed")
                self.assertNotEqual(added, working_source_digest(root), name)
            before = working_source_digest(root)
            ignored = root / "fprime-artemis-core/build-native/cache.cpp"
            ignored.parent.mkdir()
            ignored.write_text("generated cache")
            self.assertEqual(before, working_source_digest(root))
            for name in ("venv/lib/site-packages/metadata.json", "sysroot/usr/include/generated.h", "verify/run.json"):
                ignored = root / "tools/cross" / name
                ignored.parent.mkdir(parents=True, exist_ok=True)
                ignored.write_text("local generated state")
                self.assertEqual(before, working_source_digest(root), name)

    def test_modified_dictionary_cannot_start_with_old_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "bin").mkdir()
            (root / "dict").mkdir()
            binary, dictionary = b"executable", b'{"commands":[]}'
            (root / "bin/flight-controller").write_bytes(binary)
            (root / "dict/topology.json").write_bytes(dictionary)
            data = {"role": "fc", "framework_sha": "framework", "binary_sha256": hashlib.sha256(binary).hexdigest(),
                    "dictionary_sha256": hashlib.sha256(dictionary).hexdigest()}
            (root / "manifest.json").write_text(json.dumps(data))
            self.assertEqual(verify("fc", root), data)
            (root / "dict/topology.json").write_bytes(b"modified")
            with self.assertRaisesRegex(RuntimeError, "Stale or modified"):
                verify("fc", root)


if __name__ == "__main__":
    unittest.main()
