#ifndef ARTEMIS_TEENSY_RF_TX_RETRY_HPP
#define ARTEMIS_TEENSY_RF_TX_RETRY_HPP

#include <stdint.h>

namespace rf_tx_retry {

enum class AttemptResult : uint8_t {
  SENT = 0,
  START_FAILED = 1,
  TX_TIMEOUT = 2,
};

struct Outcome {
  bool sent = false;
  uint8_t attempts = 0;
  uint8_t timeouts = 0;
  uint8_t recoveries = 0;
  bool terminalFailure = false;
};

// The low-level radio driver clears both FIFOs and restores RX before it
// reports TX_TIMEOUT. Retry that recovered path once; never retry a start
// failure because no completed recovery is known to have occurred.
static constexpr uint8_t TX_TIMEOUT_RETRIES = 1;

template <typename Attempt>
Outcome sendWithBoundedTimeoutRetry(Attempt attempt,
                                    uint8_t timeoutRetries = TX_TIMEOUT_RETRIES) {
  Outcome outcome;
  uint8_t retriesRemaining = timeoutRetries;
  while (true) {
    outcome.attempts += 1;
    const AttemptResult result = attempt();
    if (result == AttemptResult::SENT) {
      outcome.sent = true;
      return outcome;
    }
    if (result == AttemptResult::START_FAILED) {
      outcome.terminalFailure = true;
      return outcome;
    }

    outcome.timeouts += 1;
    outcome.recoveries += 1;
    if (retriesRemaining == 0) {
      outcome.terminalFailure = true;
      return outcome;
    }
    retriesRemaining -= 1;
  }
}

}  // namespace rf_tx_retry

#endif
