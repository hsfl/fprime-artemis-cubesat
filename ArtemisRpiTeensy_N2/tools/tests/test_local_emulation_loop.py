#!/usr/bin/env python3

import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import local_emulation_loop as loop  # noqa: E402


class UartFrameParserTests(unittest.TestCase):
    def test_channelized_frame_round_trips_in_chunks(self) -> None:
        parser = loop.UartFrameParser()
        frame = loop.build_uart_frame(loop.CHANNEL_CCSDS, b"abc")

        self.assertEqual(parser.feed(frame[:3], 0.0), [])
        self.assertEqual(
            parser.feed(frame[3:], 0.0),
            [(loop.CHANNEL_CCSDS, b"abc")],
        )
        self.assertEqual(parser.crc_drops, 0)
        self.assertEqual(parser.framing_drops, 0)

    def test_invalid_channel_is_dropped(self) -> None:
        parser = loop.UartFrameParser()
        frame = bytearray(loop.build_uart_frame(loop.CHANNEL_CCSDS, b"abc"))
        frame[2] = loop.CHANNEL_COUNT

        self.assertEqual(parser.feed(bytes(frame), 0.0), [])
        self.assertEqual(parser.framing_drops, 1)

    def test_rf_segment_round_trips_payload_channel(self) -> None:
        segmenter = loop.RfSegmenter()
        reassembler = loop.RfReassembler()
        packets = segmenter.segment(loop.CHANNEL_PAYLOAD, b"payload-bytes")

        self.assertGreater(len(packets), 0)
        result = None
        for packet in packets:
            result = reassembler.feed(packet, 0.0)

        self.assertEqual(result, (loop.CHANNEL_PAYLOAD, b"payload-bytes"))

    def test_default_artifact_resolution_prefers_host_platform(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            project_root = Path(tmp)
            build_root = project_root / "build-artifacts"
            host = loop._host_artifact_platform()
            host_binary = build_root / host / loop.DEPLOYMENT_NAME / "bin" / loop.DEPLOYMENT_NAME
            other_binary = build_root / "OtherPlatform" / loop.DEPLOYMENT_NAME / "bin" / loop.DEPLOYMENT_NAME
            host_binary.parent.mkdir(parents=True)
            other_binary.parent.mkdir(parents=True)
            host_binary.write_text("host")
            other_binary.write_text("newer")

            self.assertEqual(loop._resolve_default_app_binary(project_root), host_binary)


class EmulationLoopPayloadPtyTests(unittest.TestCase):
    def make_emulator(
        self,
        drop_payload_data_index: int | None = None,
        blackhole_payload_data_index_first_transfer: int | None = None,
    ) -> loop.EmulationLoop:
        emulator = loop.EmulationLoop(
            app_cmd=None,
            gds_cmd=None,
            uplink_flush_ms=loop.DEFAULT_UPLINK_FLUSH_MS,
            link_mode="channelized",
            drop_payload_data_index=drop_payload_data_index,
            blackhole_payload_data_index_first_transfer=blackhole_payload_data_index_first_transfer,
        )
        emulator.app_master_fd = 10
        emulator.gds_master_fd = 11
        emulator.payload_master_fd = 12
        emulator.pending_writes = {10: bytearray(), 11: bytearray(), 12: bytearray()}
        return emulator

    def test_downlink_routes_channel_zero_and_payload_to_separate_ptys(self) -> None:
        emulator = self.make_emulator()
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        app_bytes = (
            loop.build_uart_frame(loop.CHANNEL_CCSDS, b"gds-data")
            + loop.build_uart_frame(loop.CHANNEL_PAYLOAD, b"payload-data")
        )
        with mock.patch.object(loop.os, "write", side_effect=record_write):
            emulator._process_app_to_gds(app_bytes, 0.0)

        self.assertEqual(writes, [(11, b"gds-data"), (12, b"payload-data")])
        self.assertEqual(emulator.stats.gds_bytes_out, len(b"gds-data"))
        self.assertEqual(emulator.stats.payload_bytes_out, len(b"payload-data"))

    def test_payload_data_loss_injection_drops_only_first_matching_index(self) -> None:
        emulator = self.make_emulator(drop_payload_data_index=7)
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        def data_packet(packet_index: int) -> bytes:
            return b"N2" + bytes([loop.N2_TYPE_DATA, 1]) + packet_index.to_bytes(2, "little")

        dropped = data_packet(7)
        other = data_packet(8)
        output = StringIO()
        app_bytes = (
            loop.build_uart_frame(loop.CHANNEL_PAYLOAD, dropped)
            + loop.build_uart_frame(loop.CHANNEL_PAYLOAD, other)
            + loop.build_uart_frame(loop.CHANNEL_PAYLOAD, dropped)
        )
        with mock.patch.object(loop.os, "write", side_effect=record_write), redirect_stdout(output):
            emulator._process_app_to_gds(app_bytes, 0.0)

        self.assertEqual(writes, [(12, other), (12, dropped)])
        self.assertEqual(emulator.stats.payload_data_packets_dropped, 1)
        self.assertEqual(
            emulator.stats.payload_bytes_out,
            len(other) + len(dropped),
        )
        self.assertIn(
            "[emulation] injected payload DATA drop: packet_index=7",
            output.getvalue().splitlines(),
        )

    def test_payload_data_loss_injection_cli_defaults_off_and_accepts_index(self) -> None:
        with mock.patch.object(sys, "argv", ["local_emulation_loop.py"]):
            self.assertIsNone(loop.parse_args().drop_payload_data_index)
        with mock.patch.object(
            sys,
            "argv",
            ["local_emulation_loop.py", "--drop-payload-data-index", "7"],
        ):
            self.assertEqual(loop.parse_args().drop_payload_data_index, 7)

    def test_first_transfer_blackhole_drops_repairs_but_not_next_transfer(self) -> None:
        emulator = self.make_emulator(blackhole_payload_data_index_first_transfer=7)
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        def data_packet(transfer_id: int, packet_index: int) -> bytes:
            return b"N2" + bytes([loop.N2_TYPE_DATA, transfer_id]) + packet_index.to_bytes(2, "little")

        first = data_packet(1, 7)
        second = data_packet(2, 7)
        app_bytes = (
            loop.build_uart_frame(loop.CHANNEL_PAYLOAD, first)
            + loop.build_uart_frame(loop.CHANNEL_PAYLOAD, first)
            + loop.build_uart_frame(loop.CHANNEL_PAYLOAD, second)
        )
        with mock.patch.object(loop.os, "write", side_effect=record_write):
            emulator._process_app_to_gds(app_bytes, 0.0)

        self.assertEqual(writes, [(12, second)])
        self.assertEqual(emulator.stats.payload_data_packets_dropped, 2)

    def test_payload_uplink_returns_channel_one_without_affecting_channel_zero(self) -> None:
        emulator = self.make_emulator()
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        retry_request = b"N2\x03\x07\x01\x00"
        with mock.patch.object(loop.os, "write", side_effect=record_write):
            emulator._process_payload_to_app(retry_request, 0.0)
            message = emulator.payload_burst_aggregator.poll(1.0)
            self.assertEqual(message, retry_request)
            emulator._process_payload_message_to_app(message, 1.0)  # type: ignore[arg-type]

        self.assertEqual(len(writes), 1)
        self.assertEqual(writes[0][0], 10)
        parser = loop.UartFrameParser()
        self.assertEqual(
            parser.feed(writes[0][1], 1.0),
            [(loop.CHANNEL_PAYLOAD, retry_request)],
        )
        self.assertEqual(emulator.stats.payload_messages_in, 1)
        self.assertEqual(emulator.stats.gds_messages_in, 0)
        self.assertEqual(emulator.stats.rf_packets_gds_to_app, 0)

    def test_setup_prints_stable_payload_device_marker(self) -> None:
        emulator = loop.EmulationLoop(
            app_cmd=None,
            gds_cmd=None,
            uplink_flush_ms=loop.DEFAULT_UPLINK_FLUSH_MS,
            link_mode="channelized",
        )
        output = StringIO()
        try:
            with mock.patch.object(loop.signal, "signal"), redirect_stdout(output):
                emulator.setup()
            marker = f"PAYLOAD_UART_DEVICE={emulator.payload_uart_device}"
            self.assertIn(marker, output.getvalue().splitlines())
            self.assertTrue(emulator.payload_uart_device)
        finally:
            with redirect_stdout(StringIO()):
                emulator.shutdown()


if __name__ == "__main__":
    unittest.main()
