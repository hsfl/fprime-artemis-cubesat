import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SAT_RELAY = (
    REPO_ROOT
    / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.cpp"
)
GROUND_RELAY = REPO_ROOT / "GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.cpp"
LOAD_TEST = (
    REPO_ROOT
    / "GDS_Teensy/firmware/gds_teensy/src/gds_tx_load_test.hpp"
)


class RfTxSingleAttemptTests(unittest.TestCase):
    def test_both_bridges_use_one_bounded_attempt(self) -> None:
        for relay_path in (SAT_RELAY, GROUND_RELAY):
            relay = relay_path.read_text()
            send_start = relay.index("bool RelayUartRf::sendRfPacket(")
            send_end = relay.index("bool RelayUartRf::sendRfPacketWithAck", send_start)
            send = relay[send_start:send_end]
            self.assertEqual(send.count("m_rf.send(packet, packetLen)"), 1)
            self.assertNotIn("sendWithBoundedTimeoutRetry", send)
            self.assertIn("result == Rf23SendResult::TX_TIMEOUT", send)
            self.assertIn("m_rf.consumeTxTimeoutRecoveryRequest()", send)
            self.assertNotIn("m_rf.failSafeOffLocalTx();", send)

    def test_load_test_calls_driver_directly(self) -> None:
        load_test = LOAD_TEST.read_text()
        tick_start = load_test.index("void tickRf(")
        tick_end = load_test.index("void tickUsb(", tick_start)
        tick = load_test[tick_start:tick_end]
        self.assertEqual(tick.count("m_radio.send(m_packet, m_rfPacketBytes)"), 1)
        self.assertNotIn("diagnosticSendRfPacket", tick)
        self.assertIn("m_radio.consumeTxTimeoutRecoveryRequest()", tick)
        self.assertNotIn("m_radio.failSafeOffLocalTx();", tick)


if __name__ == "__main__":
    unittest.main()
