import pathlib
import subprocess
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[3]
SAT_HEADER = (
    REPO_ROOT
    / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/rf_tx_retry.hpp"
)
GROUND_HEADER = REPO_ROOT / "GDS_Teensy/firmware/gds_teensy/src/rf_tx_retry.hpp"
SAT_RELAY = (
    REPO_ROOT
    / "ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.cpp"
)
GROUND_RELAY = REPO_ROOT / "GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.cpp"


class RfTxRetryTests(unittest.TestCase):
    def test_timeout_retry_policy(self) -> None:
        source = r'''
#include <cstddef>
#include <initializer_list>
#include <vector>

#include "rf_tx_retry.hpp"

using rf_tx_retry::AttemptResult;

struct FakeAttempts {
  std::vector<AttemptResult> results;
  std::size_t cursor = 0;

  AttemptResult operator()() {
    if (cursor >= results.size()) return AttemptResult::START_FAILED;
    return results[cursor++];
  }
};

int main() {
  {
    FakeAttempts attempts{{AttemptResult::SENT}};
    const auto outcome = rf_tx_retry::sendWithBoundedTimeoutRetry(attempts);
    if (!outcome.sent || outcome.attempts != 1 || outcome.timeouts != 0 ||
        outcome.recoveries != 0 || outcome.terminalFailure) return 1;
  }
  {
    FakeAttempts attempts{{AttemptResult::TX_TIMEOUT, AttemptResult::SENT}};
    const auto outcome = rf_tx_retry::sendWithBoundedTimeoutRetry(attempts);
    if (!outcome.sent || outcome.attempts != 2 || outcome.timeouts != 1 ||
        outcome.recoveries != 1 || outcome.terminalFailure) return 2;
  }
  {
    FakeAttempts attempts{{AttemptResult::TX_TIMEOUT, AttemptResult::TX_TIMEOUT}};
    const auto outcome = rf_tx_retry::sendWithBoundedTimeoutRetry(attempts);
    if (outcome.sent || outcome.attempts != 2 || outcome.timeouts != 2 ||
        outcome.recoveries != 2 || !outcome.terminalFailure) return 3;
  }
  {
    FakeAttempts attempts{{AttemptResult::START_FAILED, AttemptResult::SENT}};
    const auto outcome = rf_tx_retry::sendWithBoundedTimeoutRetry(attempts);
    if (outcome.sent || outcome.attempts != 1 || outcome.timeouts != 0 ||
        outcome.recoveries != 0 || !outcome.terminalFailure) return 4;
  }
  {
    FakeAttempts attempts{{AttemptResult::TX_TIMEOUT, AttemptResult::START_FAILED}};
    const auto outcome = rf_tx_retry::sendWithBoundedTimeoutRetry(attempts);
    if (outcome.sent || outcome.attempts != 2 || outcome.timeouts != 1 ||
        outcome.recoveries != 1 || !outcome.terminalFailure) return 5;
  }
  {
    FakeAttempts attempts{{AttemptResult::TX_TIMEOUT, AttemptResult::SENT}};
    const auto outcome = rf_tx_retry::sendWithBoundedTimeoutRetry(attempts, 0);
    if (outcome.sent || outcome.attempts != 1 || outcome.timeouts != 1 ||
        outcome.recoveries != 1 || !outcome.terminalFailure) return 6;
  }
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            cpp = root / "rf_tx_retry_test.cpp"
            executable = root / "rf_tx_retry_test"
            cpp.write_text(source)
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(GROUND_HEADER.parent),
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(executable)], check=True)

    def test_both_bridges_use_the_shared_policy(self) -> None:
        self.assertEqual(SAT_HEADER.read_bytes(), GROUND_HEADER.read_bytes())
        for relay_path in (SAT_RELAY, GROUND_RELAY):
            relay = relay_path.read_text()
            self.assertIn('#include "rf_tx_retry.hpp"', relay)
            self.assertIn("sendWithBoundedTimeoutRetry", relay)
            self.assertIn("m_counters.rfTxTimeouts += outcome.timeouts;", relay)
            self.assertIn("m_counters.rfRecoveries += outcome.recoveries;", relay)
            self.assertIn("if (outcome.terminalFailure)", relay)


if __name__ == "__main__":
    unittest.main()
