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


if __name__ == "__main__":
    unittest.main()
