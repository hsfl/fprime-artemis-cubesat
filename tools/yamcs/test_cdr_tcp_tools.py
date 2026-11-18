import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import MagicMock, patch

import cdr_comm_bridge
import cdr_tcp
import stock_downlink


class CdrTcpToolTests(unittest.TestCase):
    def test_dictionary_must_identify_cdr_tcp_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            dictionary = Path(temporary) / "dict.json"
            dictionary.write_text(json.dumps({"constants": [
                {"qualifiedName": "N2CdrTcp.ENABLED", "value": True},
            ]}))
            cdr_tcp.validate_marker(dictionary)
            dictionary.write_text(json.dumps({"constants": [
                {"qualifiedName": "N2CdrTcp.ENABLED", "value": False},
            ]}))
            with self.assertRaisesRegex(ValueError, "N2CdrTcp.ENABLED=true"):
                cdr_tcp.validate_marker(dictionary)

    def test_heap_validation_is_bounded_to_java_heap_syntax(self):
        self.assertEqual(cdr_tcp.validate_heap("1024m"), "1024m")
        with self.assertRaises(Exception):
            cdr_tcp.validate_heap("1024m; rm -rf /")

    def test_bridge_uses_dictionary_tm_constants(self):
        with tempfile.TemporaryDirectory() as temporary:
            dictionary = Path(temporary) / "dict.json"
            dictionary.write_text(json.dumps({"constants": [
                {"qualifiedName": "ComCfg.TmFrameFixedSize", "value": 1024},
                {"qualifiedName": "ComCfg.SpacecraftId", "value": 68},
            ]}))
            self.assertEqual(cdr_comm_bridge.dictionary_constants(dictionary), (1024, 68))

    def test_http_readiness_stops_when_startup_is_cancelled(self):
        process = MagicMock()
        process.poll.return_value = None
        cancelled = False

        with patch.object(cdr_tcp.request, "urlopen") as urlopen:
            self.assertFalse(cdr_tcp.http_ready(
                "http://127.0.0.1:8090", 90, process,
                cancel_requested=lambda: True,
            ))
            urlopen.assert_not_called()

            response = MagicMock()
            response.status = 200
            response.read.return_value = b'{"instances": []}'
            urlopen.return_value.__enter__.return_value = response

            def cancel_after_first_probe():
                nonlocal cancelled
                if cancelled:
                    return True
                cancelled = True
                return False

            self.assertFalse(cdr_tcp.http_ready(
                "http://127.0.0.1:8090", 90, process,
                cancel_requested=cancel_after_first_probe,
            ))
            self.assertEqual(urlopen.call_count, 1)

    def test_copied_yamcs_config_mirrors_downlinks_into_run_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repo"
            shutil.copytree(cdr_tcp.ROOT / "yamcs" / "config", root / "yamcs" / "config")
            run_dir = Path(temporary) / "cdr-run"
            run_dir.mkdir()
            with patch.object(cdr_tcp, "ROOT", root):
                config_dir = cdr_tcp.copy_yamcs_config(run_dir, "127.0.0.1", 8090)

            import yaml
            instance = yaml.safe_load((config_dir / "etc" / "yamcs.fprime-project.yaml").read_text())
            file_packet_services = [service for service in instance["services"]
                                    if service.get("class", "").endswith("FprimeFilePacketService")]
            self.assertEqual(len(file_packet_services), 1)
            self.assertEqual(file_packet_services[0]["args"]["downlinkMirrorDir"],
                             str(run_dir / "received-files"))
            global_config = yaml.safe_load((config_dir / "etc" / "yamcs.yaml").read_text())
            server = next(service for service in global_config["services"]
                          if service["class"].endswith("HttpServer"))
            self.assertGreaterEqual(server["args"]["maxContentLength"], 128 * 1024 * 1024)
            bucket = next(bucket for bucket in global_config["buckets"] if bucket["name"] == "fprimeFilesIn")
            self.assertGreaterEqual(bucket["maxSize"], 2 * 1024 * 1024 * 1024)
            main = next(link for link in instance["dataLinks"] if link["name"] == "UDP_TM_IN")
            self.assertEqual(main["class"], "org.yamcs.tctm.UdpTmDataLink")
            self.assertEqual(main["port"], 50000)
            self.assertEqual(main["maxLength"], 65535)
            self.assertNotIn("virtualChannels", main)

    def test_cleanup_kills_children_after_process_group_leader_exits(self):
        with tempfile.TemporaryDirectory() as temporary:
            pid_file = Path(temporary) / "child.pid"
            child_code = (
                "import os,pathlib,sys,time; "
                "pathlib.Path(sys.argv[1]).write_text(str(os.getpid())); time.sleep(120)"
            )
            leader_code = (
                "import subprocess,sys; "
                "subprocess.Popen([sys.executable, '-c', sys.argv[1], sys.argv[2]], "
                "stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)"
            )
            leader = subprocess.Popen(
                [sys.executable, "-c", leader_code, child_code, str(pid_file)],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, start_new_session=True,
            )
            try:
                leader.wait(timeout=5)
                deadline = time.monotonic() + 5
                while not pid_file.exists() and time.monotonic() < deadline:
                    time.sleep(0.02)
                self.assertTrue(pid_file.exists(), "child did not report its pid")
                child_pid = int(pid_file.read_text())
                self.assertEqual(os.getpgid(child_pid), leader.pid)

                cdr_tcp.stop_process_group(leader, grace_seconds=2)

                def child_is_running() -> bool:
                    result = subprocess.run(
                        ["ps", "-o", "stat=", "-p", str(child_pid)],
                        capture_output=True, text=True, check=False,
                    )
                    state = result.stdout.strip()
                    return bool(state) and not state.startswith("Z")

                self.assertFalse(child_is_running(), "child survived cleanup of its exited leader's process group")
            finally:
                cdr_tcp.stop_process_group(leader, grace_seconds=0.5)

    def test_remote_source_is_passed_as_relative_name(self):
        parsed = stock_downlink.build_parser().parse_args(["--source-file", "source/raw.csv"])
        self.assertEqual(parsed.source_file, "source/raw.csv")
        stock_downlink.source_bytes_limit(parsed.source_file, "source file name")

    def test_source_file_mode_requires_integrity_basis(self):
        args = stock_downlink.build_parser().parse_args(["--source-file", "source/raw.csv"])
        with self.assertRaisesRegex(ValueError, "requires --expected-file"):
            stock_downlink.integrity_expectations(args, None, None)

    def test_source_file_mode_checks_given_size_and_crc(self):
        source = b"sample payload bytes"
        crc = stock_downlink.binascii.crc_hqx(source, 0xFFFF)
        args = stock_downlink.build_parser().parse_args([
            "--source-file", "source/raw.csv", "--expected-size", str(len(source)),
            "--expected-crc16", hex(crc),
        ])
        expectations = stock_downlink.integrity_expectations(args, None, None)
        self.assertFalse(expectations["source_sha256_verified"])
        self.assertTrue(all(stock_downlink.verify_received(source, expectations).values()))
        self.assertFalse(all(stock_downlink.verify_received(b"other bytes", expectations).values()))

    def test_product_selection_derives_size_and_crc_without_claiming_source_sha(self):
        source = b"retained product bytes"
        selected = {"event": {"extra": {
            "productBytes": str(len(source)),
            "sourceCrc": str(stock_downlink.binascii.crc_hqx(source, 0xFFFF)),
        }}}
        args = stock_downlink.build_parser().parse_args(["--product-id", "17"])
        expectations = stock_downlink.integrity_expectations(args, selected, None)
        self.assertEqual(expectations["expected_size"], len(source))
        self.assertEqual(expectations["expected_crc16_ccitt"], stock_downlink.binascii.crc_hqx(source, 0xFFFF))
        self.assertFalse(expectations["source_sha256_verified"])
        self.assertTrue(all(stock_downlink.verify_received(source, expectations).values()))

    def test_product_metadata_rejects_conflicting_cli_expectations(self):
        selected = {"event": {"extra": {"productBytes": "4", "sourceCrc": "7"}}}
        cases = (
            (["--product-id", "17", "--expected-size", "5"], "conflicts with selected product size"),
            (["--product-id", "17", "--expected-crc16", "8"], "conflicts with selected product CRC"),
        )
        for options, message in cases:
            with self.subTest(options=options):
                args = stock_downlink.build_parser().parse_args(options)
                with self.assertRaisesRegex(ValueError, message):
                    stock_downlink.integrity_expectations(args, selected, None)

    def test_expected_file_is_a_local_sha_oracle(self):
        source = b"sample payload bytes"
        args = stock_downlink.build_parser().parse_args(["--source-file", "source/raw.csv"])
        expectations = stock_downlink.integrity_expectations(args, None, source)
        self.assertTrue(expectations["source_sha256_verified"])
        self.assertEqual(expectations["expected_sha256"], stock_downlink.hashlib.sha256(source).hexdigest())
        self.assertTrue(all(stock_downlink.verify_received(source, expectations).values()))

    def test_downlink_errors_must_match_time_and_file_identity(self):
        released = stock_downlink.stamp("2026-10-20T12:00:00Z")
        event = {
            "type": "ArtemisRpiTeensyDeployment.fileDownlink.FileOpenError",
            "receptionTime": "2026-10-20T12:00:01Z",
            "extra": {"fileName": "source/raw.csv"},
        }
        self.assertTrue(stock_downlink.event_matches(event, "source/raw.csv", "out.csv", released))
        self.assertFalse(stock_downlink.event_matches(event, "another.csv", "out.csv", released))
        event["receptionTime"] = "2026-10-20T11:59:59Z"
        self.assertFalse(stock_downlink.event_matches(event, "source/raw.csv", "out.csv", released))

    def test_generated_destination_is_unique_and_fits_dictionary_limit(self):
        source = "source/raw.csv"
        first = stock_downlink.default_destination(source)
        second = stock_downlink.default_destination(source)
        self.assertNotEqual(first, second)
        stock_downlink.source_bytes_limit(first, "destination file name")


if __name__ == "__main__":
    unittest.main()
