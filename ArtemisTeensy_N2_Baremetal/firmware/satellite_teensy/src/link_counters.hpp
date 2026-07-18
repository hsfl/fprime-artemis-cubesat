#ifndef ARTEMIS_TEENSY_LINK_COUNTERS_HPP
#define ARTEMIS_TEENSY_LINK_COUNTERS_HPP

#include <Arduino.h>

struct LinkCounters {
  uint32_t uartRxBytes = 0;
  uint32_t uartTxBytes = 0;
  uint32_t rfRxPackets = 0;
  uint32_t rfTxPackets = 0;

  uint32_t rfRxMessages = 0;
  uint32_t rfTxMessages = 0;
  uint32_t rfRxSegments = 0;
  uint32_t rfTxSegments = 0;
  uint32_t payloadUartRxBytes = 0;
  uint32_t payloadUartTxBytes = 0;
  uint32_t payloadRfRxMessages = 0;
  uint32_t payloadRfTxMessages = 0;
  uint32_t payloadRfRxSegments = 0;
  uint32_t payloadRfTxSegments = 0;

  uint32_t crcDrops = 0;
  uint32_t framingDrops = 0;
  uint32_t timeoutEvents = 0;

  uint32_t rfReassemblyTimeouts = 0;
  uint32_t rfReassemblyDrops = 0;
  uint32_t rfOversizeDrops = 0;
  uint32_t rfTxDrops = 0;
  uint32_t rfTxTimeouts = 0;
  uint32_t rfRecoveries = 0;
  uint32_t rfTxTerminalFailures = 0;
  uint32_t rfMsgIdGaps = 0;
  uint32_t rfAckRx = 0;
  uint32_t rfAckTx = 0;
  uint32_t rfRetries = 0;
  uint32_t rfAckTimeouts = 0;
  uint32_t rfWrongNetworkDrops = 0;
  uint32_t rfWrongAddressDrops = 0;
  uint32_t rfVersionDrops = 0;
  uint32_t rfDuplicateDrops = 0;
  uint32_t rfRecoveryPurgedUplinks = 0;
  uint32_t rfRecoveryDiscardedBytes = 0;
  uint32_t uplinkQueueDrops = 0;
  uint32_t downlinkQueueDrops = 0;

  void reset() {
    uartRxBytes = 0;
    uartTxBytes = 0;
    rfRxPackets = 0;
    rfTxPackets = 0;

    rfRxMessages = 0;
    rfTxMessages = 0;
    rfRxSegments = 0;
    rfTxSegments = 0;
    payloadUartRxBytes = 0;
    payloadUartTxBytes = 0;
    payloadRfRxMessages = 0;
    payloadRfTxMessages = 0;
    payloadRfRxSegments = 0;
    payloadRfTxSegments = 0;

    crcDrops = 0;
    framingDrops = 0;
    timeoutEvents = 0;

    rfReassemblyTimeouts = 0;
    rfReassemblyDrops = 0;
    rfOversizeDrops = 0;
    rfTxDrops = 0;
    rfTxTimeouts = 0;
    rfRecoveries = 0;
    rfTxTerminalFailures = 0;
    rfMsgIdGaps = 0;
    rfAckRx = 0;
    rfAckTx = 0;
    rfRetries = 0;
    rfAckTimeouts = 0;
    rfWrongNetworkDrops = 0;
    rfWrongAddressDrops = 0;
    rfVersionDrops = 0;
    rfDuplicateDrops = 0;
    rfRecoveryPurgedUplinks = 0;
    rfRecoveryDiscardedBytes = 0;
    uplinkQueueDrops = 0;
    downlinkQueueDrops = 0;
  }
};

#endif
