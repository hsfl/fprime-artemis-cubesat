#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import tempfile
import time
import unittest
from pathlib import Path

from bridge_core import (
    BridgeAlreadyRunningError,
    BridgeBackpressureError,
    BridgeLock,
    UplinkBatcher,
    VirtualChannel,
    atomic_write_json,
)


class BridgeLockTests(unittest.TestCase):
    def test_stale_file_is_reused_but_live_owner_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bridge.lock"
            path.write_text("pid=stale\n", encoding="utf-8")
            first = BridgeLock(path)
            second = BridgeLock(path)
            first.acquire()
            try:
                self.assertIn(f"pid={os.getpid()}", path.read_text(encoding="utf-8"))
                with self.assertRaises(BridgeAlreadyRunningError):
                    second.acquire()
            finally:
                first.close()

            second.acquire()
            second.close()

    def test_same_lock_object_cannot_be_acquired_twice(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            lock = BridgeLock(Path(directory) / "bridge.lock")
            lock.acquire()
            try:
                with self.assertRaises(RuntimeError):
                    lock.acquire()
            finally:
                lock.close()


class BatcherTests(unittest.TestCase):
    def test_fragmented_writes_flush_after_idle(self) -> None:
        batcher = UplinkBatcher(max_message_bytes=220, idle_flush_s=0.012)
        batcher.feed(b"abc", 10.000)
        batcher.feed(b"def", 10.005)
        self.assertEqual(batcher.pop_ready(10.016), [])
        self.assertEqual(batcher.pop_ready(10.018), [b"abcdef"])

    def test_large_write_is_split_without_loss(self) -> None:
        batcher = UplinkBatcher(max_message_bytes=220, idle_flush_s=0.012)
        payload = bytes(index & 0xFF for index in range(500))
        batcher.feed(payload, 1.0)
        messages = batcher.pop_ready(1.0)
        messages += batcher.pop_ready(1.020)
        self.assertEqual([len(message) for message in messages], [220, 220, 60])
        self.assertEqual(b"".join(messages), payload)


class VirtualChannelTests(unittest.TestCase):
    def test_backlog_limit_must_be_positive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                VirtualChannel(
                    0,
                    Path(directory) / "channel",
                    max_backlog_bytes=0,
                )

    def test_bidirectional_pty_and_owned_symlink_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            symlink = Path(directory) / "channel"
            channel = VirtualChannel(0, symlink, max_backlog_bytes=64)
            channel.open()
            client = os.open(symlink, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            try:
                os.write(client, b"uplink")
                deadline = time.monotonic() + 0.2
                received = b""
                while not received and time.monotonic() < deadline:
                    received = channel.read_uplink()
                    time.sleep(0.001)
                self.assertEqual(received, b"uplink")

                channel.queue_downlink(b"downlink")
                channel.flush_downlink()
                deadline = time.monotonic() + 0.2
                returned = b""
                while not returned and time.monotonic() < deadline:
                    try:
                        returned = os.read(client, 64)
                    except BlockingIOError:
                        time.sleep(0.001)
                self.assertEqual(returned, b"downlink")
            finally:
                os.close(client)
                channel.close()
            self.assertFalse(symlink.exists())
            self.assertFalse(symlink.is_symlink())

    def test_backpressure_fails_instead_of_silently_corrupting_stream(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            channel = VirtualChannel(1, Path(directory) / "channel", max_backlog_bytes=4)
            channel.open()
            try:
                channel.queue_downlink(b"1234")
                with self.assertRaises(BridgeBackpressureError):
                    channel.queue_downlink(b"5")
            finally:
                channel.close()

    def test_non_symlink_target_is_never_replaced(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "channel"
            path.write_text("owned by user", encoding="utf-8")
            channel = VirtualChannel(0, path)
            with self.assertRaises(FileExistsError):
                channel.open()
            self.assertEqual(path.read_text(encoding="utf-8"), "owned by user")

    def test_stale_pty_link_is_replaced_but_external_replacement_is_preserved(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "channel"
            path.symlink_to("/dev/tty-stale")
            channel = VirtualChannel(0, path)
            channel.open()
            self.assertEqual(os.readlink(path), channel.slave_name)

            path.unlink()
            path.symlink_to("/dev/tty-external")
            channel.close()
            self.assertTrue(path.is_symlink())
            self.assertEqual(os.readlink(path), "/dev/tty-external")


class AtomicJsonTests(unittest.TestCase):
    def test_atomic_write_replaces_content_and_removes_temporary_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "status.json"
            path.write_text("stale", encoding="utf-8")
            atomic_write_json(path, {"state": "ready", "count": 2})
            self.assertEqual(
                json.loads(path.read_text(encoding="utf-8")),
                {"state": "ready", "count": 2},
            )
            self.assertEqual(list(Path(directory).glob(".*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
