#ifndef ARTEMIS_GDS_RF_RECOVERY_POLICY_HPP
#define ARTEMIS_GDS_RF_RECOVERY_POLICY_HPP

#include <stdint.h>

namespace rf_recovery {

static constexpr uint32_t FIRST_BACKOFF_MS = 1000;
static constexpr uint32_t SECOND_BACKOFF_MS = 5000;
static constexpr uint32_t MAX_BACKOFF_MS = 30000;

inline uint32_t backoffForFailure(uint8_t consecutive_failures) {
  if (consecutive_failures <= 1U) {
    return FIRST_BACKOFF_MS;
  }
  if (consecutive_failures == 2U) {
    return SECOND_BACKOFF_MS;
  }
  return MAX_BACKOFF_MS;
}

class Schedule {
 public:
  void requestImmediate(uint32_t now_ms) {
    m_pending = true;
    m_consecutiveFailures = 0;
    m_deadlineMs = now_ms;
  }

  bool pending() const { return m_pending; }

  bool due(uint32_t now_ms) const {
    return m_pending && static_cast<int32_t>(now_ms - m_deadlineMs) >= 0;
  }

  void failed(uint32_t now_ms) {
    if (m_consecutiveFailures < 0xFFU) {
      m_consecutiveFailures = static_cast<uint8_t>(m_consecutiveFailures + 1U);
    }
    m_deadlineMs = now_ms + backoffForFailure(m_consecutiveFailures);
    m_pending = true;
  }

  void succeeded() {
    m_pending = false;
    m_consecutiveFailures = 0;
    m_deadlineMs = 0;
  }

  uint8_t consecutiveFailures() const { return m_consecutiveFailures; }
  uint32_t deadlineMs() const { return m_deadlineMs; }
  uint32_t currentBackoffMs() const {
    return m_pending && m_consecutiveFailures > 0U
               ? backoffForFailure(m_consecutiveFailures)
               : 0U;
  }

 private:
  bool m_pending = false;
  uint8_t m_consecutiveFailures = 0;
  uint32_t m_deadlineMs = 0;
};

}  // namespace rf_recovery

#endif
