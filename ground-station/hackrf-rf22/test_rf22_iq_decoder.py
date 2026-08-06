#!/usr/bin/env python3

import unittest
from tempfile import TemporaryDirectory
from pathlib import Path

from rf22_iq_decoder import (
    Rf22Frame,
    SegmentReassembler,
    StreamingCs8Decoder,
    crc16_ibm_msb,
    decode_cs8_file,
)
from rf22_protocol import build_message_packets
from rf22_tx import build_rf22_packet, modulate


class DecoderTests(unittest.TestCase):
    def test_captured_frame_crc(self) -> None:
        frame = bytes.fromhex(
            "a1 a2 c3 01 31 a5 17 00 03 2c 04 42 1e 1e 18 00 00 01 "
            "c5 25 00 59 00 01 02 00 00 00 00 02 00 6a 61 4d 86 00 "
            "02 81 83 00 00 00 00 00 00 00 01 02 00 00 01 00 02 00 "
            "1a dd"
        )
        self.assertEqual(crc16_ibm_msb(frame[:-2]), int.from_bytes(frame[-2:], "big"))

    def test_reassembles_channel_zero(self) -> None:
        reassembler = SegmentReassembler()
        first = Rf22Frame(1.0, b"\xa1\xa2\xc3\x01", b"\xa5\x17\x00\x02\x03abc", 0)
        second = Rf22Frame(1.1, b"\xa1\xa2\xc3\x01", b"\xa5\x17\x01\x02\x02de", 0)
        self.assertIsNone(reassembler.accept(first))
        message = reassembler.accept(second)
        self.assertIsNotNone(message)
        assert message is not None
        self.assertEqual(message.channel, 0)
        self.assertEqual(message.msg_id, 0x17)
        self.assertEqual(message.data, b"abcde")

    def test_synthetic_iq_round_trip_is_durable(self) -> None:
        packet = build_message_packets(
            b"payload-channel-one",
            channel=1,
            msg_id=0x44,
            direction="downlink",
        )[0]
        raw = modulate(
            packet,
            repeat=1,
            leading_silence_s=0.030,
            inter_burst_silence_s=0,
            trailing_silence_s=0.030,
        )
        with TemporaryDirectory() as directory:
            capture = Path(directory) / "synthetic.cs8"
            capture.write_bytes(raw)
            frames = list(
                decode_cs8_file(capture, chunk_seconds=0.020, overlap_seconds=0.010)
            )
        self.assertEqual(len(frames), 1)
        message = SegmentReassembler().accept(frames[0])
        self.assertIsNotNone(message)
        assert message is not None
        self.assertEqual((message.channel, message.msg_id, message.data), (1, 0x44, b"payload-channel-one"))

    def test_streaming_decoder_keeps_frame_crossing_block_boundary(self) -> None:
        packet = build_message_packets(
            b"boundary", channel=0, msg_id=7, direction="downlink"
        )[0]
        raw = modulate(
            packet,
            repeat=1,
            leading_silence_s=0.039,
            inter_burst_silence_s=0,
            trailing_silence_s=0.050,
        )
        decoder = StreamingCs8Decoder(overlap_seconds=0.025)
        block_bytes = int(0.040 * 8_000_000) * 2
        frames = []
        for offset in range(0, len(raw), block_bytes):
            frames.extend(decoder.feed(raw[offset : offset + block_bytes]))
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].payload[5:], b"boundary")

    def test_reassembly_timeout_and_completed_duplicate_are_bounded(self) -> None:
        reassembler = SegmentReassembler()
        first = Rf22Frame(1.0, b"\xa1\xa2\xc3\x01", b"\xa5\x10\x00\x02\x03abc", 0)
        late = Rf22Frame(1.6, b"\xa1\xa2\xc3\x01", b"\xa5\x10\x01\x02\x02de", 0)
        self.assertIsNone(reassembler.accept(first))
        self.assertIsNone(reassembler.accept(late))
        self.assertEqual(reassembler.timeouts, 1)

        complete = Rf22Frame(2.0, b"\xa1\xa2\xc3\x01", b"\xa5\x11\x00\x01\x03xyz", 0)
        self.assertIsNotNone(reassembler.accept(complete))
        self.assertIsNone(reassembler.accept(complete))
        self.assertEqual(reassembler.duplicates, 1)

    def test_bad_crc_synthetic_iq_is_rejected(self) -> None:
        packet = bytearray(
            build_message_packets(b"bad-crc", channel=0, msg_id=3, direction="downlink")[0]
        )
        packet[-1] ^= 0x01
        raw = modulate(
            bytes(packet),
            repeat=1,
            leading_silence_s=0.020,
            inter_burst_silence_s=0,
            trailing_silence_s=0.020,
        )
        with TemporaryDirectory() as directory:
            capture = Path(directory) / "bad.cs8"
            capture.write_bytes(raw)
            self.assertEqual(list(decode_cs8_file(capture)), [])

    def test_uplink_packet_contract(self) -> None:
        packet = build_rf22_packet(b"abc", 0x80)
        self.assertEqual(packet[:5], b"\xa2\xa1\xc3\x01\x08")
        self.assertEqual(packet[5:10], b"\xa5\x80\x00\x01\x03")
        self.assertEqual(crc16_ibm_msb(packet[:-2]), int.from_bytes(packet[-2:], "big"))


if __name__ == "__main__":
    unittest.main()
