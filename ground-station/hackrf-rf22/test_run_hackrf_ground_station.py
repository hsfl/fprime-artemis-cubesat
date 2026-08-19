#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import run_hackrf_ground_station as launcher


class PortPreflightTests(unittest.TestCase):
    def test_probe_reuses_released_socket_and_binds_all_interfaces(self) -> None:
        context = mock.MagicMock()
        stream = context.__enter__.return_value
        with (
            mock.patch.object(
                launcher.socket,
                "create_connection",
                side_effect=ConnectionRefusedError,
            ),
            mock.patch.object(launcher.socket, "socket", return_value=context),
        ):
            self.assertTrue(launcher.port_is_available(5057))
        stream.setsockopt.assert_called_once_with(
            launcher.socket.SOL_SOCKET,
            launcher.socket.SO_REUSEADDR,
            1,
        )
        stream.bind.assert_called_once_with(("", 5057))

    def test_probe_rejects_an_active_listener(self) -> None:
        connection = mock.MagicMock()
        with mock.patch.object(
            launcher.socket, "create_connection", return_value=connection
        ):
            self.assertFalse(launcher.port_is_available(5057))
        connection.__enter__.assert_called_once_with()

    def test_port_ranges_and_duplicates_are_rejected(self) -> None:
        for ports in ([0], [65_536], [5057, 5057]):
            with self.subTest(ports=ports), self.assertRaises(ValueError):
                launcher.validate_ports(list(ports))
        launcher.validate_ports([5057, 50057, 8064])


class RuntimeSelectionTests(unittest.TestCase):
    def test_bridge_only_mode_does_not_require_gds_payload_or_ports(self) -> None:
        args = argparse.Namespace(
            no_gds=True,
            no_payload_ui=True,
            gui_port=5057,
            tts_port=50057,
            payload_web_port=8064,
        )
        self.assertEqual(
            launcher.runtime_requirements(args),
            [launcher.HERE / ".venv" / "bin" / "python"],
        )
        self.assertEqual(launcher.selected_ports(args), [])

    def test_full_mode_selects_every_runtime_and_port(self) -> None:
        args = argparse.Namespace(
            no_gds=False,
            no_payload_ui=False,
            gui_port=5057,
            tts_port=50057,
            payload_web_port=8064,
        )
        self.assertEqual(
            launcher.runtime_requirements(args),
            [
                launcher.HERE / ".venv" / "bin" / "python",
                launcher.FPRIME_VENV / "bin" / "fprime-gds",
                launcher.FPRIME_VENV / "bin" / "python",
                launcher.PAYLOAD_UI,
            ],
        )
        self.assertEqual(launcher.selected_ports(args), [5057, 50057, 8064])


class StaleRuntimeCleanupTests(unittest.TestCase):
    def test_owned_runtime_includes_resources_and_descendants_only(self) -> None:
        table = {
            10: (1, str(launcher.HERE / "run_hackrf_ground_station.py")),
            11: (10, "python worker"),
            20: (1, "python stale serial reader"),
            21: (20, "python worker"),
            99: (1, "unrelated service"),
        }
        with (
            mock.patch.object(launcher, "_process_table", return_value=table),
            mock.patch.object(launcher, "_manifest_pids", return_value={10, 99}),
            mock.patch.object(launcher, "_lsof_pids", return_value={20}),
        ):
            self.assertEqual(
                launcher.owned_runtime_pids(Path("/unused"), [5057]),
                {10, 11, 20, 21},
            )

    def test_reap_terminates_owned_processes(self) -> None:
        with (
            mock.patch.object(launcher, "owned_runtime_pids", return_value={10, 11}),
            mock.patch.object(launcher, "_process_table", return_value={}),
            mock.patch.object(launcher.os, "kill") as kill,
        ):
            self.assertEqual(
                launcher.reap_owned_runtime(Path("/unused"), [5057]),
                [10, 11],
            )
        self.assertEqual(
            kill.call_args_list,
            [
                mock.call(11, launcher.signal.SIGTERM),
                mock.call(10, launcher.signal.SIGTERM),
            ],
        )


class TxSafetyCliTests(unittest.TestCase):
    def test_launcher_defaults_to_fixed_receive_only_baseline(self) -> None:
        args = launcher.apply_student_rf_baseline(
            launcher.build_parser().parse_args([])
        )
        self.assertTrue(args.no_tx)
        self.assertEqual(args.tx_gain, 0)
        self.assertEqual(args.rx_lna_gain, 0)
        self.assertEqual(args.rx_vga_gain, 8)
        self.assertFalse(args.tx_safety_confirmed)
        self.assertFalse(args.allow_elevated_tx_gain)
        self.assertEqual(
            args.rf_path_label, "pobady-433-3dbi-rg174-3m-magnetic-base"
        )
        self.assertEqual(args.tx_mode, "ack")
        self.assertEqual(args.tx_leading_ms, 100.0)
        self.assertTrue(args.auto_calibrate)
        self.assertFalse(args.rx_rf_amp_enabled)
        self.assertFalse(args.tx_rf_amp_enabled)

    def test_enable_tx_selects_the_only_qualified_configuration(self) -> None:
        args = launcher.apply_student_rf_baseline(
            launcher.build_parser().parse_args(
                ["--enable-tx", "--tx-safety-confirmed"]
            )
        )
        self.assertFalse(args.no_tx)
        self.assertTrue(args.tx_safety_confirmed)
        self.assertEqual(args.tx_gain, 16)
        self.assertEqual(args.rx_lna_gain, 0)
        self.assertEqual(args.rx_vga_gain, 8)
        self.assertEqual(args.tx_mode, "ack")
        self.assertEqual(
            args.rf_path_label, "pobady-433-3dbi-rg174-3m-magnetic-base"
        )
        self.assertTrue(args.allow_elevated_tx_gain)
        self.assertTrue(args.auto_calibrate)
        self.assertFalse(args.rx_rf_amp_enabled)
        self.assertFalse(args.tx_rf_amp_enabled)

    def test_student_launcher_exposes_no_rf_tuning_options(self) -> None:
        options = {
            option
            for action in launcher.build_parser()._actions
            for option in action.option_strings
        }
        for option in (
            "--network",
            "--tx-mode",
            "--tx-gain",
            "--rx-lna-gain",
            "--rx-vga-gain",
            "--allow-elevated-tx-gain",
            "--rf-path-label",
        ):
            with self.subTest(option=option):
                self.assertNotIn(option, options)


class ChildLifecycleTests(unittest.TestCase):
    def test_operator_surfaces_can_start_while_bridge_is_calibrating(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            supervisor = launcher.Supervisor.__new__(launcher.Supervisor)
            supervisor.args = SimpleNamespace(start_timeout=0.2)
            supervisor.stopping = False
            supervisor.bridge_metrics = root / "bridge-status.json"
            supervisor.bridge_metrics.write_text(
                '{"radio_state":"calibrating"}', encoding="utf-8"
            )
            supervisor.gds_symlink = root / "gds-port"
            supervisor.payload_symlink = root / "payload-port"
            supervisor.gds_symlink.symlink_to(root / "gds-pty")
            supervisor.payload_symlink.symlink_to(root / "payload-pty")
            process = mock.MagicMock(returncode=None)
            process.poll.return_value = None
            child = launcher.Child(
                "bridge", process, root / "bridge.log", mock.MagicMock()
            )

            supervisor._wait_bridge_runtime(child)

            process.poll.assert_called_once_with()

    def test_concurrent_http_wait_publishes_each_surface_when_ready(self) -> None:
        supervisor = launcher.Supervisor.__new__(launcher.Supervisor)
        supervisor.args = SimpleNamespace(start_timeout=0.2, no_open=False)
        supervisor.stopping = False
        gds_process = mock.MagicMock(returncode=None)
        gds_process.poll.return_value = None
        payload_process = mock.MagicMock(returncode=None)
        payload_process.poll.return_value = None
        gds = launcher.Child(
            "gds", gds_process, Path("gds.log"), mock.MagicMock()
        )
        payload = launcher.Child(
            "payload-ui",
            payload_process,
            Path("payload.log"),
            mock.MagicMock(),
        )

        with (
            mock.patch.object(launcher, "http_ready", return_value=True) as ready,
            mock.patch.object(launcher.webbrowser, "open") as open_browser,
        ):
            supervisor._wait_http_surfaces(
                [(gds, 5057, "GDS"), (payload, 8064, "PAYLOAD")]
            )

        self.assertEqual(ready.call_args_list, [mock.call(5057), mock.call(8064)])
        self.assertEqual(
            open_browser.call_args_list,
            [
                mock.call("http://127.0.0.1:5057"),
                mock.call("http://127.0.0.1:8064"),
            ],
        )

    def test_spawn_closes_log_when_process_creation_fails(self) -> None:
        supervisor = launcher.Supervisor.__new__(launcher.Supervisor)
        supervisor.run_dir = Path("/unused")
        supervisor.children = []
        log_stream = mock.MagicMock()
        with (
            mock.patch.object(Path, "open", return_value=log_stream),
            mock.patch.object(
                launcher.subprocess,
                "Popen",
                side_effect=OSError("cannot spawn"),
            ),
            self.assertRaises(OSError),
        ):
            supervisor._spawn("bridge", ["missing"], "bridge.log")
        log_stream.close.assert_called_once_with()
        self.assertEqual(supervisor.children, [])

    def test_stop_closes_log_even_when_wait_fails(self) -> None:
        process = mock.MagicMock(pid=123)
        process.poll.return_value = None
        process.wait.side_effect = RuntimeError("wait failed")
        log_stream = mock.MagicMock()
        child = launcher.Child("bridge", process, Path("bridge.log"), log_stream)
        with (
            mock.patch.object(launcher.os, "killpg"),
            self.assertRaises(RuntimeError),
        ):
            launcher.stop_child(child)
        log_stream.close.assert_called_once_with()

    def test_timeout_escalates_to_kill(self) -> None:
        process = mock.MagicMock(pid=123)
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired("bridge", 1), 0]
        child = launcher.Child(
            "bridge",
            process,
            Path("bridge.log"),
            mock.MagicMock(),
        )
        with mock.patch.object(launcher.os, "killpg") as killpg:
            launcher.stop_child(child, timeout_s=1)
        self.assertEqual(
            killpg.call_args_list,
            [
                mock.call(123, launcher.signal.SIGTERM),
                mock.call(123, launcher.signal.SIGKILL),
            ],
        )


class SupervisorCleanupTests(unittest.TestCase):
    def test_cleanup_attempts_every_child_and_records_failure_state(self) -> None:
        supervisor = launcher.Supervisor.__new__(launcher.Supervisor)
        supervisor.children = []
        for name in ("first", "second", "third"):
            child = mock.MagicMock()
            child.name = name
            supervisor.children.append(child)
        states: list[str] = []

        def write_manifest(state: str) -> None:
            states.append(state)
            if state == "stopping":
                raise OSError("manifest unavailable")

        supervisor._write_manifest = mock.MagicMock(side_effect=write_manifest)
        stopped: list[str] = []

        def stop(child: mock.MagicMock) -> None:
            stopped.append(child.name)
            if child.name == "second":
                raise RuntimeError("stop failed")

        with mock.patch.object(launcher, "stop_child", side_effect=stop):
            errors = supervisor._cleanup()

        self.assertEqual(stopped, ["third", "second", "first"])
        self.assertEqual(states, ["stopping", "cleanup_failed"])
        self.assertEqual(len(errors), 2)

    def test_clean_cleanup_preserves_whether_run_failed(self) -> None:
        for run_failed, final_state in ((False, "stopped"), (True, "failed")):
            with self.subTest(run_failed=run_failed):
                supervisor = launcher.Supervisor.__new__(launcher.Supervisor)
                supervisor.children = []
                supervisor._write_manifest = mock.MagicMock()
                self.assertEqual(
                    supervisor._cleanup(run_failed=run_failed),
                    [],
                )
                self.assertEqual(
                    supervisor._write_manifest.call_args_list,
                    [mock.call("stopping"), mock.call(final_state)],
                )


if __name__ == "__main__":
    unittest.main()
