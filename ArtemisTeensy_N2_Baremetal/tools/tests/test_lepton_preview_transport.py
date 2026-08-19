#!/usr/bin/env python3
"""Structural contract checks for the isolated Lepton-preview relay path."""

from pathlib import Path
import unittest


SATELLITE_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = Path(__file__).resolve().parents[3]
SRC = SATELLITE_ROOT / "firmware/satellite_teensy/src"


class LeptonPreviewTransportTest(unittest.TestCase):
    def test_manifest_has_bounded_preview_contract(self):
        manifest = (WORKSPACE_ROOT / "config/transport_constants.json").read_text()
        self.assertIn('"target_lepton_preview": 4', manifest)
        self.assertIn('"max_frame_bytes": 4800', manifest)
        self.assertIn('"chunk_bytes": 200', manifest)
        self.assertIn('"rf_gap_ms": 2', manifest)

    def test_preview_records_are_one_shot_and_single_segment(self):
        relay = (SRC / "relay_uart_rf.cpp").read_text()
        self.assertIn("sendPreviewPacketBestEffort", relay)
        self.assertIn("length > link_protocol::RF_SEGMENT_MAX_DATA", relay)
        self.assertIn("m_rf.send(rfPacket", relay)
        self.assertNotIn("sendRfPacketWithAck(rfPacket", relay[relay.index("sendPreviewPacketBestEffort"):relay.index("bool RelayUartRf::sendRfPacket(")])
        self.assertIn("LEPTON_PREVIEW_RF_GAP_MS", relay)

    def test_mutual_exclusion_returns_target_specific_response_shape(self):
        router = (SRC / "local_teensy_router.cpp").read_text()
        cache = (SRC / "payload_cache.cpp").read_text()
        preview = (SRC / "lepton_preview.cpp").read_text()
        preview_header = (SRC / "lepton_preview.hpp").read_text()
        self.assertIn("m_payloadCache.rejectBusy(requestId, requestOperation)", router)
        self.assertIn("m_leptonPreview.rejectBusy(requestId, requestOperation)", router)
        self.assertIn("prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, operation)", cache)
        self.assertIn("prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, operation)", preview)
        self.assertIn("RESPONSE_BODY_LEN = 8U", preview_header)

    def test_local_response_is_drained_per_completed_local_frame(self):
        relay = (SRC / "relay_uart_rf.cpp").read_text()
        local_frame = relay[relay.index("if (m_frameChannel == link_protocol::CHANNEL_TEENSY_LOCAL)"):]
        self.assertIn("flushLocalResponseToUart();", local_frame[:700])
        self.assertIn("localResponsesTx", relay)


if __name__ == "__main__":
    unittest.main()
