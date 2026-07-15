import json
import pathlib
import struct
import sys
import tempfile
import time
import types
import unittest
from unittest import mock


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))


class DummySerialException(Exception):
    pass


sys.modules.setdefault(
    "serial",
    types.SimpleNamespace(Serial=object, SerialException=DummySerialException),
)

import payload_receiver  # noqa: E402


class DummySerial:
    def __init__(self) -> None:
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)


class ScriptedSerial(DummySerial):
    def __init__(self, data: bytes = b"", read_error: Exception | None = None) -> None:
        super().__init__()
        self.buffer = bytearray(data)
        self.read_error = read_error

    def __enter__(self) -> "ScriptedSerial":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> bool:
        return False

    def read(self, size: int) -> bytes:
        if self.buffer:
            chunk = bytes(self.buffer[:size])
            del self.buffer[:size]
            return chunk
        if self.read_error is not None:
            raise self.read_error
        return b""


def make_header(
    product_id: int,
    transfer_id: int,
    blob: bytes,
    expected_crc: int | None = None,
) -> bytes:
    total_packets = (len(blob) + payload_receiver.DATA_BYTES - 1) // payload_receiver.DATA_BYTES
    packet = bytearray()
    packet += payload_receiver.MAGIC
    packet += bytes([payload_receiver.TYPE_HEADER, transfer_id])
    packet += struct.pack("<I", product_id)
    packet += struct.pack("<I", len(blob))
    packet += struct.pack("<H", total_packets)
    packet += bytes([payload_receiver.DATA_BYTES])
    packet += struct.pack(
        "<H",
        payload_receiver.crc16_ccitt(blob) if expected_crc is None else expected_crc,
    )
    return bytes(packet)


def make_data(transfer_id: int, packet_index: int, chunk: bytes) -> bytes:
    packet = bytearray()
    packet += payload_receiver.MAGIC
    packet += bytes([payload_receiver.TYPE_DATA, transfer_id])
    packet += struct.pack("<H", packet_index)
    packet += bytes([len(chunk)])
    packet += chunk
    packet += struct.pack("<H", payload_receiver.crc16_ccitt(packet))
    return bytes(packet)


def make_end(transfer_id: int, total_packets: int, blob: bytes) -> bytes:
    packet = bytearray()
    packet += payload_receiver.MAGIC
    packet += bytes([payload_receiver.TYPE_END, transfer_id])
    packet += struct.pack("<H", total_packets)
    packet += struct.pack("<H", payload_receiver.crc16_ccitt(blob))
    return bytes(packet)


class PayloadReceiverTests(unittest.TestCase):
    def test_extracts_variable_length_packets_and_reconstructs_arbitrary_bytes(self) -> None:
        blob = b"\x00N2\xffpayload,csv\n1,2,3\n"
        transfer_id = 9
        receiver = payload_receiver.PayloadReceiver("unused", 115200, pathlib.Path("/tmp/out.bin"), 1.0)
        header = make_header(42, transfer_id, blob)
        data = make_data(transfer_id, 0, blob)
        end = make_end(transfer_id, 1, blob)

        receiver.rx_buffer += b"noise" + header + data + end

        packets = [receiver.try_extract_packet(), receiver.try_extract_packet(), receiver.try_extract_packet()]
        self.assertEqual([len(packet) for packet in packets], [17, 7 + len(blob) + 2, 8])

        serial = DummySerial()
        for packet in packets:
            receiver.handle_packet(packet, serial)  # type: ignore[arg-type]

        self.assertTrue(receiver.complete)
        self.assertEqual(receiver.reconstruct(), blob)
        self.assertEqual(serial.writes, [])

    def test_preserves_split_magic_prefix_across_serial_reads(self) -> None:
        receiver = payload_receiver.PayloadReceiver("unused", 115200, pathlib.Path("/tmp/out.bin"), 1.0)
        header = make_header(42, 9, b"payload")

        receiver.rx_buffer += b"noiseN"
        self.assertEqual(receiver.try_extract_packet(), b"")
        self.assertEqual(receiver.rx_buffer, bytearray(b"N"))

        receiver.rx_buffer += header[1:]
        self.assertEqual(receiver.try_extract_packet(), header)

    def test_retry_request_is_not_padded_to_max_packet_length(self) -> None:
        blob = b"x" * (payload_receiver.DATA_BYTES * 10)
        events: list[payload_receiver.ReceiverEvent] = []
        receiver = payload_receiver.PayloadReceiver(
            "unused",
            115200,
            pathlib.Path("/tmp/out.bin"),
            1.0,
            on_event=events.append,
        )
        receiver.handle_header(make_header(7, 3, blob))
        events.clear()

        serial = DummySerial()
        receiver.request_retries(serial)  # type: ignore[arg-type]

        self.assertEqual(len(serial.writes), 1)
        request = serial.writes[0]
        self.assertLess(len(request), payload_receiver.MAX_PACKET)
        self.assertEqual(request[:4], payload_receiver.MAGIC + bytes([payload_receiver.TYPE_RETRY_REQUEST, 3]))
        self.assertEqual(request[6], 2)
        self.assertEqual(len(events), 1)
        event = events[0]
        self.assertEqual(event.kind, "retry_requested")
        self.assertEqual(event.retry_start, 0)
        self.assertEqual(event.retry_count, 10)
        self.assertEqual(event.retry_bitmap_bytes, 2)
        self.assertEqual(event.retry_rounds, 1)
        self.assertEqual(event.missing_packets, 10)

    def test_retry_waits_for_quiet_after_repair_data(self) -> None:
        blob = b"x" * (payload_receiver.DATA_BYTES * 2)
        receiver = payload_receiver.PayloadReceiver(
            "unused", 115200, pathlib.Path("/tmp/out.bin"), 1.0
        )
        receiver.handle_header(make_header(7, 3, blob))
        receiver.end_seen = True
        receiver.last_end_s = 100.0
        receiver.last_packet_s = 105.0
        receiver.next_retry_request_s = 0.0
        serial = DummySerial()

        with mock.patch.object(payload_receiver.time, "monotonic", return_value=105.1):
            receiver.request_retries_if_due(serial)  # type: ignore[arg-type]
        self.assertEqual(serial.writes, [])

        with mock.patch.object(payload_receiver.time, "monotonic", return_value=105.6):
            receiver.request_retries_if_due(serial)  # type: ignore[arg-type]
        self.assertEqual(len(serial.writes), 1)

    def test_repeated_header_preserves_packets_for_same_transfer(self) -> None:
        blob = b"a" * payload_receiver.DATA_BYTES + b"tail"
        transfer_id = 11
        receiver = payload_receiver.PayloadReceiver("unused", 115200, pathlib.Path("/tmp/out.bin"), 1.0)
        header = make_header(73, transfer_id, blob)
        first_data = make_data(transfer_id, 0, blob[: payload_receiver.DATA_BYTES])

        receiver.handle_header(header)
        receiver.handle_data(first_data)
        receiver.end_seen = True
        receiver.handle_header(header)

        self.assertEqual(receiver.packets, {0: blob[: payload_receiver.DATA_BYTES]})
        self.assertTrue(receiver.end_seen)

    def test_data_packet_with_wrong_position_length_is_rejected(self) -> None:
        blob = b"a" * payload_receiver.DATA_BYTES + b"tail"
        transfer_id = 12
        receiver = payload_receiver.PayloadReceiver(
            "unused", 115200, pathlib.Path("/tmp/out.bin"), 1.0
        )
        receiver.handle_header(make_header(7, transfer_id, blob))

        receiver.handle_data(make_data(transfer_id, 0, b"short"))
        receiver.handle_data(make_data(transfer_id, 1, b"too-long"))

        self.assertEqual(receiver.packets, {})

    def test_conflicting_duplicate_packet_is_rejected_and_checkpoint_stays_consistent(self) -> None:
        original = b"a" * payload_receiver.DATA_BYTES
        conflicting = b"b" * payload_receiver.DATA_BYTES
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            checkpoint = root / "checkpoint"
            receiver = payload_receiver.PayloadReceiver(
                "port", 115200, root / "unused.fdp", 1.0, checkpoint_dir=checkpoint
            )
            receiver.handle_header(make_header(7, 12, original))
            receiver.handle_data(make_data(12, 0, original))
            receiver.handle_data(make_data(12, 0, conflicting))
            self.assertEqual(receiver.packets[0], original)

            resumed = payload_receiver.PayloadReceiver(
                "port", 115200, root / "unused.fdp", 1.0, checkpoint_dir=checkpoint
            )
            self.assertTrue(resumed.load_checkpoint())
            self.assertEqual(resumed.packets[0], original)

    def test_new_header_resets_previous_transfer_state(self) -> None:
        first_blob = b"first"
        second_blob = b"second"
        receiver = payload_receiver.PayloadReceiver("unused", 115200, pathlib.Path("/tmp/out.bin"), 1.0)
        receiver.handle_header(make_header(1, 1, first_blob))
        receiver.handle_data(make_data(1, 0, first_blob))
        receiver.end_seen = True

        receiver.handle_header(make_header(2, 2, second_blob))

        self.assertEqual(receiver.transfer_id, 2)
        self.assertEqual(receiver.packets, {})
        self.assertFalse(receiver.end_seen)

    def test_directory_mode_writes_completed_transfer_with_requested_extension(self) -> None:
        blob = b"fake-fdp-bytes"
        transfer_id = 4
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = pathlib.Path(tmp)
            receiver = payload_receiver.PayloadReceiver(
                "unused",
                115200,
                pathlib.Path("/tmp/out.bin"),
                1.0,
                output_dir=output_dir,
                ext=".fdp",
            )

            packets = [
                make_header(88, transfer_id, blob),
                make_data(transfer_id, 0, blob),
                make_end(transfer_id, 1, blob),
            ]
            serial = DummySerial()
            for packet in packets:
                receiver.handle_packet(packet, serial)  # type: ignore[arg-type]

            self.assertTrue(receiver.complete)
            receiver.finalize_to_dir()

            outputs = list(output_dir.glob("Dp_*.fdp"))
            self.assertEqual(len(outputs), 1)
            self.assertEqual(outputs[0].read_bytes(), blob)

    def test_run_emits_success_lifecycle_and_writes_exact_blob(self) -> None:
        blob = b"callback-success"
        transfer_id = 12
        stream = (
            make_header(101, transfer_id, blob)
            + make_data(transfer_id, 0, blob)
            + make_end(transfer_id, 1, blob)
        )
        events: list[payload_receiver.ReceiverEvent] = []

        with tempfile.TemporaryDirectory() as tmp:
            output = pathlib.Path(tmp) / "payload.fdp"
            serial = ScriptedSerial(stream)
            receiver = payload_receiver.PayloadReceiver(
                "test-port",
                115200,
                output,
                1.0,
                on_event=events.append,
                serial_factory=lambda *args, **kwargs: serial,
            )

            self.assertEqual(receiver.run(), 0)
            self.assertEqual(output.read_bytes(), blob)

        self.assertEqual(
            [event.kind for event in events],
            ["ready", "transfer_started", "progress", "crc_checked", "transfer_saved"],
        )
        progress = events[2]
        self.assertEqual(progress.product_id, 101)
        self.assertEqual(progress.transfer_id, transfer_id)
        self.assertEqual(progress.received_bytes, len(blob))
        self.assertEqual(progress.received_packets, 1)
        self.assertEqual(progress.missing_packets, 0)
        self.assertTrue(events[3].crc_ok)
        self.assertEqual(events[3].actual_crc, payload_receiver.crc16_ccitt(blob))
        self.assertTrue(events[4].crc_ok)
        self.assertTrue(events[4].output_path and events[4].output_path.endswith("payload.fdp"))

    def test_run_emits_crc_failure_and_preserves_exit_code(self) -> None:
        blob = b"bad-whole-file-crc"
        transfer_id = 13
        actual_crc = payload_receiver.crc16_ccitt(blob)
        expected_crc = actual_crc ^ 0xFFFF
        stream = make_header(102, transfer_id, blob, expected_crc) + make_data(transfer_id, 0, blob)
        events: list[payload_receiver.ReceiverEvent] = []

        with tempfile.TemporaryDirectory() as tmp:
            output = pathlib.Path(tmp) / "must-not-exist.fdp"
            serial = ScriptedSerial(stream)
            receiver = payload_receiver.PayloadReceiver(
                "test-port",
                115200,
                output,
                1.0,
                on_event=events.append,
                serial_factory=lambda *args, **kwargs: serial,
            )

            self.assertEqual(receiver.run(), 3)
            self.assertFalse(output.exists())

        crc_events = [event for event in events if event.kind == "crc_checked"]
        self.assertEqual(len(crc_events), 1)
        self.assertFalse(crc_events[0].crc_ok)
        self.assertEqual(crc_events[0].actual_crc, actual_crc)
        self.assertEqual(crc_events[0].expected_crc, expected_crc)
        self.assertNotIn("transfer_saved", [event.kind for event in events])

    def test_serial_open_error_emits_event_and_reraises(self) -> None:
        events: list[payload_receiver.ReceiverEvent] = []
        serial_error = payload_receiver.serial.SerialException("port busy")

        def fail_to_open(*args: object, **kwargs: object) -> object:
            raise serial_error

        with tempfile.TemporaryDirectory() as tmp:
            receiver = payload_receiver.PayloadReceiver(
                "busy-port",
                115200,
                pathlib.Path(tmp) / "unused.bin",
                1.0,
                output_dir=pathlib.Path(tmp),
                on_event=events.append,
                serial_factory=fail_to_open,
            )

            with self.assertRaises(payload_receiver.serial.SerialException) as raised:
                receiver.run_directory()

        self.assertIs(raised.exception, serial_error)
        self.assertEqual([event.kind for event in events], ["serial_error"])
        self.assertEqual(events[0].error_phase, "open")
        self.assertEqual(events[0].error, "port busy")

    def test_serial_disconnect_emits_event_and_reraises(self) -> None:
        events: list[payload_receiver.ReceiverEvent] = []
        serial_error = payload_receiver.serial.SerialException("device disconnected")
        serial = ScriptedSerial(read_error=serial_error)

        with tempfile.TemporaryDirectory() as tmp:
            receiver = payload_receiver.PayloadReceiver(
                "disconnecting-port",
                115200,
                pathlib.Path(tmp) / "unused.bin",
                1.0,
                output_dir=pathlib.Path(tmp),
                on_event=events.append,
                serial_factory=lambda *args, **kwargs: serial,
            )

            with self.assertRaises(payload_receiver.serial.SerialException) as raised:
                receiver.run_directory()

        self.assertIs(raised.exception, serial_error)
        self.assertEqual([event.kind for event in events], ["ready", "serial_error"])
        self.assertEqual(events[-1].error_phase, "io")
        self.assertEqual(events[-1].error, "device disconnected")

    def test_directory_mode_receives_two_consecutive_transfers(self) -> None:
        first_blob = b"first-fdp"
        second_blob = b"second-fdp"
        stream = (
            make_header(201, 21, first_blob)
            + make_data(21, 0, first_blob)
            + make_end(21, 1, first_blob)
            + make_header(202, 22, second_blob)
            + make_data(22, 0, second_blob)
            + make_end(22, 1, second_blob)
        )
        events: list[payload_receiver.ReceiverEvent] = []
        serial = ScriptedSerial(stream)

        with tempfile.TemporaryDirectory() as tmp:
            output_dir = pathlib.Path(tmp)
            receiver: payload_receiver.PayloadReceiver
            receiver = payload_receiver.PayloadReceiver(
                "test-port",
                115200,
                output_dir / "unused.bin",
                1.0,
                output_dir=output_dir,
                ext=".fdp",
                on_event=events.append,
                serial_factory=lambda *args, **kwargs: serial,
                stop_requested=lambda: receiver.received_count >= 2,
            )

            self.assertEqual(receiver.run_directory(), 0)
            outputs = sorted(output_dir.glob("Dp_*.fdp"))
            self.assertEqual(len(outputs), 2)
            self.assertEqual({output.read_bytes() for output in outputs}, {first_blob, second_blob})

        saved_events = [event for event in events if event.kind == "transfer_saved"]
        self.assertEqual(len(saved_events), 2)
        self.assertEqual([event.product_id for event in saved_events], [201, 202])
        self.assertEqual([event.transfer_id for event in saved_events], [21, 22])
        self.assertEqual(len({event.output_path for event in saved_events}), 2)
        self.assertTrue(all(event.crc_ok for event in saved_events))

    def test_main_defaults_to_single_file_mode(self) -> None:
        with mock.patch.object(payload_receiver, "PayloadReceiver") as receiver_class:
            receiver = receiver_class.return_value
            receiver.run.return_value = 7

            result = payload_receiver.main(["--port", "test-port"])

        self.assertEqual(result, 7)
        receiver_class.assert_called_once_with(
            "test-port",
            115200,
            pathlib.Path("payload_blob.bin"),
            120.0,
            output_dir=None,
            ext=".bin",
            debug=False,
            checkpoint_dir=None,
            checkpoint_max_age_s=payload_receiver.DEFAULT_CHECKPOINT_MAX_AGE_S,
            transfer_timeout_s=None,
            absolute_transfer_timeout_s=None,
            save_partial_on_timeout=False,
        )
        receiver.run.assert_called_once_with()
        receiver.run_directory.assert_not_called()

    def test_main_exposes_bounded_partial_timeout_mode(self) -> None:
        with mock.patch.object(payload_receiver, "PayloadReceiver") as receiver_class:
            receiver_class.return_value.run_directory.return_value = 0
            result = payload_receiver.main(
                [
                    "--port",
                    "test-port",
                    "--output-dir",
                    "/tmp/payloads",
                    "--transfer-timeout",
                    "2",
                    "--absolute-transfer-timeout",
                    "10",
                    "--save-partial-on-timeout",
                ]
            )

        self.assertEqual(result, 0)
        kwargs = receiver_class.call_args.kwargs
        self.assertEqual(kwargs["transfer_timeout_s"], 2.0)
        self.assertEqual(kwargs["absolute_transfer_timeout_s"], 10.0)
        self.assertTrue(kwargs["save_partial_on_timeout"])

    def test_partial_reconstruction_preserves_packet_positions(self) -> None:
        blob = bytes(range(105))
        receiver = payload_receiver.PayloadReceiver(
            "unused",
            115200,
            pathlib.Path("/tmp/unused.bin"),
            1.0,
        )
        receiver.handle_header(make_header(301, 31, blob))
        receiver.handle_data(make_data(31, 0, blob[:35]))
        receiver.handle_data(make_data(31, 2, blob[70:]))

        partial = receiver.reconstruct_partial()

        self.assertEqual(len(partial), len(blob))
        self.assertEqual(partial[:35], blob[:35])
        self.assertEqual(partial[35:70], b"\x00" * 35)
        self.assertEqual(partial[70:], blob[70:])

    def test_directory_deadline_saves_partial_with_missing_map(self) -> None:
        blob = bytes(range(105))
        stream = (
            make_header(302, 32, blob)
            + make_data(32, 0, blob[:35])
            + make_data(32, 2, blob[70:])
            + make_end(32, 3, blob)
        )
        events: list[payload_receiver.ReceiverEvent] = []
        serial = ScriptedSerial(stream)

        with tempfile.TemporaryDirectory() as tmp:
            output_dir = pathlib.Path(tmp)
            receiver: payload_receiver.PayloadReceiver
            receiver = payload_receiver.PayloadReceiver(
                "test-port",
                115200,
                output_dir / "unused.bin",
                1.0,
                output_dir=output_dir,
                ext=".fdp",
                on_event=events.append,
                serial_factory=lambda *args, **kwargs: serial,
                stop_requested=lambda: receiver.received_count >= 1,
                transfer_timeout_s=0.01,
                save_partial_on_timeout=True,
            )

            self.assertEqual(receiver.run_directory(), 0)
            outputs = list(output_dir.glob("*.fdp.partial"))
            self.assertEqual(len(outputs), 1)
            self.assertEqual(len(outputs[0].read_bytes()), len(blob))

        event = next(item for item in events if item.kind == "partial_saved")
        self.assertTrue(event.partial)
        self.assertEqual(event.missing_packet_indices, (1,))
        self.assertFalse(event.crc_ok)
        self.assertIn("stalled", event.timeout_reason or "")

    def test_checkpoint_restart_resumes_at_25_50_and_90_percent_with_exact_crc(self) -> None:
        blob = bytes(index % 251 for index in range(payload_receiver.DATA_BYTES * 20))
        transfer_id = 44
        header = make_header(9001, transfer_id, blob)
        for label, received_packets in (("25", 5), ("50", 10), ("90", 18)):
            with self.subTest(percent=label), tempfile.TemporaryDirectory() as tmp:
                root = pathlib.Path(tmp)
                checkpoint = root / "active-checkpoint"
                first = payload_receiver.PayloadReceiver(
                    "old-port",
                    115200,
                    root / "unused.fdp",
                    1.0,
                    checkpoint_dir=checkpoint,
                    source_identity={"serial_number": "ground", "interface_ordinal": 2},
                )
                first.handle_header(header)
                for index in range(received_packets):
                    start = index * payload_receiver.DATA_BYTES
                    first.handle_data(
                        make_data(transfer_id, index, blob[start : start + payload_receiver.DATA_BYTES])
                    )
                first.retry_rounds = 3
                first.persist_checkpoint(reason=f"receiver stopped at {label}%")

                events: list[payload_receiver.ReceiverEvent] = []
                resumed = payload_receiver.PayloadReceiver(
                    "new-port",
                    115200,
                    root / "unused.fdp",
                    1.0,
                    checkpoint_dir=checkpoint,
                    source_identity={"serial_number": "ground", "interface_ordinal": 2},
                    on_event=events.append,
                )
                self.assertTrue(resumed.load_checkpoint())
                self.assertEqual(len(resumed.packets), received_packets)
                self.assertEqual(resumed.retry_rounds, 3)
                self.assertEqual(events[-1].kind, "transfer_resumed")
                for index in range(received_packets, resumed.total_packets):
                    start = index * payload_receiver.DATA_BYTES
                    resumed.handle_data(
                        make_data(transfer_id, index, blob[start : start + payload_receiver.DATA_BYTES])
                    )
                self.assertTrue(resumed.complete)
                reconstructed = resumed.reconstruct()
                self.assertEqual(reconstructed, blob)
                self.assertEqual(payload_receiver.crc16_ccitt(reconstructed), resumed.file_crc)

    def test_corrupted_and_stale_checkpoints_are_rejected(self) -> None:
        blob = b"checkpoint-integrity" * 4
        for mode in ("corrupt-json", "corrupt-packet", "stale"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                root = pathlib.Path(tmp)
                checkpoint = root / "checkpoint"
                receiver = payload_receiver.PayloadReceiver(
                    "port",
                    115200,
                    root / "unused.fdp",
                    1.0,
                    checkpoint_dir=checkpoint,
                    checkpoint_max_age_s=5.0,
                )
                receiver.handle_header(make_header(77, 8, blob))
                receiver.handle_data(make_data(8, 0, blob[: payload_receiver.DATA_BYTES]))
                if mode == "corrupt-json":
                    (checkpoint / "manifest.json").write_text("{not-json", encoding="utf-8")
                elif mode == "corrupt-packet":
                    (checkpoint / "packets/000000.bin").write_bytes(b"corrupt")
                else:
                    manifest_path = checkpoint / "manifest.json"
                    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                    manifest["updated_at_wall_s"] = time.time() - 30.0
                    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

                events: list[payload_receiver.ReceiverEvent] = []
                restored = payload_receiver.PayloadReceiver(
                    "port",
                    115200,
                    root / "unused.fdp",
                    1.0,
                    checkpoint_dir=checkpoint,
                    checkpoint_max_age_s=5.0,
                    on_event=events.append,
                )
                self.assertFalse(restored.load_checkpoint())
                self.assertIsNone(restored.transfer_id)
                self.assertEqual(events[-1].kind, "checkpoint_rejected")
                self.assertFalse(checkpoint.exists())
                self.assertEqual(len(list(root.glob("checkpoint.rejected.*"))), 1)

    def test_checkpoint_directory_requires_safe_dedicated_ownership(self) -> None:
        blob = b"checkpoint-safety"
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            output_dir = root / "history"
            output_dir.mkdir()
            history = output_dir / "existing.fdp"
            history.write_bytes(b"preserve-me")

            with self.assertRaisesRegex(ValueError, "dedicated child directory"):
                payload_receiver.PayloadReceiver(
                    "port",
                    115200,
                    root / "unused.fdp",
                    1.0,
                    output_dir=output_dir,
                    checkpoint_dir=output_dir,
                )
            self.assertEqual(history.read_bytes(), b"preserve-me")

            unowned = root / "unowned-checkpoint"
            unowned.mkdir()
            unrelated = unowned / "unrelated.txt"
            unrelated.write_text("preserve-me", encoding="utf-8")
            receiver = payload_receiver.PayloadReceiver(
                "port",
                115200,
                root / "unused.fdp",
                1.0,
                checkpoint_dir=unowned,
            )
            with self.assertRaisesRegex(ValueError, "not owned"):
                receiver.handle_header(make_header(7, 12, blob))
            self.assertEqual(unrelated.read_text(encoding="utf-8"), "preserve-me")
            self.assertTrue(unowned.is_dir())

    def test_checkpoint_cleanup_deletes_only_owned_artifacts(self) -> None:
        blob = b"owned-checkpoint-cleanup"
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            checkpoint = root / "checkpoint"
            receiver = payload_receiver.PayloadReceiver(
                "port",
                115200,
                root / "unused.fdp",
                1.0,
                checkpoint_dir=checkpoint,
            )
            receiver.handle_header(make_header(7, 12, blob))
            unrelated = checkpoint / "operator-note.txt"
            unrelated.write_text("preserve-me", encoding="utf-8")

            receiver.clear_checkpoint()

            self.assertEqual(unrelated.read_text(encoding="utf-8"), "preserve-me")
            self.assertTrue(
                (checkpoint / payload_receiver.CHECKPOINT_OWNER_FILENAME).is_file()
            )
            self.assertFalse((checkpoint / "manifest.json").exists())

    def test_reenumerated_port_resumes_checkpoint_and_completes_exact_crc(self) -> None:
        blob = bytes(index % 251 for index in range(payload_receiver.DATA_BYTES * 8))
        transfer_id = 19
        first_half = make_header(501, transfer_id, blob) + b"".join(
            make_data(
                transfer_id,
                index,
                blob[index * payload_receiver.DATA_BYTES : (index + 1) * payload_receiver.DATA_BYTES],
            )
            for index in range(4)
        )
        second_half = b"".join(
            make_data(
                transfer_id,
                index,
                blob[index * payload_receiver.DATA_BYTES : (index + 1) * payload_receiver.DATA_BYTES],
            )
            for index in range(4, 8)
        ) + make_end(transfer_id, 8, blob)
        disconnect = payload_receiver.serial.SerialException("device disappeared")
        serials = {
            "old-port": ScriptedSerial(first_half, read_error=disconnect),
            "new-port": ScriptedSerial(second_half),
        }
        resolved_calls = 0

        def resolve() -> str | None:
            nonlocal resolved_calls
            resolved_calls += 1
            return "new-port"

        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            receiver: payload_receiver.PayloadReceiver
            receiver = payload_receiver.PayloadReceiver(
                "old-port",
                115200,
                root / "unused.fdp",
                1.0,
                output_dir=root / "output",
                ext=".fdp",
                serial_factory=lambda port, *_args, **_kwargs: serials[port],
                stop_requested=lambda: receiver.received_count >= 1,
                checkpoint_dir=root / "checkpoint",
                source_identity={"serial_number": "ground", "interface_ordinal": 2},
                port_resolver=resolve,
                reconnect_timeout_s=0.2,
                reconnect_interval_s=0.01,
                save_partial_on_disconnect=True,
            )
            self.assertEqual(receiver.run_directory(), 0)
            outputs = list((root / "output").glob("*.fdp"))
            self.assertEqual(len(outputs), 1)
            self.assertEqual(outputs[0].read_bytes(), blob)
            self.assertGreaterEqual(resolved_calls, 1)
            self.assertEqual(receiver.port, "new-port")
            self.assertFalse((root / "checkpoint").exists())

    def test_complete_checkpoint_finalizes_without_reopening_serial(self) -> None:
        blob = bytes(index % 251 for index in range(payload_receiver.DATA_BYTES * 3))
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            checkpoint = root / "checkpoint"
            first = payload_receiver.PayloadReceiver(
                "old-port", 115200, root / "unused.fdp", 1.0, checkpoint_dir=checkpoint
            )
            first.handle_header(make_header(701, 33, blob))
            for index in range(3):
                first.handle_data(
                    make_data(
                        33,
                        index,
                        blob[index * payload_receiver.DATA_BYTES : (index + 1) * payload_receiver.DATA_BYTES],
                    )
                )

            receiver: payload_receiver.PayloadReceiver
            receiver = payload_receiver.PayloadReceiver(
                "missing-port",
                115200,
                root / "unused.fdp",
                1.0,
                output_dir=root / "output",
                ext=".fdp",
                checkpoint_dir=checkpoint,
                serial_factory=lambda *_args, **_kwargs: self.fail("serial must not be opened"),
                stop_requested=lambda: receiver.received_count >= 1,
            )
            self.assertEqual(receiver.run_directory(), 0)
            output = next((root / "output").glob("*.fdp"))
            self.assertEqual(output.read_bytes(), blob)
            self.assertFalse(checkpoint.exists())

    def test_permanent_disconnect_saves_position_aware_partial_and_reason(self) -> None:
        blob = bytes(range(105))
        transfer_id = 55
        stream = make_header(601, transfer_id, blob) + make_data(
            transfer_id, 1, blob[35:70]
        )
        disconnect = payload_receiver.serial.SerialException("permanent disconnect")
        events: list[payload_receiver.ReceiverEvent] = []
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            receiver = payload_receiver.PayloadReceiver(
                "old-port",
                115200,
                root / "unused.fdp",
                1.0,
                output_dir=root / "output",
                ext=".fdp",
                on_event=events.append,
                serial_factory=lambda *_args, **_kwargs: ScriptedSerial(
                    stream, read_error=disconnect
                ),
                checkpoint_dir=root / "checkpoint",
                port_resolver=lambda: None,
                reconnect_timeout_s=0.02,
                reconnect_interval_s=0.005,
                save_partial_on_disconnect=True,
            )
            self.assertEqual(receiver.run_directory(), 2)
            partial = next((root / "output").glob("*.fdp.partial"))
            positional = partial.read_bytes()
            self.assertEqual(positional[:35], b"\x00" * 35)
            self.assertEqual(positional[35:70], blob[35:70])
            self.assertEqual(positional[70:], b"\x00" * 35)
            missing_map = pathlib.Path(f"{partial}.missing.json")
            missing = json.loads(missing_map.read_text(encoding="utf-8"))
            self.assertEqual(missing["missing_packet_indices"], [0, 2])
            self.assertIn("serial recovery expired", missing["failure_reason"])
            event = next(item for item in events if item.kind == "partial_saved")
            self.assertEqual(event.missing_packet_indices, (0, 2))
            self.assertIn("serial recovery expired", event.timeout_reason or "")
            self.assertEqual(event.missing_map_path, str(missing_map))

    def test_stop_during_serial_recovery_preserves_checkpoint_without_partial(self) -> None:
        blob = b"restart-during-recovery" * 3
        stream = make_header(801, 61, blob) + make_data(
            61, 0, blob[: payload_receiver.DATA_BYTES]
        )
        stopped = False

        def resolve() -> str | None:
            nonlocal stopped
            stopped = True
            return None

        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            receiver = payload_receiver.PayloadReceiver(
                "old-port",
                115200,
                root / "unused.fdp",
                1.0,
                output_dir=root / "output",
                ext=".fdp",
                serial_factory=lambda *_args, **_kwargs: ScriptedSerial(
                    stream,
                    read_error=payload_receiver.serial.SerialException("disconnected"),
                ),
                stop_requested=lambda: stopped,
                checkpoint_dir=root / "checkpoint",
                port_resolver=resolve,
                reconnect_timeout_s=0.2,
                reconnect_interval_s=0.005,
                save_partial_on_disconnect=True,
            )
            self.assertEqual(receiver.run_directory(), 0)
            self.assertTrue((root / "checkpoint/manifest.json").is_file())
            self.assertEqual(list((root / "output").glob("*.partial")), [])

    def test_progress_stall_and_absolute_deadlines_are_independent(self) -> None:
        receiver = payload_receiver.PayloadReceiver(
            "port",
            115200,
            pathlib.Path("/tmp/unused.fdp"),
            1.0,
            transfer_timeout_s=10.0,
            absolute_transfer_timeout_s=200.0,
        )
        receiver.transfer_id = 1
        receiver.transfer_started_s = 100.0
        receiver.last_packet_s = 195.0
        self.assertIsNone(receiver.transfer_expiry_reason(now=200.0))
        receiver.last_packet_s = 180.0
        self.assertIn("stalled", receiver.transfer_expiry_reason(now=200.0) or "")
        self.assertIn("absolute", receiver.transfer_expiry_reason(now=301.0) or "")


if __name__ == "__main__":
    unittest.main()
