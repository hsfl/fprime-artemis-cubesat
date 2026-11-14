"""Atomic, restartable migration of the existing stock Yamcs configuration."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import yaml

import file_delivery_config as config


class FileDeliveryConfigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "etc").mkdir()
        self.global_path = self.root / "etc/yamcs.yaml"
        self.instance_path = self.root / "etc/yamcs.test.yaml"
        self.global_path.write_text("# operator configuration\n" + yaml.safe_dump({
            "services": [{"class": "org.yamcs.http.HttpServer", "args": {"port": 8123}}],
            "secretKey": "test-only-value", "instances": ["test"]}, sort_keys=False))
        self.instance_path.write_text(yaml.safe_dump({"dataLinks": [
            {"name": "UDP_TM_IN", "class": "org.yamcs.tctm.ccsds.UdpTmFrameLink", "port": 50000,
             "virtualChannels": [{"vcId": 1, "packetPreprocessorClassName": "operator.Preprocessor"}]},
            {"name": "UDP_TM_SPLIT_IN", "port": 50002}, {"name": "UDP_TC_OUT", "port": 50001}]}))

    def test_invalid_later_instance_or_dictionary_leaves_all_files_untouched(self):
        original = self.global_path.read_bytes()
        self.instance_path.write_text("dataLinks: [broken")
        with self.assertRaises(yaml.YAMLError):
            config.configure_file_delivery(self.root)
        self.assertEqual(self.global_path.read_bytes(), original)
        self.assertFalse(list((self.root / "etc").glob("*.before-local-files")))
        self.instance_path.write_text("dataLinks: []\n")
        dictionary = self.root / "dictionary.json"
        dictionary.write_text(json.dumps({"constants": []}))
        with self.assertRaises(ValueError):
            config.configure_file_delivery(self.root, dictionary)
        self.assertEqual(self.global_path.read_bytes(), original)

    def test_atomic_replacement_failure_keeps_valid_original_and_restart_completes(self):
        original = self.global_path.read_bytes()
        replace = os.replace
        def fail_active_replace(source, destination):
            if Path(destination) == self.global_path:
                raise OSError("injected replacement failure")
            return replace(source, destination)
        with patch.object(config.os, "replace", side_effect=fail_active_replace):
            with self.assertRaisesRegex(OSError, "replacement failure"):
                config.configure_file_delivery(self.root)
        self.assertEqual(self.global_path.read_bytes(), original)
        backup = self.global_path.with_name(self.global_path.name + ".before-local-files")
        self.assertEqual(backup.read_bytes(), original)
        self.assertFalse(list((self.root / "etc").glob("*.tmp")))
        config.configure_file_delivery(self.root)
        parsed = yaml.safe_load(self.global_path.read_text())
        self.assertEqual(parsed["secretKey"], "test-only-value")
        main = yaml.safe_load(self.instance_path.read_text())["dataLinks"][0]
        self.assertEqual(main["class"], "org.yamcs.tctm.UdpTmDataLink")
        self.assertEqual(main["packetPreprocessorClassName"], "operator.Preprocessor")

    def test_idempotent_migration_keeps_backup_and_operator_permissions(self):
        self.global_path.chmod(0o640)
        original = self.global_path.read_bytes()
        config.configure_file_delivery(self.root)
        modification = self.global_path.stat().st_mtime_ns
        backup = self.global_path.with_name(self.global_path.name + ".before-local-files")
        self.assertEqual(backup.read_bytes(), original)
        if os.name != "nt":
            self.assertEqual(self.global_path.stat().st_mode & 0o777, 0o640)
            self.assertEqual(backup.stat().st_mode & 0o777, 0o640)
        config.configure_file_delivery(self.root)
        self.assertEqual(self.global_path.stat().st_mtime_ns, modification)
        self.assertEqual(backup.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
