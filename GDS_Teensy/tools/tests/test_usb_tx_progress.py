import pathlib
import re
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
GDS_ROOT = REPO_ROOT / "GDS_Teensy"
HEADER = GDS_ROOT / "firmware/gds_teensy/src/usb_tx_progress.hpp"
RELAY_CPP = GDS_ROOT / "firmware/gds_teensy/src/relay_uart_rf.cpp"
RELAY_HPP = GDS_ROOT / "firmware/gds_teensy/src/relay_uart_rf.hpp"


class UsbTxProgressTests(unittest.TestCase):
    def test_host_state_machine_and_channel_independence(self) -> None:
        source = r'''
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "usb_tx_progress.hpp"

struct FakeWriter {
  int available = 0;
  size_t writeLimit = static_cast<size_t>(-1);
  std::vector<uint8_t> sink;

  int availableForWrite() { return available; }
  size_t write(const uint8_t* data, size_t size) {
    const size_t count = std::min(size, writeLimit);
    sink.insert(sink.end(), data, data + count);
    return count;
  }
};

int main() {
  const uint8_t message[] = {1, 2, 3, 4, 5};

  FakeWriter blocked;
  uint16_t blockedOffset = 0;
  auto attempt = usb_tx::writeAvailable(blocked, message, sizeof(message), blockedOffset);
  if (attempt.result != usb_tx::WriteResult::BACKPRESSURE || blockedOffset != 0 || attempt.written != 0) return 1;

  usb_tx::ChannelCounters counters;
  bool impeded = false;
  usb_tx::observeWrite(counters, attempt, impeded);
  if (counters.backpressureEvents != 1 || !impeded) return 2;

  FakeWriter zero;
  zero.available = 5;
  zero.writeLimit = 0;
  uint16_t zeroOffset = 0;
  attempt = usb_tx::writeAvailable(zero, message, sizeof(message), zeroOffset);
  usb_tx::observeWrite(counters, attempt, impeded);
  if (attempt.result != usb_tx::WriteResult::ZERO_WRITE || zeroOffset != 0 || counters.zeroWrites != 1) return 3;

  FakeWriter suffix;
  suffix.available = 2;
  uint16_t suffixOffset = 0;
  bool suffixImpeded = false;
  usb_tx::ChannelCounters suffixCounters;
  attempt = usb_tx::writeAvailable(suffix, message, sizeof(message), suffixOffset);
  usb_tx::observeWrite(suffixCounters, attempt, suffixImpeded);
  if (attempt.result != usb_tx::WriteResult::PARTIAL || suffixOffset != 2 || suffixCounters.partialWrites != 1) return 4;
  suffix.available = 8;
  attempt = usb_tx::writeAvailable(suffix, message, sizeof(message), suffixOffset);
  usb_tx::observeWrite(suffixCounters, attempt, suffixImpeded);
  if (attempt.result != usb_tx::WriteResult::COMPLETE || suffixOffset != 5 || suffixCounters.recoveries != 1) return 5;
  if (suffix.sink != std::vector<uint8_t>(message, message + sizeof(message))) return 6;

  FakeWriter shortWrite;
  shortWrite.available = 5;
  shortWrite.writeLimit = 2;
  uint16_t shortOffset = 0;
  attempt = usb_tx::writeAvailable(shortWrite, message, sizeof(message), shortOffset);
  if (attempt.result != usb_tx::WriteResult::PARTIAL || shortOffset != 2 || attempt.written != 2) return 7;

  FakeWriter channel0;
  FakeWriter channel1;
  channel0.available = 0;
  channel1.available = 5;
  uint16_t channel0Offset = 0;
  uint16_t channel1Offset = 0;
  const auto channel0Attempt = usb_tx::writeAvailable(channel0, message, sizeof(message), channel0Offset);
  const auto channel1Attempt = usb_tx::writeAvailable(channel1, message, sizeof(message), channel1Offset);
  if (channel0Attempt.result != usb_tx::WriteResult::BACKPRESSURE || channel0Offset != 0) return 8;
  if (channel1Attempt.result != usb_tx::WriteResult::COMPLETE || channel1Offset != 5) return 9;

  channel0.available = 5;
  FakeWriter nextChannel1;
  nextChannel1.available = 0;
  uint16_t nextChannel1Offset = 0;
  if (usb_tx::writeAvailable(channel0, message, sizeof(message), channel0Offset).result != usb_tx::WriteResult::COMPLETE) return 10;
  if (usb_tx::writeAvailable(nextChannel1, message, sizeof(message), nextChannel1Offset).result != usb_tx::WriteResult::BACKPRESSURE) return 11;

  usb_tx::QueueAccounting accounting;
  if (!accounting.admit(usb_tx::CHANNEL_CCSDS, 0, 2)) return 12;
  if (!accounting.admit(usb_tx::CHANNEL_PAYLOAD, 0, 2)) return 13;
  if (!accounting.admit(usb_tx::CHANNEL_CCSDS, 1, 2)) return 14;
  if (!accounting.admit(usb_tx::CHANNEL_PAYLOAD, 1, 2)) return 15;
  if (accounting.admit(usb_tx::CHANNEL_PAYLOAD, 2, 2)) return 16;
  const auto* ccsds = accounting.forChannel(usb_tx::CHANNEL_CCSDS);
  const auto* payload = accounting.forChannel(usb_tx::CHANNEL_PAYLOAD);
  if (ccsds == nullptr || payload == nullptr) return 17;
  if (ccsds->queueHighWater != 2 || payload->queueHighWater != 2) return 18;
  if (ccsds->explicitDiscards != 0 || payload->explicitDiscards != 1) return 19;
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            cpp = root / "usb_tx_progress_test.cpp"
            executable = root / "usb_tx_progress_test"
            cpp.write_text(source)
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(HEADER.parent),
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_relay_retains_suffix_and_uses_independent_channel_queues(self) -> None:
        relay_cpp = RELAY_CPP.read_text()
        relay_hpp = RELAY_HPP.read_text()
        self.assertIn("uint16_t writeOffset;", relay_hpp)
        self.assertIn(
            "m_downlinkQueue[usb_tx::CHANNEL_COUNT][MAX_QUEUE_DEPTH]",
            relay_hpp,
        )
        self.assertIn("serviceDownlinkChannel(link_protocol::CHANNEL_CCSDS);", relay_cpp)
        self.assertIn("serviceDownlinkChannel(link_protocol::CHANNEL_PAYLOAD);", relay_cpp)
        self.assertIn("usb_tx::writeAvailable", relay_cpp)
        self.assertNotIn("m_payloadIo->flush()", relay_cpp)
        self.assertNotIn("m_payloadIo->write(entry.payload, entry.length)", relay_cpp)
        self.assertRegex(
            relay_cpp,
            re.compile(
                r"if \(attempt\.result == usb_tx::WriteResult::COMPLETE\) \{\s*"
                r"popDownlinkEntry\(channel\);\s*\}"
            ),
        )
        service_start = relay_cpp.index("void RelayUartRf::serviceDownlinkChannel")
        service = relay_cpp[service_start:]
        payload_route = service.index("if (channel == link_protocol::CHANNEL_PAYLOAD)")
        ccsds_route = service.index(
            "else if (channel == link_protocol::CHANNEL_CCSDS)", payload_route
        )
        self.assertLess(payload_route, ccsds_route)
        self.assertIn("output = m_payloadIo;", service[payload_route:ccsds_route])
        self.assertIn("output = &m_linkIo;", service[ccsds_route:])


if __name__ == "__main__":
    unittest.main()
