"""Local file-delivery faults using stock wire packets, without hardware."""
from contextlib import contextmanager
import hashlib
import errno
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch
import binascii

from fprime_gds.common.communication.ccsds.chain import RawSpaceDataLinkFramerDeframer

import cdr_comm_bridge
from local_file_receiver import BucketPublisher, LocalFileReceiver, PublicationWorker, publication_retry_command


def checksum(data):
    padded = data + bytes((-len(data)) % 4)
    return sum(word[0] for word in struct.iter_unpack(">I", padded)) & 0xFFFFFFFF


def space_packet(inner, seq=0, apid=3):
    body = struct.pack(">H", apid) + inner
    return struct.pack(">HHH", apid, 0xC000 | (seq & 0x3FFF), len(body) - 1) + body


def start(data, destination="science.csv", source="onboard/product.csv"):
    source, destination = source.encode(), destination.encode()
    return space_packet(struct.pack(">BIIB", 0, 0, len(data), len(source)) + source +
                        bytes([len(destination)]) + destination)


def data_packet(seq, offset, data):
    return space_packet(struct.pack(">BIIH", 1, seq, offset, len(data)) + data, seq)


def end_packet(seq, data, *, corrupt=False):
    return space_packet(struct.pack(">BII", 2, seq, checksum(data) ^ int(corrupt)), seq)


def file_packets(data, destination="science.csv", chunk_size=513):
    yield start(data, destination)
    sequence = 1
    for offset in range(0, len(data), chunk_size):
        yield data_packet(sequence, offset, data[offset:offset + chunk_size])
        sequence += 1
    yield end_packet(sequence, data)


def tm_frames(packets, frame_size=128):
    """Independent TM generator with mixed packets and packet continuations."""
    capacity = frame_size - 8
    stream = b"".join(packets)
    starts, offset = [], 0
    for packet in packets:
        starts.append(offset)
        offset += len(packet)
    padding = (-len(stream)) % capacity
    if padding:
        if padding < 7:
            padding += capacity
        starts.append(len(stream))
        stream += struct.pack(">HHH", 0x7FF, 0xC000, padding - 7) + bytes(padding - 6)
    frames = []
    for count, offset in enumerate(range(0, len(stream), capacity)):
        begin = next((position - offset for position in starts if offset <= position < offset + capacity), 0x7FF)
        header = struct.pack(">HBBH", (68 << 4) | 2, count & 255, count & 255, 0x1800 | begin)
        frame = header + stream[offset:offset + capacity]
        frames.append(frame + struct.pack(">H", binascii.crc_hqx(frame, 0xFFFF)))
    return frames


@contextmanager
def object_server(*, max_upload=128 * 1024 * 1024, block_upload=None):
    state = {"objects": {}, "posts": 0, "max_upload": max_upload}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            body = state["objects"].get(self.path)
            self.send_response(404 if body is None else 200)
            self.end_headers()
            if body is not None:
                self.wfile.write(body)

        def do_POST(self):
            size = int(self.headers["Content-Length"])
            if size > state["max_upload"]:
                self.send_response(413)
                self.end_headers()
                return
            if block_upload:
                block_upload.wait(timeout=5)
            state["objects"][self.path] = self.rfile.read(size)
            state["posts"] += 1
            self.send_response(200)
            self.end_headers()

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_port}", state
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.receiver = LocalFileReceiver(self.root)
        self.addCleanup(self.receiver.close)

    def receipt(self, destination="science.csv"):
        return json.loads((self.root / (destination + ".receipt.json")).read_text())

    def receive_file(self, data, destination="science.csv"):
        for packet in file_packets(data, destination):
            self.assertTrue(self.receiver.receive(packet))
        return self.root / (destination + ".receipt.json")

    def test_valid_file_uses_disk_checksum_and_receipt(self):
        data = bytes(range(256)) * 33 + b"last"
        self.receive_file(data)
        self.assertEqual((self.root / "science.csv").read_bytes(), data)
        self.assertFalse((self.root / "science.csv.part").exists())
        receipt = self.receipt()
        self.assertEqual(receipt["state"], "saved_locally")
        self.assertEqual(receipt["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(receipt["checksum"], checksum(data))
        self.assertEqual(json.loads((self.root / "receiver-status.json").read_text())["state"], "ready")

    def test_missing_duplicate_reordered_or_wrong_offset_data_never_publish(self):
        for label, invalid in (
            ("missing", data_packet(2, 3, b"def")),
            ("duplicate", data_packet(0, 0, b"abc")),
            ("offset", data_packet(1, 1, b"abc")),
            ("overflow", data_packet(1, 0, b"1234567")),
        ):
            with self.subTest(label=label):
                self.assertTrue(self.receiver.receive(start(b"abcdef", label + ".csv")))
                self.assertFalse(self.receiver.receive(invalid))
                self.assertEqual(self.receipt(label + ".csv")["state"], "failed")
                self.assertFalse((self.root / (label + ".csv")).exists())

    def test_bad_checksum_and_incomplete_end_remain_partial(self):
        for label, data, end in (
            ("checksum", b"abcdef", end_packet(2, b"abcdef", corrupt=True)),
            ("incomplete", b"abc", end_packet(2, b"abcdef")),
        ):
            with self.subTest(label=label):
                self.receiver.receive(start(b"abcdef", label + ".csv"))
                self.receiver.receive(data_packet(1, 0, data))
                self.assertFalse(self.receiver.receive(end))
                self.assertTrue((self.root / (label + ".csv.part")).exists())
                self.assertFalse((self.root / (label + ".csv")).exists())

    def test_truncated_or_extra_wire_bytes_fail_closed(self):
        for index, malformed in enumerate((data_packet(1, 0, b"abc")[:-1],
                                            data_packet(1, 0, b"abc") + b"x")):
            destination = f"malformed{index}.csv"
            self.receiver.receive(start(b"abc", destination))
            self.assertFalse(self.receiver.receive(malformed))
            self.assertEqual(self.receipt(destination)["state"], "failed")

    def test_disconnect_timeout_cancel_and_restart_leave_unpublished_partial(self):
        self.receiver.receive(start(b"abcdef", "disconnect.csv"))
        self.receiver.receive(data_packet(1, 0, b"abc"))
        self.receiver.abort("TCP connection changed")
        self.assertEqual(self.receipt("disconnect.csv")["state"], "failed")
        self.receiver.receive(start(b"abcdef", "timeout.csv"))
        self.receiver.active.last_progress -= 31
        self.receiver.tick()
        self.assertEqual(self.receipt("timeout.csv")["state"], "failed")
        self.receiver.receive(start(b"abcdef", "cancel.csv"))
        self.receiver.receive(space_packet(struct.pack(">BI", 3, 1)))
        self.assertEqual(self.receipt("cancel.csv")["state"], "failed")
        self.receiver.close()
        restarted = LocalFileReceiver(self.root)
        try:
            self.assertFalse(restarted.receive(data_packet(1, 0, b"abc")))
            self.assertFalse((self.root / "disconnect.csv").exists())
            self.assertFalse(restarted.receive(start(b"abcdef", "disconnect.csv")))
        finally:
            restarted.close()

    def test_unsafe_duplicate_and_oversize_destination_rejected(self):
        self.assertFalse(self.receiver.receive(start(b"abc", "../escape.csv")))
        self.assertFalse(self.receiver.receive(start(b"abc", "C:\\escape.csv")))
        self.receive_file(b"abc")
        self.assertFalse(self.receiver.receive(start(b"abc")))
        self.assertEqual((self.root / "science.csv").read_bytes(), b"abc")
        self.receiver.max_file_bytes = 2
        self.assertFalse(self.receiver.receive(start(b"abc", "oversize.csv")))

    def test_quota_failure_does_not_touch_existing_files(self):
        self.receiver.max_spool_bytes = 16384
        self.assertFalse(self.receiver.receive(start(b"abc")))
        self.assertFalse((self.root / "science.csv.part").exists())

    def test_progress_persists_on_tick_and_spool_has_one_owner(self):
        self.receiver.receive(start(b"abcdef"))
        self.receiver.receive(data_packet(1, 0, b"abc"))
        self.receiver.tick()
        self.assertEqual(self.receipt()["received_size"], 3)
        status = json.loads((self.root / "receiver-status.json").read_text())
        self.assertEqual(status["state"], "receiving")
        self.assertEqual(status["destination"], "science.csv")
        with self.assertRaisesRegex(RuntimeError, "another process"):
            LocalFileReceiver(self.root)

    def test_process_exit_releases_spool_and_marks_interrupted_receipt_failed(self):
        self.receiver.close()
        code = (
            "import os,sys; from pathlib import Path; "
            "from local_file_receiver import LocalFileReceiver; "
            "from test_local_file_receiver import start,data_packet; "
            "r=LocalFileReceiver(Path(sys.argv[1])); "
            "r.receive(start(b'abcdef')); r.receive(data_packet(1,0,b'abc')); "
            "r.tick(); os._exit(0)"
        )
        subprocess.run([sys.executable, "-c", code, str(self.root)], check=True, timeout=10, cwd=Path(__file__).parent)
        self.assertEqual(self.receipt()["state"], "receiving")
        restarted = LocalFileReceiver(self.root)
        try:
            self.assertEqual(self.receipt()["state"], "failed")
            self.assertFalse((self.root / "science.csv").exists())
            self.assertTrue((self.root / "science.csv.part").exists())
        finally:
            restarted.close()

    def test_restart_recovers_only_verified_finalization_windows(self):
        code = """
import os,sys
from pathlib import Path
import local_file_receiver as module
from test_local_file_receiver import file_packets
root, mode = Path(sys.argv[1]), sys.argv[2]
receiver = module.LocalFileReceiver(root)
original_link, original_json = module.os.link, module.atomic_json
def link(source, destination):
    if mode == 'before-link': os._exit(0)
    original_link(source, destination)
    if mode == 'after-link': os._exit(0)
def atomic_json(path, value):
    if value.get('state') == 'saved_locally': os._exit(0)
    original_json(path, value)
module.os.link, module.atomic_json = link, atomic_json
for packet in file_packets(b'complete verified bytes'):
    receiver.receive(packet)
raise RuntimeError('fault was not exercised')
"""
        for mode in ("before-link", "after-link", "after-unlink"):
            with self.subTest(mode=mode):
                directory = self.root / mode
                subprocess.run([sys.executable, "-c", code, str(directory), mode], check=True, timeout=10, cwd=Path(__file__).parent)
                receipt_path = directory / "science.csv.receipt.json"
                self.assertEqual(json.loads(receipt_path.read_text())["state"], "finalizing")
                restarted = LocalFileReceiver(directory)
                try:
                    self.assertEqual(json.loads(receipt_path.read_text())["state"], "saved_locally")
                    self.assertEqual((directory / "science.csv").read_bytes(), b"complete verified bytes")
                    self.assertFalse((directory / "science.csv.part").exists())
                finally:
                    restarted.close()

    def test_restart_rejects_tampered_finalization_marker(self):
        directory = self.root / "tampered"
        directory.mkdir()
        (directory / "science.csv.part").write_bytes(b"bad")
        (directory / "science.csv.receipt.json").write_text(json.dumps({
            "state": "finalizing", "destination": "science.csv", "expected_size": 3,
            "received_size": 3, "sha256": hashlib.sha256(b"abc").hexdigest(),
        }))
        restarted = LocalFileReceiver(directory)
        try:
            self.assertEqual(json.loads((directory / "science.csv.receipt.json").read_text())["state"], "failed")
            self.assertFalse((directory / "science.csv").exists())
        finally:
            restarted.close()

    def assert_bridge_paths_continue(self, receiver):
        flight, ground = Mock(), Mock()
        flight.write.return_value = ground.write.return_value = True
        bridge = cdr_comm_bridge.PacketRouterBridge(flight, ground, receiver, 128, 68)
        pong = space_packet(b"PONG event after file fault", apid=2)
        bridge.route(pong)
        ground.write.assert_called_once_with(pong)
        command = b"stock TC frame containing PING"
        def command_once():
            bridge.running = False
            return command
        ground.read.side_effect = command_once
        bridge.uplink_loop()
        flight.write.assert_called_once_with(command)

    def test_bad_historical_receipt_is_quarantined_without_blocking_good_files(self):
        self.receive_file(b"retained valid file", "good.csv")
        self.receiver.close()
        (self.root / "broken.csv.receipt.json").write_text("{broken")
        (self.root / "missing.csv.receipt.json").write_text('{"state":"saved_locally"}')
        restarted = LocalFileReceiver(self.root)
        try:
            self.assertTrue(restarted.file_admission_ready)
            self.assertEqual(restarted.status()["quarantined_receipts"], 2)
            self.assertEqual(len(list(self.root.glob("*.quarantined-*"))), 2)
            self.assertEqual((self.root / "good.csv").read_bytes(), b"retained valid file")
            self.assert_bridge_paths_continue(restarted)
            for packet in file_packets(b"new file", "new.csv"):
                self.assertTrue(restarted.receive(packet))
        finally:
            restarted.close()

    def test_disk_full_data_failure_disables_files_but_keeps_stock_bridge_paths(self):
        self.receiver.receive(start(b"abcdef"))
        output = Mock(wraps=self.receiver.active.output)
        output.write.side_effect = OSError(errno.ENOSPC, "disk full")
        self.receiver.active.output = output
        self.assertFalse(self.receiver.receive(data_packet(1, 0, b"abc")))
        self.assertFalse(self.receiver.file_admission_ready)
        self.assertIn("disk full", self.receiver.file_error)
        self.assertFalse((self.root / "science.csv").exists())
        self.assertFalse(self.receiver.receive(start(b"abc", "later.csv")))
        self.receiver.tick()
        self.assert_bridge_paths_continue(self.receiver)
        status = json.loads((self.root / "receiver-status.json").read_text())
        self.assertFalse(status["file_admission_ready"])
        self.assertEqual(status["state"], "ready")

    def test_unwritable_status_and_startup_spool_do_not_stop_telemetry(self):
        with patch("local_file_receiver.atomic_json", side_effect=PermissionError(errno.EROFS, "read-only spool")):
            self.receiver.tick()
            self.receiver.abort("cancel")
            self.assertFalse(self.receiver.file_admission_ready)
            self.assert_bridge_paths_continue(self.receiver)
        with patch.object(Path, "mkdir", side_effect=PermissionError(errno.EACCES, "spool unavailable")):
            receiver = LocalFileReceiver(self.root / "unavailable")
        try:
            self.assertFalse(receiver.file_admission_ready)
            receiver.tick()
            self.assert_bridge_paths_continue(receiver)
        finally:
            receiver.close()

    @unittest.skipIf(os.name == "nt", "POSIX directory permissions")
    def test_read_only_spool_start_rejects_files_without_stopping_bridge(self):
        self.root.chmod(0o555)
        try:
            self.assertFalse(self.receiver.receive(start(b"abc")))
            self.assertFalse(self.receiver.file_admission_ready)
            self.assert_bridge_paths_continue(self.receiver)
        finally:
            self.root.chmod(0o755)

    def test_old_socket_drop_after_reconnect_does_not_change_new_generation(self):
        client = cdr_comm_bridge.ObservedTcpClient("127.0.0.1", 1)
        old, new = Mock(), Mock()
        client.connection = old
        blocked = threading.Event()
        gate = threading.Lock()
        gate.acquire()
        class GateLock:
            def __enter__(self):
                blocked.set()
                gate.acquire()
            def __exit__(self, *args):
                gate.release()
        client.lock = GateLock()
        thread = threading.Thread(target=client.drop, args=(old, "old write failed"))
        thread.start()
        self.assertTrue(blocked.wait(1))
        # Replace under the same held lock before stale drop can test ownership.
        client.connection = new
        expected_epoch = client.connection_epoch
        gate.release()
        thread.join(1)
        self.assertFalse(thread.is_alive())
        self.assertIs(client.connection, new)
        self.assertEqual(client.connection_epoch, expected_epoch)
        old.close.assert_called_once()
        new.close.assert_not_called()

    def test_status_reports_saved_pending_and_remaining_space(self):
        self.receive_file(b"abc")
        self.receiver.tick()
        status = self.receiver.status()
        self.assertEqual(status["saved_files"], 1)
        self.assertEqual(status["pending_publications"], 1)
        self.assertGreater(status["free_spool_bytes"], 0)
        self.assertLess(status["free_spool_bytes"], self.receiver.max_spool_bytes)

    def test_stock_decoder_preserves_mixed_packets_spanning_frames_and_fragmented_tcp(self):
        data = bytes(range(256)) * 7
        file = list(file_packets(data, chunk_size=257))
        telemetry = [space_packet(b"telemetry", apid=1), space_packet(b"event", apid=2)]
        packets = [file[0], telemetry[0], *file[1:-1], telemetry[1], file[-1]]
        frames = tm_frames(packets)
        decoder = RawSpaceDataLinkFramerDeframer(scid=68, vcid=1, frame_size=128)
        forwarded = []
        router = cdr_comm_bridge.PacketRouterBridge(Mock(), Mock(), self.receiver, 128, 68)
        router.write_or_warn = lambda adapter, side, packet: forwarded.append(bytes(packet))
        pending = b""
        wire = b"".join(frames)
        for offset in range(0, len(wire), 19):
            pending += wire[offset:offset + 19]
            decoded, pending, discarded = decoder.deframe_all(pending, no_copy=True)
            self.assertFalse(discarded)
            for packet in decoded:
                router.route(packet)
        self.assertEqual(forwarded, telemetry)
        self.assertEqual((self.root / "science.csv").read_bytes(), data)

    def test_real_tcp_reconnect_discards_old_transfer_and_preserves_telemetry(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(2)
        listener.settimeout(5)
        done = threading.Event()
        errors = []
        telemetry = space_packet(b"telemetry after reconnect", apid=1)

        def server():
            try:
                with listener.accept()[0] as peer:
                    first = tm_frames([start(b"abcdefgh", "interrupted.csv"), data_packet(1, 0, b"abc")])
                    peer.sendall(b"".join(first))
                    deadline = time.monotonic() + 3
                    while not self.receiver.active and time.monotonic() < deadline:
                        time.sleep(0.01)
                with listener.accept()[0] as peer:
                    packets = [end_packet(2, b"abcdefgh"), telemetry,
                               *file_packets(b"complete new file", "reconnected.csv")]
                    wire = b"".join(tm_frames(packets))
                    for offset in range(0, len(wire), 23):
                        peer.sendall(wire[offset:offset + 23])
                    done.wait(timeout=5)
            except Exception as error:
                errors.append(error)

        class Ground:
            def __init__(self):
                self.packets = []

            def open(self):
                pass

            def close(self):
                pass

            def read(self):
                time.sleep(0.01)
                return b""

            def write(self, packet):
                self.packets.append(bytes(packet))
                return True

        thread = threading.Thread(target=server, daemon=True)
        thread.start()
        ground = Ground()
        bridge = cdr_comm_bridge.PacketRouterBridge(
            cdr_comm_bridge.ObservedTcpClient("127.0.0.1", listener.getsockname()[1]),
            ground, self.receiver, 128, 68)
        try:
            bridge.start()
            deadline = time.monotonic() + 5
            target = self.root / "reconnected.csv"
            while not target.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(target.exists(), errors)
            self.assertEqual(target.read_bytes(), b"complete new file")
            self.assertEqual(self.receipt("interrupted.csv")["state"], "failed")
            self.assertFalse((self.root / "interrupted.csv").exists())
            self.assertEqual(ground.packets, [telemetry])
            self.assertGreater(bridge.statistics()["tcp_bytes"], 0)
            self.assertEqual(bridge.statistics()["nonfile_packets"], 1)
            self.assertGreaterEqual(bridge.statistics()["connection_changes"], 3)
        finally:
            done.set()
            bridge.stop()
            listener.close()
            thread.join(timeout=2)
        self.assertFalse(errors)

    def test_oversize_nonfile_packet_and_udp_refusal_have_bounded_counters(self):
        ground = Mock()
        ground.write.return_value = False
        router = cdr_comm_bridge.PacketRouterBridge(Mock(), ground, self.receiver, 128, 68)
        router.route(space_packet(b"x" * 65500, apid=2))
        ground.write.assert_not_called()
        router.route(space_packet(b"event", apid=2))
        self.assertEqual(ground.write.call_count, 1)
        self.assertEqual(router.statistics()["oversize_nonfile_packets"], 1)
        self.assertEqual(router.statistics()["udp_write_failures"], 1)

    def test_uploader_outage_retains_verified_file_and_manual_retry_is_idempotent(self):
        receipt_path = self.receive_file(b"a,b\n1,2\n")
        publisher = BucketPublisher("http://127.0.0.1:1", timeout=0.2, bucket="science archive")
        with self.assertRaises(OSError):
            publisher.publish(receipt_path)
        self.assertEqual(self.receipt()["state"], "saved_locally")
        self.assertEqual(self.receipt()["bucket"], "science archive")
        with object_server() as (url, state):
            publisher = BucketPublisher(url, bucket="science archive")
            publisher.publish(receipt_path)
            publisher.publish(receipt_path)
            self.assertEqual(state["posts"], 1)
        self.assertEqual(self.receipt()["state"], "available_in_yamcs")

    def test_retry_command_quotes_windows_powershell_arguments(self):
        with patch.object(sys, "executable", "C:\\Program Files\\Python\\python.exe"):
            command = publication_retry_command(Path("receipt's file.json"), Path("station's folder"),
                                                "http://127.0.0.1:8123", "science archive", windows=True)
        self.assertTrue(command.startswith("& 'C:\\Program Files\\Python\\python.exe' "))
        self.assertIn("'--station' 'station''s folder'", command)
        self.assertIn("'--bucket' 'science archive'", command)
        self.assertIn("'--retry' 'receipt''s file.json'", command)

    def test_changed_local_file_and_conflicting_bucket_object_are_not_overwritten(self):
        receipt_path = self.receive_file(b"abc")
        with object_server() as (url, state):
            state["objects"]["/api/storage/buckets/fprimeFilesIn/objects/science.csv"] = b"different"
            with self.assertRaises(RuntimeError):
                BucketPublisher(url).publish(receipt_path)
            self.assertEqual(state["posts"], 0)
        (self.root / "science.csv").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "changed"):
            BucketPublisher("http://127.0.0.1:1").publish(receipt_path)

    def test_http_upload_above_five_mib_rejects_then_retries_from_disk(self):
        data = b"12345,67890\n" * 500000  # 6 MB, above the bundled route's default 5 MiB.
        receipt_path = self.receive_file(data)
        with object_server(max_upload=5 * 1024 * 1024) as (url, state):
            publisher = BucketPublisher(url)
            with self.assertRaises((RuntimeError, OSError)):
                publisher.publish(receipt_path)
            self.assertEqual(self.receipt()["state"], "saved_locally")
            state["max_upload"] = len(data) + 1
            publisher.publish(receipt_path)
            self.assertEqual(self.receipt()["bucket_sha256"], hashlib.sha256(data).hexdigest())

    def test_slow_and_oversize_http_responses_release_the_publication_worker(self):
        for mode in ("slow", "oversize", "error-body"):
            with self.subTest(mode=mode):
                directory = self.root / mode
                receiver = LocalFileReceiver(directory)
                first_data = b"a" * 1000
                second_data = b"second CSV"
                first = directory / "first.csv.receipt.json"
                second = directory / "second.csv.receipt.json"
                for packet in file_packets(first_data, "first.csv"):
                    self.assertTrue(receiver.receive(packet))
                for packet in file_packets(second_data, "second.csv"):
                    self.assertTrue(receiver.receive(packet))
                state = {"objects": {}, "fault": mode}
                class Handler(BaseHTTPRequestHandler):
                    def log_message(self, *args):
                        pass
                    def do_GET(self):
                        if self.path.endswith("first.csv") and state["fault"]:
                            self.send_response(500 if mode == "error-body" else 200)
                            if mode == "oversize":
                                self.send_header("Content-Length", "1001")
                            self.end_headers()
                            try:
                                for _ in range(1000 if mode == "slow" else 2):
                                    self.wfile.write(b"a" if mode == "slow" else b"x" * 8192)
                                    self.wfile.flush()
                                    if mode == "slow":
                                        time.sleep(0.02)
                            except (BrokenPipeError, ConnectionResetError):
                                pass
                            return
                        body = state["objects"].get(self.path)
                        self.send_response(404 if body is None else 200)
                        self.end_headers()
                        if body is not None:
                            self.wfile.write(body)
                    def do_POST(self):
                        state["objects"][self.path] = self.rfile.read(int(self.headers["Content-Length"]))
                        self.send_response(200)
                        self.end_headers()
                server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                publisher = BucketPublisher(f"http://127.0.0.1:{server.server_port}", timeout=0.2)
                worker = PublicationWorker(publisher)
                started = time.monotonic()
                try:
                    worker.submit(first)
                    worker.submit(second)
                    deadline = started + 2
                    while time.monotonic() < deadline:
                        first_receipt = json.loads(first.read_text())
                        second_receipt = json.loads(second.read_text())
                        if first_receipt.get("publication_error") and second_receipt["state"] == "available_in_yamcs":
                            break
                        time.sleep(0.01)
                    self.assertLess(time.monotonic() - started, 1.5)
                    self.assertEqual(first_receipt["state"], "saved_locally")
                    self.assertTrue(first_receipt.get("publication_error"))
                    self.assertEqual(second_receipt["state"], "available_in_yamcs")
                    self.assertEqual((directory / "first.csv").read_bytes(), first_data)
                    state["fault"] = None
                    publisher.publish(first)
                    self.assertEqual(json.loads(first.read_text())["state"], "available_in_yamcs")
                finally:
                    worker.close()
                    receiver.close()
                    server.shutdown()
                    server.server_close()
                    thread.join(1)

    def test_background_publication_does_not_block_next_reception(self):
        release = threading.Event()
        with object_server(block_upload=release) as (url, state):
            worker = PublicationWorker(BucketPublisher(url))
            self.receiver.on_saved = worker.submit
            try:
                self.receive_file(b"first", "first.csv")
                self.receive_file(b"second", "second.csv")
                self.assertEqual(self.receipt("second.csv")["state"], "saved_locally")
                release.set()
                deadline = time.monotonic() + 5
                while state["posts"] < 2 and time.monotonic() < deadline:
                    time.sleep(0.02)
                self.assertEqual(state["posts"], 2)
            finally:
                release.set()
                worker.close()


if __name__ == "__main__":
    unittest.main()
