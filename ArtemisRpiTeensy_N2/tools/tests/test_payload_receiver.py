import pathlib
import struct
import sys
import tempfile
import types
import unittest


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))
sys.modules.setdefault("serial", types.SimpleNamespace(Serial=object))

import payload_receiver  # noqa: E402


class DummySerial:
    def __init__(self) -> None:
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)


def make_header(product_id: int, transfer_id: int, blob: bytes) -> bytes:
    total_packets = (len(blob) + payload_receiver.DATA_BYTES - 1) // payload_receiver.DATA_BYTES
    packet = bytearray()
    packet += payload_receiver.MAGIC
    packet += bytes([payload_receiver.TYPE_HEADER, transfer_id])
    packet += struct.pack("<I", product_id)
    packet += struct.pack("<I", len(blob))
    packet += struct.pack("<H", total_packets)
    packet += bytes([payload_receiver.DATA_BYTES])
    packet += struct.pack("<H", payload_receiver.crc16_ccitt(blob))
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

    def test_retry_request_is_not_padded_to_max_packet_length(self) -> None:
        blob = b"x" * (payload_receiver.DATA_BYTES * 10)
        receiver = payload_receiver.PayloadReceiver("unused", 115200, pathlib.Path("/tmp/out.bin"), 1.0)
        receiver.handle_header(make_header(7, 3, blob))

        serial = DummySerial()
        receiver.request_retries(serial)  # type: ignore[arg-type]

        self.assertEqual(len(serial.writes), 1)
        request = serial.writes[0]
        self.assertLess(len(request), payload_receiver.MAX_PACKET)
        self.assertEqual(request[:4], payload_receiver.MAGIC + bytes([payload_receiver.TYPE_RETRY_REQUEST, 3]))
        self.assertEqual(request[6], 2)

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


if __name__ == "__main__":
    unittest.main()
