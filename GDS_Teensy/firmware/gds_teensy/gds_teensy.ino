#include <Arduino.h>

#include "src/link_counters.hpp"
#include "src/relay_uart_rf.hpp"
#include "src/rf23_driver.hpp"

// Teensy 4.1 + RF23BP pinout from EPSCOR demo baseline.
static constexpr int RADIO_CS = 38;
static constexpr int RADIO_INT = 40;
static constexpr uint8_t RADIO_RX_ON_PIN = 30;
static constexpr uint8_t RADIO_TX_ON_PIN = 31;

static constexpr uint32_t USB_UART_BAUD = 115200;
static constexpr uint32_t DEBUG_UART_BAUD = 115200;
static constexpr uint16_t RAW_UART_FLUSH_MS = 12;
static constexpr uint8_t UPLINK_QUEUE_DEPTH = 32;
static constexpr uint8_t DOWNLINK_QUEUE_DEPTH = 32;
static constexpr uint32_t DEBUG_STATUS_PERIOD_MS = 1000;

#if defined(USB_DUAL_SERIAL) || defined(USB_TRIPLE_SERIAL)
#define ARTEMIS_HAS_DEBUG_USB 1
#else
#define ARTEMIS_HAS_DEBUG_USB 0
#endif

LinkCounters g_linkCounters;
Rf23Driver g_rfDriver(RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN);
// Demo bridge mode:
// - read raw CCSDS bytes from laptop GDS on USB Serial
// - aggregate and segment over RF
// - reassemble RF return traffic and write raw back to GDS
RelayConfig g_relayConfig{true, false, false, false, RAW_UART_FLUSH_MS, UPLINK_QUEUE_DEPTH, DOWNLINK_QUEUE_DEPTH};
RelayUartRf g_relay(Serial, g_rfDriver, g_linkCounters, g_relayConfig);

void debugPrintCounters(const char* prefix) {
#if ARTEMIS_HAS_DEBUG_USB
  SerialUSB1.print(prefix);
  SerialUSB1.print(" uart_rx=");
  SerialUSB1.print(g_linkCounters.uartRxBytes);
  SerialUSB1.print(" uart_tx=");
  SerialUSB1.print(g_linkCounters.uartTxBytes);
  SerialUSB1.print(" rf_rx_pkt=");
  SerialUSB1.print(g_linkCounters.rfRxPackets);
  SerialUSB1.print(" rf_tx_pkt=");
  SerialUSB1.print(g_linkCounters.rfTxPackets);
  SerialUSB1.print(" rf_rx_msg=");
  SerialUSB1.print(g_linkCounters.rfRxMessages);
  SerialUSB1.print(" rf_tx_msg=");
  SerialUSB1.print(g_linkCounters.rfTxMessages);
  SerialUSB1.print(" rf_rx_seg=");
  SerialUSB1.print(g_linkCounters.rfRxSegments);
  SerialUSB1.print(" rf_tx_seg=");
  SerialUSB1.print(g_linkCounters.rfTxSegments);
  SerialUSB1.print(" rf_tx_drops=");
  SerialUSB1.print(g_linkCounters.rfTxDrops);
  SerialUSB1.print(" rf_msg_id_gaps=");
  SerialUSB1.print(g_linkCounters.rfMsgIdGaps);
  SerialUSB1.print(" rf_ack_rx=");
  SerialUSB1.print(g_linkCounters.rfAckRx);
  SerialUSB1.print(" rf_ack_tx=");
  SerialUSB1.print(g_linkCounters.rfAckTx);
  SerialUSB1.print(" rf_retries=");
  SerialUSB1.print(g_linkCounters.rfRetries);
  SerialUSB1.print(" rf_ack_timeouts=");
  SerialUSB1.print(g_linkCounters.rfAckTimeouts);
  SerialUSB1.print(" rf_reasm_drops=");
  SerialUSB1.print(g_linkCounters.rfReassemblyDrops);
  SerialUSB1.print(" up_q_drops=");
  SerialUSB1.print(g_linkCounters.uplinkQueueDrops);
  SerialUSB1.print(" down_q_drops=");
  SerialUSB1.println(g_linkCounters.downlinkQueueDrops);
#else
  (void)prefix;
#endif
}

void setup() {
  // USB serial to laptop GDS.
  Serial.begin(USB_UART_BAUD);
#if ARTEMIS_HAS_DEBUG_USB
  SerialUSB1.begin(DEBUG_UART_BAUD);
#endif

  // Keep USB clean: no banner prints on this stream.
  const bool radioOk = g_rfDriver.begin();
  g_relay.begin();

#if ARTEMIS_HAS_DEBUG_USB
  delay(200);
  SerialUSB1.println("[GDS_Teensy] debug port ready; data port is USB Serial");
  if (radioOk) {
    SerialUSB1.println("[GDS_Teensy] RF23 bridge ready (raw USB byte tunnel + RF segmentation)");
  } else {
    SerialUSB1.println("[GDS_Teensy] RF23 init failed; relay running without RF");
  }
  debugPrintCounters("[GDS_Teensy] counters");
#else
  (void)radioOk;
#endif
}

void loop() {
#if ARTEMIS_HAS_DEBUG_USB
  static uint32_t lastDebugStatusMs = 0;
#endif

  g_relay.poll();

#if ARTEMIS_HAS_DEBUG_USB
  const uint32_t now = millis();
  if ((now - lastDebugStatusMs) >= DEBUG_STATUS_PERIOD_MS) {
    debugPrintCounters("[GDS_Teensy] counters");
    lastDebugStatusMs = now;
  }
#endif
}
