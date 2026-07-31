import pathlib
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SAT_ROOT = REPO_ROOT / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy"
GROUND_ROOT = REPO_ROOT / "GDS_Teensy/firmware/gds_teensy"
HELPER_COPIES = (
    REPO_ROOT
    / "ArtemisTeensy_N2_Baremetal/firmware/libs/rf23bp/artemis_rf23bp.hpp",
    SAT_ROOT / "src/artemis_rf23bp.hpp",
    GROUND_ROOT / "src/artemis_rf23bp.hpp",
)


class RfRxBufferTransitionTests(unittest.TestCase):
    def test_radiohead_shared_buffer_failure_model(self) -> None:
        source = r'''
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct SharedRadioBuffer {
  std::array<uint8_t, 50> bytes{};
  uint8_t length = 0;

  void transmit(const std::vector<uint8_t>& packet) {
    std::copy(packet.begin(), packet.end(), bytes.begin());
    length = static_cast<uint8_t>(packet.size());
  }

  void restartReceiveClean() { length = 0; }

  bool receive(const std::vector<uint8_t>& packet) {
    if (packet.size() < length) {
      length = 0;
      return false;
    }
    std::copy(packet.begin(), packet.begin() + (packet.size() - length),
              bytes.begin() + length);
    length = static_cast<uint8_t>(packet.size());
    return true;
  }

  bool equals(const std::vector<uint8_t>& packet) const {
    return packet.size() == length &&
           std::equal(packet.begin(), packet.end(), bytes.begin());
  }
};

std::vector<uint8_t> pattern(std::size_t length, uint8_t seed) {
  std::vector<uint8_t> packet(length);
  for (std::size_t i = 0; i < length; ++i) {
    packet[i] = static_cast<uint8_t>(seed + i);
  }
  return packet;
}

bool receiveInProgress(uint32_t now, uint32_t lastPreamble, bool inRxMode) {
  return inRxMode && lastPreamble != 0U &&
         static_cast<uint32_t>(now - lastPreamble) < 6U;
}

int main() {
  const std::vector<uint8_t> ack = {0xA5, 0x12, 0xFF, 0x00, 0x00};
  const auto downlink = pattern(49, 0x10);
  const auto noArgCommand = pattern(24, 0x40);
  auto u32Command = pattern(28, 0x60);
  u32Command[0] = 0xA5;
  u32Command[1] = 0x34;
  u32Command[2] = 0x00;
  u32Command[3] = 0x01;
  u32Command[4] = 0x17;

  SharedRadioBuffer radio;
  radio.transmit(ack);
  if (!radio.receive(u32Command) || radio.equals(u32Command)) return 1;
  if (!std::equal(ack.begin(), ack.end(), radio.bytes.begin())) return 2;
  if (radio.bytes[2] != 0xFF) return 3;

  radio.transmit(downlink);
  if (radio.receive(noArgCommand)) return 4;
  radio.transmit(downlink);
  if (radio.receive(u32Command)) return 5;

  const std::array<std::size_t, 4> rxLengths = {5, 24, 28, 49};
  for (std::size_t cycle = 0; cycle < 100; ++cycle) {
    const auto tx = pattern((cycle % 2 == 0) ? 5 : 49,
                            static_cast<uint8_t>(cycle));
    const auto rx = pattern(rxLengths[cycle % rxLengths.size()],
                            static_cast<uint8_t>(cycle + 0x50));
    radio.transmit(tx);
    radio.restartReceiveClean();
    if (!radio.receive(rx) || !radio.equals(rx)) return 6;
  }
  if (!receiveInProgress(1005U, 1000U, true)) return 7;
  if (receiveInProgress(1006U, 1000U, true)) return 8;
  if (receiveInProgress(1005U, 1000U, false)) return 9;
  if (!receiveInProgress(2U, 0xFFFFFFFEU, true)) return 10;
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            cpp = root / "rf_rx_buffer_transition_test.cpp"
            executable = root / "rf_rx_buffer_transition_test"
            cpp.write_text(source)
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_production_wrapper_cleans_tx_to_rx_transition(self) -> None:
        helper_bytes = [path.read_bytes() for path in HELPER_COPIES]
        self.assertEqual(helper_bytes[0], helper_bytes[1])
        self.assertEqual(helper_bytes[0], helper_bytes[2])
        helper = helper_bytes[0].decode()

        clean_start = helper.index("void restartReceiveClean()")
        clean_end = helper.index("bool receiveInProgress", clean_start)
        clean = helper[clean_start:clean_end]
        ordered_steps = (
            "setModeIdle();",
            "resetRxFifo();",
            "RH_RF22_REG_03_INTERRUPT_STATUS1",
            "RH_RF22_REG_04_INTERRUPT_STATUS2",
            "clearRxBuf();",
            "setModeRx();",
        )
        positions = [clean.index(step) for step in ordered_steps]
        self.assertEqual(positions, sorted(positions))

        guard_start = helper.index("bool receiveInProgress(uint32_t now_ms)")
        guard_end = helper.index("bool initBounded", guard_start)
        guard = helper[guard_start:guard_end]
        for contract in (
            "mode() == RHModeRx",
            "last_preamble_ms != 0U",
            "(now_ms - last_preamble_ms) < RX_PACKET_GUARD_MS",
            "RX_PACKET_GUARD_MS = 6U",
        ):
            self.assertIn(contract, guard)

        receive_start = helper.index("inline bool receivePacket")
        receive_end = helper.index("// Read interrupt status", receive_start)
        receive = helper[receive_start:receive_end]
        self.assertNotIn("setModeRx", receive)
        self.assertNotIn("enterReceiveMode", receive)
        self.assertLess(
            receive.index("radio.available()"), receive.index("radio.recv(")
        )

        satellite_driver = (SAT_ROOT / "src/rf23_driver.cpp").read_text()
        self.assertIn(
            "return isReady() && m_radio.receiveInProgress(millis());",
            satellite_driver,
        )
        self.assertNotIn(
            "receiveInProgress",
            (GROUND_ROOT / "src/rf23_driver.cpp").read_text(),
        )

        satellite_relay = (SAT_ROOT / "src/relay_uart_rf.cpp").read_text()
        self.assertIn("RX_TURNAROUND_DWELL_MS", satellite_relay)
        self.assertIn("millis() - m_lastNormalTxCompleteMs", satellite_relay)
        self.assertIn(
            "rxTurnaroundDwellComplete && !m_rf.receiveInProgress()",
            satellite_relay,
        )
        self.assertIn("m_lastNormalTxCompleteMs = millis();", satellite_relay)


if __name__ == "__main__":
    unittest.main()
