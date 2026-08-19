#!/usr/bin/env python3

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import replay_pty


def run_args(input_path: Path, symlink: Path) -> list[str]:
    return [
        "replay_pty.py",
        str(input_path),
        "--message-bytes",
        "4",
        "--startup-delay",
        "0",
        "--interval",
        "0",
        "--symlink",
        str(symlink),
    ]


class ReplayLifecycleTests(unittest.TestCase):
    def test_existing_regular_file_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "input.bin"
            input_path.write_bytes(b"data")
            target = root / "serial"
            target.write_text("owned by user", encoding="utf-8")

            with (
                mock.patch.object(sys, "argv", run_args(input_path, target)),
                self.assertRaises(FileExistsError),
            ):
                replay_pty.main()

            self.assertEqual(target.read_text(encoding="utf-8"), "owned by user")

    def test_non_pty_symlink_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "input.bin"
            input_path.write_bytes(b"data")
            target = root / "serial"
            target.symlink_to("/tmp/not-a-pty")

            with (
                mock.patch.object(sys, "argv", run_args(input_path, target)),
                self.assertRaises(FileExistsError),
            ):
                replay_pty.main()

            self.assertTrue(target.is_symlink())
            self.assertEqual(os.readlink(target), "/tmp/not-a-pty")

    def test_external_replacement_symlink_survives_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "input.bin"
            input_path.write_bytes(b"data")
            target = root / "serial"
            sleep_calls = 0

            def replace_during_startup(_seconds: float) -> None:
                nonlocal sleep_calls
                sleep_calls += 1
                if sleep_calls == 1:
                    target.unlink()
                    target.symlink_to("/dev/tty-external")

            with (
                mock.patch.object(sys, "argv", run_args(input_path, target)),
                mock.patch.object(replay_pty.signal, "signal"),
                mock.patch.object(
                    replay_pty.time,
                    "sleep",
                    side_effect=replace_during_startup,
                ),
            ):
                self.assertEqual(replay_pty.main(), 0)

            self.assertTrue(target.is_symlink())
            self.assertEqual(os.readlink(target), "/dev/tty-external")

    def test_startup_failure_removes_owned_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "input.bin"
            input_path.write_bytes(b"data")
            target = root / "serial"

            with (
                mock.patch.object(sys, "argv", run_args(input_path, target)),
                mock.patch.object(replay_pty.signal, "signal"),
                mock.patch.object(
                    replay_pty.time,
                    "sleep",
                    side_effect=RuntimeError("startup failed"),
                ),
                self.assertRaisesRegex(RuntimeError, "startup failed"),
            ):
                replay_pty.main()

            self.assertFalse(target.exists())
            self.assertFalse(target.is_symlink())


if __name__ == "__main__":
    unittest.main()
