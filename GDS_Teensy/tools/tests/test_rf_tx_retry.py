import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SAT_ROOT = REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy"
GROUND_ROOT = REPO_ROOT / "GDS_Teensy/firmware/gds_teensy"


class RfTxSingleAttemptTests(unittest.TestCase):
    def test_both_bridges_use_one_bounded_attempt(self) -> None:
        for relay_path in (
            SAT_ROOT / "src/relay_uart_rf.cpp",
            GROUND_ROOT / "src/relay_uart_rf.cpp",
        ):
            relay = relay_path.read_text()
            send_start = relay.index("bool RelayUartRf::sendRfPacket(")
            send_end = relay.index(
                "bool RelayUartRf::sendRfPacketWithAck", send_start
            )
            send = relay[send_start:send_end]
            self.assertEqual(send.count("m_rf.send(packet, packetLen)"), 1)
            self.assertNotIn("sendWithBoundedTimeoutRetry", send)
            self.assertIn("result == Rf23SendResult::TX_TIMEOUT", send)
            self.assertIn("m_rf.consumeTxTimeoutRecoveryRequest()", send)
            self.assertNotIn("m_rf.failSafeOffLocalTx();", send)

    def test_three_timeouts_trigger_sdn_recovery(self) -> None:
        for driver_path in (
            SAT_ROOT / "src/rf23_driver.cpp",
            GROUND_ROOT / "src/rf23_driver.cpp",
        ):
            driver = driver_path.read_text()
            send_start = driver.index("Rf23SendResult Rf23Driver::send(")
            send_end = driver.index(
                "artemis::rf23bp::LinkStats Rf23Driver::linkStats",
                send_start,
            ) if "artemis::rf23bp::LinkStats Rf23Driver::linkStats" in driver[
                send_start:
            ] else driver.index("void Rf23Driver::enterOff", send_start)
            send = driver[send_start:send_end]
            self.assertIn("TX_TIMEOUTS_BEFORE_RECOVERY = 3", send)
            self.assertIn("m_consecutiveTxTimeouts += 1U", send)
            self.assertIn("m_txTimeoutRecoveryRequested = true", send)
            self.assertIn("failSafeOffLocalTx();", send)

    def test_hardware_watchdog_is_not_armed(self) -> None:
        for sketch in (
            SAT_ROOT / "satellite_teensy.ino",
            GROUND_ROOT / "gds_teensy.ino",
        ):
            source = sketch.read_text()
            self.assertNotIn("wdt_guard", source)
            self.assertNotIn("hardware watchdog armed", source)


if __name__ == "__main__":
    unittest.main()
