#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
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


class RadioRpcEmulatorTests(unittest.TestCase):
    @staticmethod
    def request(request_id: int, op: int, *arguments: int) -> bytes:
        payload = bytes([op, *arguments])
        return bytes(
            [loop.TEENSY_TARGET_RF_STATUS, request_id, len(payload), 0]
        ) + payload

    def test_radio_rpc_constants_match_transport_manifest(self) -> None:
        repo_root = Path(__file__).resolve().parents[3]
        manifest = json.loads(
            (repo_root / "config" / "transport_constants.json").read_text()
        )["teensy_rpc"]
        self.assertEqual(loop.TEENSY_TARGET_RF_STATUS, manifest["target_rf_status"])
        self.assertEqual(loop.TEENSY_STATUS_TARGET_ERROR, manifest["status_target_error"])
        self.assertEqual(loop.TEENSY_RF_OP_STATUS, manifest["rf_op_status"])
        self.assertEqual(
            loop.TEENSY_RF_OP_SET_ENABLED, manifest["rf_op_set_enabled"]
        )
        self.assertEqual(loop.RADIO_STATE_OFF, manifest["rf_state_off"])
        self.assertEqual(loop.RADIO_STATE_READY, manifest["rf_state_ready"])
        self.assertEqual(loop.RADIO_FAULT_NONE, manifest["rf_fault_none"])
        self.assertEqual(
            loop.RADIO_FAULT_INIT_FAILED, manifest["rf_fault_init_failed"]
        )
        self.assertEqual(
            loop.RADIO_FAULT_WATCHDOG_RESET,
            manifest["rf_fault_watchdog_reset"],
        )
        self.assertEqual(
            loop.RADIO_BOOT_FLAG_WATCHDOG,
            manifest["rf_boot_flag_watchdog"],
        )

    def test_status_defaults_off_with_preserved_stats_prefix(self) -> None:
        radio = loop.RadioRpcEmulator()
        response = radio.handle(self.request(7, loop.TEENSY_RF_OP_STATUS), 5.0)

        self.assertIsNotNone(response)
        response = response or b""
        self.assertEqual(
            response[:4],
            bytes(
                [
                    loop.TEENSY_TARGET_RF_STATUS,
                    7,
                    loop.TEENSY_STATUS_OK,
                    loop.RADIO_STATUS_PAYLOAD_LEN,
                ]
            ),
        )
        payload = response[4:]
        self.assertEqual(len(payload), 33)
        self.assertEqual(payload[0], loop.TEENSY_RF_OP_STATUS)
        self.assertEqual(int.from_bytes(payload[1:3], "little", signed=True), 0)
        self.assertEqual(payload[3:21], bytes(18))
        self.assertEqual(payload[21], loop.RADIO_STATE_OFF)
        self.assertEqual(payload[22], loop.RADIO_FAULT_NONE)
        self.assertEqual(payload[23], 0)
        self.assertEqual(payload[24], 0)
        self.assertEqual(int.from_bytes(payload[25:29], "little"), 0xFFFFFFFF)
        self.assertEqual(int.from_bytes(payload[29:33], "little"), 0)

    def test_returned_init_failure_then_idempotent_recovery(self) -> None:
        radio = loop.RadioRpcEmulator(init_failures=1)
        enable = self.request(1, loop.TEENSY_RF_OP_SET_ENABLED, 1)
        output = StringIO()

        with redirect_stdout(output):
            failed = radio.handle(enable, 0.0)
            recovered = radio.handle(enable, 1.0)
            repeated = radio.handle(enable, 2.0)

        self.assertEqual(
            failed,
            bytes(
                [
                    loop.TEENSY_TARGET_RF_STATUS,
                    1,
                    loop.TEENSY_STATUS_TARGET_ERROR,
                    loop.RADIO_SET_ENABLED_PAYLOAD_LEN,
                    loop.TEENSY_RF_OP_SET_ENABLED,
                    1,
                    loop.RADIO_STATE_OFF,
                    loop.RADIO_FAULT_INIT_FAILED,
                ]
            ),
        )
        self.assertEqual(recovered[-4:], bytes([2, 1, 1, 0]))  # type: ignore[index]
        self.assertEqual(repeated, recovered)
        self.assertEqual(radio.init_attempts, 2)
        self.assertEqual(radio.total_enable_attempts, 2)
        self.assertEqual(radio.enable_attempt_times, [0.0, 1.0])
        self.assertTrue(radio.ready)
        self.assertIn("RADIO_STATE=READY attempt=2", output.getvalue())

    def test_watchdog_injection_drops_response_and_later_recovers(self) -> None:
        radio = loop.RadioRpcEmulator(watchdog_resets=1)
        enable = self.request(9, loop.TEENSY_RF_OP_SET_ENABLED, 1)
        with redirect_stdout(StringIO()):
            self.assertIsNone(radio.handle(enable, 0.0))

        status = radio.handle(self.request(10, loop.TEENSY_RF_OP_STATUS), 1.0)
        self.assertIsNotNone(status)
        payload = (status or b"")[4:]
        self.assertEqual(payload[21], loop.RADIO_STATE_OFF)
        self.assertEqual(payload[22], loop.RADIO_FAULT_WATCHDOG_RESET)
        self.assertEqual(payload[23], loop.RADIO_BOOT_FLAG_WATCHDOG)
        self.assertEqual(int.from_bytes(payload[29:33], "little"), 0)

        with redirect_stdout(StringIO()):
            recovered = radio.handle(enable, 2.0)
        self.assertEqual((recovered or b"")[-4:], bytes([2, 1, 1, 0]))
        self.assertEqual(radio.init_attempts, 1)
        self.assertEqual(radio.total_enable_attempts, 2)

    def test_disable_is_idempotent_and_invalid_requests_are_rejected(self) -> None:
        radio = loop.RadioRpcEmulator()
        enable = self.request(1, loop.TEENSY_RF_OP_SET_ENABLED, 1)
        disable = self.request(2, loop.TEENSY_RF_OP_SET_ENABLED, 0)
        with redirect_stdout(StringIO()):
            radio.handle(enable, 0.0)
            first = radio.handle(disable, 1.0)
            second = radio.handle(disable, 2.0)

        self.assertEqual(first, second)
        self.assertEqual((second or b"")[-4:], bytes([2, 0, 0, 0]))
        self.assertFalse(radio.ready)
        bad_bool = radio.handle(
            self.request(3, loop.TEENSY_RF_OP_SET_ENABLED, 2), 3.0
        )
        self.assertEqual(
            bad_bool,
            bytes([2, 3, loop.TEENSY_STATUS_BAD_REQUEST, 0]),
        )
        self.assertIsNone(radio.handle(bytes([1, 4, 1, 0, 1]), 4.0))


class PayloadCacheEmulatorTests(unittest.TestCase):
    TRANSFER_ID = 7
    PRODUCT_ID = 0x1002A000

    @staticmethod
    def request(request_id: int, body: bytes) -> bytes:
        return bytes(
            [
                loop.TEENSY_TARGET_PAYLOAD_CACHE,
                request_id,
                len(body),
                0,
            ]
        ) + body

    def begin_request(self, request_id: int, payload: bytes) -> bytes:
        body = (
            bytes([loop.PAYLOAD_CACHE_OP_BEGIN, self.TRANSFER_ID])
            + self.PRODUCT_ID.to_bytes(4, "little")
            + len(payload).to_bytes(4, "little")
            + loop.crc16_ccitt(payload).to_bytes(2, "little")
        )
        return self.request(request_id, body)

    def chunk_request(
        self, request_id: int, offset: int, payload: bytes
    ) -> bytes:
        body = (
            bytes([loop.PAYLOAD_CACHE_OP_CHUNK, self.TRANSFER_ID])
            + offset.to_bytes(4, "little")
            + bytes([len(payload)])
            + payload
        )
        return self.request(request_id, body)

    def load_and_commit(
        self, cache: loop.PayloadCacheEmulator, payload: bytes
    ) -> bytes:
        response = cache.handle(self.begin_request(1, payload))
        self.assertEqual((response or b"")[6], loop.PAYLOAD_CACHE_STATE_RECEIVING)
        for request_id, offset in enumerate(
            range(0, len(payload), loop.PAYLOAD_CACHE_CHUNK_BYTES),
            start=2,
        ):
            chunk = payload[offset : offset + loop.PAYLOAD_CACHE_CHUNK_BYTES]
            response = cache.handle(self.chunk_request(request_id, offset, chunk))
            self.assertEqual((response or b"")[2], loop.TEENSY_STATUS_OK)
            self.assertEqual(
                int.from_bytes((response or b"")[8:12], "little"),
                offset + len(chunk),
            )
        commit_id = 99
        commit = self.request(
            commit_id,
            bytes([loop.PAYLOAD_CACHE_OP_COMMIT_AND_SEND, self.TRANSFER_ID]),
        )
        response = cache.handle(commit)
        self.assertEqual((response or b"")[6], loop.PAYLOAD_CACHE_STATE_SENDING)
        return response or b""

    def test_cache_constants_match_transport_manifest(self) -> None:
        repo_root = Path(__file__).resolve().parents[3]
        manifest = json.loads(
            (repo_root / "config" / "transport_constants.json").read_text()
        )
        rpc = manifest["teensy_rpc"]
        payload = manifest["payload"]

        self.assertEqual(
            loop.TEENSY_TARGET_PAYLOAD_CACHE, rpc["target_payload_cache"]
        )
        self.assertEqual(loop.TEENSY_STATUS_BUSY, rpc["status_busy"])
        self.assertEqual(
            loop.PAYLOAD_CACHE_MAX_BYTES, payload["cache_max_bytes"]
        )
        self.assertEqual(
            loop.PAYLOAD_CACHE_CHUNK_BYTES, payload["cache_chunk_bytes"]
        )
        self.assertEqual(
            loop.PAYLOAD_PACKET_DATA_BYTES, payload["packet_data_bytes"]
        )
        self.assertEqual(
            [
                loop.PAYLOAD_CACHE_OP_BEGIN,
                loop.PAYLOAD_CACHE_OP_CHUNK,
                loop.PAYLOAD_CACHE_OP_COMMIT_AND_SEND,
                loop.PAYLOAD_CACHE_OP_ABORT,
            ],
            [
                payload["cache_op_begin"],
                payload["cache_op_chunk"],
                payload["cache_op_commit_and_send"],
                payload["cache_op_abort"],
            ],
        )
        self.assertEqual(
            [
                loop.PAYLOAD_CACHE_STATE_EMPTY,
                loop.PAYLOAD_CACHE_STATE_RECEIVING,
                loop.PAYLOAD_CACHE_STATE_READY,
                loop.PAYLOAD_CACHE_STATE_SENDING,
                loop.PAYLOAD_CACHE_STATE_ERROR,
            ],
            [
                payload["cache_state_empty"],
                payload["cache_state_receiving"],
                payload["cache_state_ready"],
                payload["cache_state_sending"],
                payload["cache_state_error"],
            ],
        )

    def test_begin_chunk_commit_emits_exact_n2_product(self) -> None:
        payload = bytes((index * 17 + 3) & 0xFF for index in range(83))
        cache = loop.PayloadCacheEmulator()

        commit_response = self.load_and_commit(cache, payload)
        self.assertEqual(commit_response[2], loop.TEENSY_STATUS_OK)
        self.assertEqual(
            int.from_bytes(commit_response[8:12], "little"), len(payload)
        )

        packets: list[bytes] = []
        while True:
            packet = cache.next_payload_packet()
            if packet is None:
                break
            packets.append(packet)

        total_packets = (
            len(payload) + loop.PAYLOAD_PACKET_DATA_BYTES - 1
        ) // loop.PAYLOAD_PACKET_DATA_BYTES
        self.assertEqual(
            [packet[2] for packet in packets],
            [loop.N2_TYPE_HEADER] * loop.PAYLOAD_CACHE_HEADER_REPEATS
            + [loop.N2_TYPE_DATA] * total_packets
            + [loop.N2_TYPE_END],
        )
        self.assertEqual(
            packets[0],
            b"N2"
            + bytes([loop.N2_TYPE_HEADER, self.TRANSFER_ID])
            + self.PRODUCT_ID.to_bytes(4, "little")
            + len(payload).to_bytes(4, "little")
            + total_packets.to_bytes(2, "little")
            + bytes([loop.PAYLOAD_PACKET_DATA_BYTES])
            + loop.crc16_ccitt(payload).to_bytes(2, "little"),
        )

        reconstructed = bytearray()
        for packet_index, packet in enumerate(
            packets[
                loop.PAYLOAD_CACHE_HEADER_REPEATS :
                loop.PAYLOAD_CACHE_HEADER_REPEATS + total_packets
            ]
        ):
            self.assertEqual(
                int.from_bytes(packet[4:6], "little"), packet_index
            )
            valid_bytes = packet[6]
            crc_offset = 7 + valid_bytes
            self.assertEqual(
                int.from_bytes(packet[crc_offset : crc_offset + 2], "little"),
                loop.crc16_ccitt(packet[:crc_offset]),
            )
            reconstructed.extend(packet[7:crc_offset])
        self.assertEqual(bytes(reconstructed), payload)

        completion = cache.take_completion_response()
        self.assertEqual((completion or b"")[1], 99)
        self.assertEqual((completion or b"")[6], loop.PAYLOAD_CACHE_STATE_READY)
        self.assertEqual(cache.state, loop.PAYLOAD_CACHE_STATE_READY)

    def test_oversized_begin_is_rejected_without_allocating_cache(self) -> None:
        cache = loop.PayloadCacheEmulator()
        body = (
            bytes([loop.PAYLOAD_CACHE_OP_BEGIN, self.TRANSFER_ID])
            + self.PRODUCT_ID.to_bytes(4, "little")
            + (loop.PAYLOAD_CACHE_MAX_BYTES + 1).to_bytes(4, "little")
            + b"\x00\x00"
        )
        response = cache.handle(self.request(1, body))

        self.assertEqual((response or b"")[2], loop.TEENSY_STATUS_BAD_REQUEST)
        self.assertEqual(cache.state, loop.PAYLOAD_CACHE_STATE_EMPTY)
        self.assertEqual(cache.cache, bytearray())

    def test_retry_request_emits_only_requested_data_then_end(self) -> None:
        payload = bytes(index & 0xFF for index in range(150))
        cache = loop.PayloadCacheEmulator()
        self.load_and_commit(cache, payload)
        while cache.next_payload_packet() is not None:
            pass
        self.assertIsNotNone(cache.take_completion_response())

        retry = (
            b"N2"
            + bytes([loop.N2_TYPE_RETRY_REQUEST, self.TRANSFER_ID])
            + (1).to_bytes(2, "little")
            + bytes([1, 0b00000101])
        )
        self.assertTrue(cache.handle_payload_control(retry))
        repair_packets = []
        while True:
            packet = cache.next_payload_packet()
            if packet is None:
                break
            repair_packets.append(packet)

        self.assertEqual(
            [packet[2] for packet in repair_packets],
            [loop.N2_TYPE_DATA, loop.N2_TYPE_DATA, loop.N2_TYPE_END],
        )
        self.assertEqual(
            [int.from_bytes(packet[4:6], "little") for packet in repair_packets[:2]],
            [1, 3],
        )


class EmulationLoopPayloadPtyTests(unittest.TestCase):
    def make_emulator(
        self,
        drop_payload_data_index: int | None = None,
        blackhole_payload_data_index_first_transfer: int | None = None,
        radio_ready: bool = True,
        radio_init_failures: int = 0,
        radio_watchdog_resets: int = 0,
    ) -> loop.EmulationLoop:
        emulator = loop.EmulationLoop(
            app_cmd=None,
            gds_cmd=None,
            uplink_flush_ms=loop.DEFAULT_UPLINK_FLUSH_MS,
            link_mode="channelized",
            drop_payload_data_index=drop_payload_data_index,
            blackhole_payload_data_index_first_transfer=blackhole_payload_data_index_first_transfer,
            radio_init_failures=radio_init_failures,
            radio_watchdog_resets=radio_watchdog_resets,
        )
        if radio_ready:
            emulator.radio.state = loop.RADIO_STATE_READY
        emulator.app_master_fd = 10
        emulator.gds_master_fd = 11
        emulator.payload_master_fd = 12
        emulator.pending_writes = {10: bytearray(), 11: bytearray(), 12: bytearray()}
        return emulator

    def test_channel_two_enable_loops_back_locally_and_unlocks_rf(self) -> None:
        emulator = self.make_emulator(radio_ready=False)
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        request = bytes([2, 8, 2, 0, loop.TEENSY_RF_OP_SET_ENABLED, 1])
        app_bytes = loop.build_uart_frame(loop.CHANNEL_TEENSY_LOCAL, request)
        with mock.patch.object(loop.os, "write", side_effect=record_write), redirect_stdout(StringIO()):
            emulator._process_app_to_gds(app_bytes, 0.0)

        self.assertEqual(len(writes), 1)
        self.assertEqual(writes[0][0], 10)
        parser = loop.UartFrameParser()
        self.assertEqual(
            parser.feed(writes[0][1], 0.0),
            [
                (
                    loop.CHANNEL_TEENSY_LOCAL,
                    bytes([2, 8, 0, 4, 2, 1, 1, 0]),
                )
            ],
        )
        self.assertTrue(emulator.radio.ready)
        self.assertEqual(emulator.stats.local_frames_observed, 1)
        self.assertEqual(emulator.stats.local_responses_sent, 1)

    def test_channel_two_target_three_routes_to_single_payload_cache(self) -> None:
        emulator = self.make_emulator(radio_ready=False)
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        payload = b"one-standard-fdp"
        body = (
            bytes([loop.PAYLOAD_CACHE_OP_BEGIN, 4])
            + (0x1002A000).to_bytes(4, "little")
            + len(payload).to_bytes(4, "little")
            + loop.crc16_ccitt(payload).to_bytes(2, "little")
        )
        request = bytes(
            [loop.TEENSY_TARGET_PAYLOAD_CACHE, 9, len(body), 0]
        ) + body
        app_bytes = loop.build_uart_frame(loop.CHANNEL_TEENSY_LOCAL, request)

        with mock.patch.object(loop.os, "write", side_effect=record_write):
            emulator._process_app_to_gds(app_bytes, 0.0)

        self.assertEqual(len(writes), 1)
        parser = loop.UartFrameParser()
        responses = parser.feed(writes[0][1], 0.0)
        self.assertEqual(len(responses), 1)
        channel, response = responses[0]
        self.assertEqual(channel, loop.CHANNEL_TEENSY_LOCAL)
        self.assertEqual(response[0], loop.TEENSY_TARGET_PAYLOAD_CACHE)
        self.assertEqual(response[1], 9)
        self.assertEqual(response[2], loop.TEENSY_STATUS_OK)
        self.assertEqual(response[4], loop.PAYLOAD_CACHE_OP_BEGIN)
        self.assertEqual(response[5], 4)
        self.assertEqual(response[6], loop.PAYLOAD_CACHE_STATE_RECEIVING)
        self.assertEqual(emulator.payload_cache.total_bytes, len(payload))
        self.assertFalse(emulator.radio.ready)

    def test_radio_off_drops_both_rf_directions_but_not_local_rpc(self) -> None:
        emulator = self.make_emulator(radio_ready=False)
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        status_request = bytes([2, 1, 1, 0, loop.TEENSY_RF_OP_STATUS])
        app_bytes = (
            loop.build_uart_frame(loop.CHANNEL_CCSDS, b"downlink")
            + loop.build_uart_frame(loop.CHANNEL_TEENSY_LOCAL, status_request)
        )
        with mock.patch.object(loop.os, "write", side_effect=record_write):
            emulator._process_app_to_gds(app_bytes, 0.0)
            emulator._process_ground_message_to_app(loop.CHANNEL_CCSDS, b"uplink", 0.0)

        self.assertEqual([fd for fd, _ in writes], [10])
        self.assertEqual(emulator.stats.radio_frames_dropped_off, 2)
        self.assertEqual(emulator.stats.local_responses_sent, 1)

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
            args = loop.parse_args()
            self.assertIsNone(args.drop_payload_data_index)
            self.assertEqual(args.radio_init_failures, 0)
            self.assertEqual(args.radio_watchdog_resets, 0)
        with mock.patch.object(
            sys,
            "argv",
            ["local_emulation_loop.py", "--drop-payload-data-index", "7"],
        ):
            self.assertEqual(loop.parse_args().drop_payload_data_index, 7)

    def test_radio_fault_cli_accepts_one_non_negative_mode(self) -> None:
        with mock.patch.object(
            sys,
            "argv",
            ["local_emulation_loop.py", "--radio-init-failures", "2"],
        ):
            self.assertEqual(loop.parse_args().radio_init_failures, 2)
        with mock.patch.object(
            sys,
            "argv",
            ["local_emulation_loop.py", "--radio-watchdog-resets", "1"],
        ):
            self.assertEqual(loop.parse_args().radio_watchdog_resets, 1)
        with mock.patch.object(
            sys,
            "argv",
            ["local_emulation_loop.py", "--radio-init-failures", "-1"],
        ), redirect_stderr(StringIO()):
            with self.assertRaises(SystemExit):
                loop.parse_args()
        with mock.patch.object(
            sys,
            "argv",
            [
                "local_emulation_loop.py",
                "--radio-init-failures",
                "1",
                "--radio-watchdog-resets",
                "1",
            ],
        ), redirect_stderr(StringIO()):
            with self.assertRaises(SystemExit):
                loop.parse_args()

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

    def test_non_cache_payload_uplink_returns_channel_one_without_affecting_channel_zero(self) -> None:
        emulator = self.make_emulator()
        writes: list[tuple[int, bytes]] = []

        def record_write(fd: int, data: bytes | bytearray) -> int:
            writes.append((fd, bytes(data)))
            return len(data)

        control_message = b"N2\x03\x07\x01\x00"
        with mock.patch.object(loop.os, "write", side_effect=record_write):
            emulator._process_payload_to_app(control_message, 0.0)
            message = emulator.payload_burst_aggregator.poll(1.0)
            self.assertEqual(message, control_message)
            emulator._process_payload_message_to_app(message, 1.0)  # type: ignore[arg-type]

        self.assertEqual(len(writes), 1)
        self.assertEqual(writes[0][0], 10)
        parser = loop.UartFrameParser()
        self.assertEqual(
            parser.feed(writes[0][1], 1.0),
            [(loop.CHANNEL_PAYLOAD, control_message)],
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
