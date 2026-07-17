import pathlib
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GROUND_ROOT = REPO_ROOT / "GDS_Teensy/firmware/gds_teensy"
SAT_ROOT = REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy"
SHARED_HELPER = (
    REPO_ROOT
    / "ArtemisTeensy_N2_Baremetal/firmware/libs/rf23bp/artemis_rf23bp.hpp"
)
HELPER_COPIES = (
    SHARED_HELPER,
    SAT_ROOT / "src/artemis_rf23bp.hpp",
    GROUND_ROOT / "src/artemis_rf23bp.hpp",
)


class RfRecoveryHardeningTests(unittest.TestCase):
    def test_recovery_schedule_backoff_and_rollover(self) -> None:
        source = r'''
#include <cstdint>
#include "rf_recovery_policy.hpp"

int main() {
  rf_recovery::Schedule schedule;
  if (schedule.pending() || schedule.due(0)) return 1;

  schedule.requestImmediate(100);
  if (!schedule.pending() || !schedule.due(100) || schedule.currentBackoffMs() != 0) return 2;
  schedule.failed(100);
  if (schedule.deadlineMs() != 1100 || schedule.currentBackoffMs() != 1000) return 3;
  if (schedule.due(1099) || !schedule.due(1100)) return 4;

  schedule.failed(1100);
  if (schedule.deadlineMs() != 6100 || schedule.currentBackoffMs() != 5000) return 5;
  schedule.failed(6100);
  if (schedule.deadlineMs() != 36100 || schedule.currentBackoffMs() != 30000) return 6;
  schedule.failed(36100);
  if (schedule.deadlineMs() != 66100 || schedule.currentBackoffMs() != 30000) return 7;
  schedule.succeeded();
  if (schedule.pending() || schedule.currentBackoffMs() != 0) return 8;

  constexpr uint32_t nearWrap = 0xFFFFFFF0U;
  schedule.requestImmediate(nearWrap);
  schedule.failed(nearWrap);
  const uint32_t deadline = schedule.deadlineMs();
  if (schedule.due(static_cast<uint32_t>(deadline - 1U))) return 9;
  if (!schedule.due(deadline)) return 10;
  return 0;
}
'''
        header_dir = GROUND_ROOT / "src"
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            cpp = root / "rf_recovery_policy_test.cpp"
            executable = root / "rf_recovery_policy_test"
            cpp.write_text(source)
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(header_dir),
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_shared_helper_snapshot_and_bounded_paths(self) -> None:
        helper_bytes = [path.read_bytes() for path in HELPER_COPIES]
        self.assertEqual(helper_bytes[0], helper_bytes[1])
        self.assertEqual(helper_bytes[0], helper_bytes[2])
        helper = helper_bytes[0].decode()

        for contract in (
            "struct FaultSnapshot",
            "class BoundedRf22",
            "initBounded",
            "chip_ready_timeout_ms",
            "captureFirstFaultSnapshot",
            "first_fault->valid",
        ):
            self.assertIn(contract, helper)

        capture_start = helper.index("inline void captureFirstFaultSnapshot")
        capture_end = helper.index("// Recover the radio", capture_start)
        capture = helper[capture_start:capture_end]
        ordered_reads = (
            "digitalRead(pins.irq_pin)",
            "radio.mode()",
            "RH_RF22_REG_00_DEVICE_TYPE",
            "RH_RF22_REG_01_VERSION_CODE",
            "RH_RF22_REG_02_DEVICE_STATUS",
            "RH_RF22_REG_05_INTERRUPT_ENABLE1",
            "RH_RF22_REG_06_INTERRUPT_ENABLE2",
            "RH_RF22_REG_07_OPERATING_MODE1",
            "RH_RF22_REG_08_OPERATING_MODE2",
            "RH_RF22_REG_26_RSSI",
            "RH_RF22_REG_03_INTERRUPT_STATUS1",
            "RH_RF22_REG_04_INTERRUPT_STATUS2",
            "snapshot.valid = true",
        )
        positions = [capture.index(token) for token in ordered_reads]
        self.assertEqual(positions, sorted(positions))

        send_start = helper.index("inline SendResult sendPacket")
        send_end = helper.index("// Receive one packet", send_start)
        send = helper[send_start:send_end]
        pre_guard = send.index("radio.mode() == RHGenericDriver::RHModeTx")
        radio_send = send.index("radio.send(data, len)")
        self.assertLess(pre_guard, radio_send)
        timeout_capture = send.rindex(
            "captureFirstFaultSnapshot(radio, pins, SendResult::TX_TIMEOUT"
        )
        timeout_recovery = send.rindex("recoverTransmitPath(radio, pins, profile)")
        self.assertLess(timeout_capture, timeout_recovery)

        for driver in (
            GROUND_ROOT / "src/rf23_driver.hpp",
            SAT_ROOT / "src/rf23_driver.hpp",
        ):
            self.assertIn("artemis::rf23bp::BoundedRf22 m_radio;", driver.read_text())

    def test_ground_terminal_fault_containment_contract(self) -> None:
        relay = (GROUND_ROOT / "src/relay_uart_rf.cpp").read_text()
        header = (GROUND_ROOT / "src/relay_uart_rf.hpp").read_text()
        driver_header = (GROUND_ROOT / "src/rf23_driver.hpp").read_text()
        driver = (GROUND_ROOT / "src/rf23_driver.cpp").read_text()
        sketch = (GROUND_ROOT / "gds_teensy.ino").read_text()

        for contract in (
            "beginSafeOff",
            "serviceRecovery",
            "failSafeOffLocalTx",
            "consumeFaultSnapshot",
            "recoveryBackoffMs",
        ):
            self.assertIn(contract, driver_header)
        self.assertIn("return isReady() && m_radio.available();", driver)
        self.assertGreaterEqual(driver.count("if (!isReady())"), 2)

        terminal = relay.index("if (outcome.terminalFailure)")
        terminal_end = relay.index("return outcome.sent", terminal)
        self.assertIn("m_rf.failSafeOffLocalTx();", relay[terminal:terminal_end])

        poll_start = relay.index("void RelayUartRf::poll()")
        poll_end = relay.index("void RelayUartRf::processUartByte", poll_start)
        poll = relay[poll_start:poll_end]
        service = poll.rindex("serviceUplinkQueue();")
        purge_after_pop = poll.index("discardRadioWorkOnOff();", service)
        self.assertLess(service, purge_after_pop)
        self.assertIn("if (!m_lastRadioReady)", poll)

        discard_start = relay.index("void RelayUartRf::discardRadioWorkOnOff()")
        discard_end = relay.index("void RelayUartRf::serviceDownlinkQueue", discard_start)
        discard = relay[discard_start:discard_end]
        for contract in (
            "while (m_linkIo.available() > 0)",
            "while (m_payloadIo->available() > 0)",
            "m_uplinkCount = 0;",
            "m_rawUartLen = 0;",
            "m_payloadUartLen = 0;",
            "resetFrameParser(false);",
            "resetReassembly(channel, false, partialMessage);",
        ):
            self.assertIn(contract, discard)
        for preserved_downlink_state in (
            "m_downlinkQueue",
            "m_downlinkHead",
            "m_downlinkTail",
            "m_downlinkCount",
        ):
            self.assertNotIn(preserved_downlink_state, discard)

        ack_start = relay.index("bool RelayUartRf::sendRfPacketWithAck")
        ack_end = relay.index("bool RelayUartRf::waitForAck", ack_start)
        self.assertNotIn("failSafeOffLocalTx", relay[ack_start:ack_end])
        self.assertIn("bool m_lastRadioReady;", header)

        self.assertIn("RADIO_SDN_PIN = 37", sketch)
        self.assertIn("[GDS_Teensy] RF_FAULT", sketch)
        self.assertIn("rf_sdn_recoveries=", sketch)
        self.assertLess(sketch.index("g_relay.poll();"), sketch.index("g_rfDriver.serviceRecovery();"))
        self.assertNotIn("readAndClearInterruptStatus", sketch)

    def test_completed_message_duplicates_are_not_forwarded(self) -> None:
        for relay_path in (
            GROUND_ROOT / "src/relay_uart_rf.cpp",
            SAT_ROOT / "src/relay_uart_rf.cpp",
        ):
            relay = relay_path.read_text()
            with self.subTest(relay=relay_path):
                duplicate_guard = (
                    "!state.active && state.seenRxMsgId && "
                    "msgId == state.lastRxMsgId"
                )
                self.assertIn(duplicate_guard, relay)
                guard_start = relay.index(duplicate_guard)
                guard_end = relay.index("if (!state.active)", guard_start)
                guard = relay[guard_start:guard_end]
                self.assertIn("sendAck(channel, msgId, segIdx);", guard)
                self.assertIn("return;", guard)
                self.assertNotIn("enqueueDownlinkMessage", guard)


if __name__ == "__main__":
    unittest.main()
