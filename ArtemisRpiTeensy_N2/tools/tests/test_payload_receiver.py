import pathlib
import struct
import sys
import tempfile
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
        )
        receiver.run.assert_called_once_with()
        receiver.run_directory.assert_not_called()

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
        self.assertIn("deadline", event.timeout_reason or "")


if __name__ == "__main__":
    unittest.main()
