#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path


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

    def test_rf_message_ids_advance_independently_per_channel(self) -> None:
        segmenter = loop.RfSegmenter()
        ccsds_first = segmenter.segment(loop.CHANNEL_CCSDS, b"a")[0]
        payload_first = segmenter.segment(loop.CHANNEL_PAYLOAD, b"b")[0]
        ccsds_second = segmenter.segment(loop.CHANNEL_CCSDS, b"c")[0]

        self.assertEqual(ccsds_first[1], 0)
        self.assertEqual(payload_first[1], 0)
        self.assertEqual(ccsds_second[1], 1)


class RadioRpcEmulatorTests(unittest.TestCase):
    @staticmethod
    def request(request_id: int, operation: int, enabled: int | None = None) -> bytes:
        payload = bytes([operation]) if enabled is None else bytes([operation, enabled])
        return bytes([loop.TEENSY_TARGET_RF_STATUS, request_id, len(payload), 0]) + payload

    def test_status_then_enable_matches_satellite_contract(self) -> None:
        radio = loop.RadioRpcEmulator()
        status = radio.handle(self.request(7, loop.TEENSY_RF_OP_STATUS), 1.0)
        self.assertIsNotNone(status)
        assert status is not None
        self.assertEqual(status[:4], bytes([loop.TEENSY_TARGET_RF_STATUS, 7, loop.TEENSY_STATUS_OK, 33]))
        self.assertEqual(status[25], loop.RADIO_STATE_OFF)

        enabled = radio.handle(self.request(8, loop.TEENSY_RF_OP_SET_ENABLED, 1), 2.0)
        self.assertEqual(
            enabled,
            bytes(
                [
                    loop.TEENSY_TARGET_RF_STATUS,
                    8,
                    loop.TEENSY_STATUS_OK,
                    4,
                    loop.TEENSY_RF_OP_SET_ENABLED,
                    1,
                    loop.RADIO_STATE_READY,
                    loop.RADIO_FAULT_NONE,
                ]
            ),
        )
        self.assertTrue(radio.ready)

    def test_init_failure_returns_factual_off_state(self) -> None:
        radio = loop.RadioRpcEmulator(init_failures=1)
        response = radio.handle(self.request(1, loop.TEENSY_RF_OP_SET_ENABLED, 1), 0.0)
        self.assertIsNotNone(response)
        assert response is not None
        self.assertEqual(response[2], loop.TEENSY_STATUS_TARGET_ERROR)
        self.assertEqual(response[-2:], bytes([loop.RADIO_STATE_OFF, loop.RADIO_FAULT_INIT_FAILED]))


if __name__ == "__main__":
    unittest.main()
