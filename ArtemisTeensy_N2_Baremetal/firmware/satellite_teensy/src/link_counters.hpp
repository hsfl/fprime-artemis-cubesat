#ifndef ARTEMIS_TEENSY_LINK_COUNTERS_HPP
#define ARTEMIS_TEENSY_LINK_COUNTERS_HPP

#include <Arduino.h>

struct LinkCounters {
  uint32_t uartRxBytes = 0;
  uint32_t uartTxBytes = 0;
  uint32_t rfRxPackets = 0;
  uint32_t rfTxPackets = 0;
  uint32_t crcDrops = 0;
  uint32_t framingDrops = 0;
  uint32_t timeoutEvents = 0;

  void reset() {
    uartRxBytes = 0;
    uartTxBytes = 0;
    rfRxPackets = 0;
    rfTxPackets = 0;
    crcDrops = 0;
    framingDrops = 0;
    timeoutEvents = 0;
  }
};

#endif
