#!/usr/bin/env python3

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from rf22_protocol import (
    DEFAULT_PROFILE,
    MessageIdStore,
    build_message_packets,
    crc16_ibm_msb,
    matches_ack,
)


class ProtocolTests(unittest.TestCase):
    def test_profile_comes_from_active_c3m_manifests(self) -> None:
        self.assertEqual(DEFAULT_PROFILE.name, "epscorc3m")
        self.assertEqual(DEFAULT_PROFILE.radio_header("downlink"), b"\xa1\xa2\xc3\x01")
        self.assertEqual(DEFAULT_PROFILE.radio_header("uplink"), b"\xa2\xa1\xc3\x01")
        self.assertEqual(DEFAULT_PROFILE.segment_max_data, 44)
        self.assertEqual(DEFAULT_PROFILE.frame_max_payload, 220)
        self.assertEqual(DEFAULT_PROFILE.ack_retries, 4)
        self.assertEqual(DEFAULT_PROFILE.ack_timeout_ms, 80)

    def test_message_segmentation_and_crc(self) -> None:
        for length, expected_segments in ((1, 1), (44, 1), (45, 2), (220, 5)):
            with self.subTest(length=length):
                packets = build_message_packets(
                    bytes(index & 0xFF for index in range(length)),
                    channel=1,
                    msg_id=0x7E,
                    direction="uplink",
                )
                self.assertEqual(len(packets), expected_segments)
                rebuilt = bytearray()
                for index, packet in enumerate(packets):
                    self.assertEqual(packet[:4], b"\xa2\xa1\xc3\x01")
                    self.assertEqual(packet[5:9], bytes((0xA6, 0x7E, index, expected_segments)))
                    chunk_len = packet[9]
                    rebuilt.extend(packet[10 : 10 + chunk_len])
                    self.assertEqual(
                        crc16_ibm_msb(packet[:-2]), int.from_bytes(packet[-2:], "big")
                    )
                self.assertEqual(len(rebuilt), length)

    def test_exact_ack_matching(self) -> None:
        expected = b"\xa5\x44\xff\x02\x00"
        self.assertTrue(
            matches_ack(expected, channel=0, msg_id=0x44, segment_index=2)
        )
        self.assertFalse(
            matches_ack(expected, channel=1, msg_id=0x44, segment_index=2)
        )
        self.assertFalse(
            matches_ack(expected, channel=0, msg_id=0x45, segment_index=2)
        )

    def test_message_ids_are_reserved_before_use_and_persist_per_channel(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ids.json"
            store = MessageIdStore(path, seed=0x20)
            self.assertEqual(store.reserve(0), 0x20)
            self.assertEqual(store.reserve(1), 0xA0)
            self.assertEqual(MessageIdStore(path).reserve(0), 0x21)
            state = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(state["next_by_channel"], {"0": 0x22, "1": 0xA1})

    def test_legacy_scalar_message_id_is_migrated(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ids"
            path.write_text("0x83\n", encoding="utf-8")
            self.assertEqual(MessageIdStore(path).reserve(0), 0x83)
            self.assertEqual(json.loads(path.read_text())["next_by_channel"]["0"], 0x84)


if __name__ == "__main__":
    unittest.main()
