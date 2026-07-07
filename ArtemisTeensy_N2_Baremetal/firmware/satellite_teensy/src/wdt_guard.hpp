#ifndef ARTEMIS_TEENSY_WDT_GUARD_HPP
#define ARTEMIS_TEENSY_WDT_GUARD_HPP

#include <Arduino.h>

namespace wdt_guard {

static constexpr uint8_t TIMEOUT_SECONDS = 12;

inline bool consumeWatchdogResetFlag() {
#if defined(__IMXRT1062__)
  const bool wasWatchdogReset = (SRC_SRSR & SRC_SRSR_WDOG_RST_B) != 0;
  if (wasWatchdogReset) {
    SRC_SRSR = SRC_SRSR_WDOG_RST_B;
  }
  return wasWatchdogReset;
#else
#error "wdt_guard requires the Teensy 4.x IMXRT1062 watchdog registers"
#endif
}

inline void feed() {
#if defined(__IMXRT1062__)
  WDOG1_WSR = 0x5555;
  WDOG1_WSR = 0xAAAA;
#endif
}

inline void begin() {
#if defined(__IMXRT1062__)
  CCM_CCGR3 |= CCM_CCGR3_WDOG1(CCM_CCGR_ON);
  const uint16_t timeoutHalfSeconds = static_cast<uint16_t>(TIMEOUT_SECONDS * 2U);
  WDOG1_WCR = WDOG_WCR_WDE | WDOG_WCR_WT(timeoutHalfSeconds) | WDOG_WCR_SRS;
  feed();
#endif
}

}  // namespace wdt_guard

#endif
