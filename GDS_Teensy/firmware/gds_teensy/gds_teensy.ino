#include <Arduino.h>

#include "src/link_counters.hpp"
#include "src/relay_uart_rf.hpp"
#include "src/rf23_driver.hpp"
#include "src/wdt_guard.hpp"

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
static constexpr uint8_t TEENSY_LED_PIN = 13;
static constexpr uint32_t RADIO_TRAFFIC_LED_BLINK_MS = 60;

#if defined(USB_DUAL_SERIAL) || defined(USB_TRIPLE_SERIAL)
#define ARTEMIS_HAS_DEBUG_USB 1
#else
#define ARTEMIS_HAS_DEBUG_USB 0
#endif

#if defined(USB_TRIPLE_SERIAL)
#define ARTEMIS_HAS_PAYLOAD_USB 1
#else
#define ARTEMIS_HAS_PAYLOAD_USB 0
#endif

LinkCounters g_linkCounters;
Rf23Driver g_rfDriver(RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN);
// Channelized bridge mode:
// - Serial remains raw CCSDS for fprime-gds on channel 0
// - SerialUSB2 is a raw payload-blob packet stream on channel 1 when triple serial is enabled
RelayConfig g_relayConfig{
    true,
    false,
    false,
    false,
    RAW_UART_FLUSH_MS,
    UPLINK_QUEUE_DEPTH,
    DOWNLINK_QUEUE_DEPTH,
    link_protocol::RF_SEGMENT_MAX_DATA,
    link_protocol::CHANNEL_CCSDS};
#if ARTEMIS_HAS_PAYLOAD_USB
RelayUartRf g_relay(Serial, g_rfDriver, g_linkCounters, g_relayConfig, &SerialUSB2);
#else
RelayUartRf g_relay(Serial, g_rfDriver, g_linkCounters, g_relayConfig);
#endif
static uint32_t g_radioTrafficLedUntilMs = 0;

struct RadioTrafficSnapshot {
  uint32_t rxPackets = 0;
  uint32_t txPackets = 0;
  uint32_t ackRx = 0;
  uint32_t ackTx = 0;
  uint32_t retries = 0;
  uint32_t ackTimeouts = 0;
  uint32_t txDrops = 0;
};

RadioTrafficSnapshot radioTrafficSnapshot() {
  return {
      g_linkCounters.rfRxPackets,
      g_linkCounters.rfTxPackets,
      g_linkCounters.rfAckRx,
      g_linkCounters.rfAckTx,
      g_linkCounters.rfRetries,
      g_linkCounters.rfAckTimeouts,
      g_linkCounters.rfTxDrops,
  };
}

bool radioTrafficChanged(const RadioTrafficSnapshot& a, const RadioTrafficSnapshot& b) {
  return a.rxPackets != b.rxPackets || a.txPackets != b.txPackets || a.ackRx != b.ackRx ||
         a.ackTx != b.ackTx || a.retries != b.retries || a.ackTimeouts != b.ackTimeouts ||
         a.txDrops != b.txDrops;
}

void updateRadioTrafficLed(uint32_t now) {
  static RadioTrafficSnapshot lastTraffic = radioTrafficSnapshot();
  const RadioTrafficSnapshot currentTraffic = radioTrafficSnapshot();
  if (radioTrafficChanged(currentTraffic, lastTraffic)) {
    lastTraffic = currentTraffic;
    g_radioTrafficLedUntilMs = now + RADIO_TRAFFIC_LED_BLINK_MS;
    digitalWrite(TEENSY_LED_PIN, LOW);
  } else if (static_cast<int32_t>(now - g_radioTrafficLedUntilMs) >= 0) {
    digitalWrite(TEENSY_LED_PIN, HIGH);
  }
}

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
  SerialUSB1.print(" payload_uart_rx=");
  SerialUSB1.print(g_linkCounters.payloadUartRxBytes);
  SerialUSB1.print(" payload_uart_tx=");
  SerialUSB1.print(g_linkCounters.payloadUartTxBytes);
  SerialUSB1.print(" payload_rf_rx_msg=");
  SerialUSB1.print(g_linkCounters.payloadRfRxMessages);
  SerialUSB1.print(" payload_rf_tx_msg=");
  SerialUSB1.print(g_linkCounters.payloadRfTxMessages);
  SerialUSB1.print(" payload_rf_rx_seg=");
  SerialUSB1.print(g_linkCounters.payloadRfRxSegments);
  SerialUSB1.print(" payload_rf_tx_seg=");
  SerialUSB1.print(g_linkCounters.payloadRfTxSegments);
  SerialUSB1.print(" rf_tx_drops=");
  SerialUSB1.print(g_linkCounters.rfTxDrops);
  SerialUSB1.print(" crc_drops=");
  SerialUSB1.print(g_linkCounters.crcDrops);
  SerialUSB1.print(" framing_drops=");
  SerialUSB1.print(g_linkCounters.framingDrops);
  SerialUSB1.print(" uart_timeouts=");
  SerialUSB1.print(g_linkCounters.timeoutEvents);
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
  SerialUSB1.print(" rf_wrong_network=");
  SerialUSB1.print(g_linkCounters.rfWrongNetworkDrops);
  SerialUSB1.print(" rf_wrong_address=");
  SerialUSB1.print(g_linkCounters.rfWrongAddressDrops);
  SerialUSB1.print(" rf_wrong_version=");
  SerialUSB1.print(g_linkCounters.rfVersionDrops);
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
  const bool watchdogReset = wdt_guard::consumeWatchdogResetFlag();

  // USB serial to laptop GDS.
  Serial.begin(USB_UART_BAUD);
#if ARTEMIS_HAS_DEBUG_USB
  SerialUSB1.begin(DEBUG_UART_BAUD);
#endif
#if ARTEMIS_HAS_PAYLOAD_USB
  SerialUSB2.begin(USB_UART_BAUD);
#endif
  pinMode(TEENSY_LED_PIN, OUTPUT);
  digitalWrite(TEENSY_LED_PIN, HIGH);
  wdt_guard::begin();

  // Keep USB clean: no banner prints on this stream.
  const bool radioOk = g_rfDriver.begin();
  g_relay.begin();

#if ARTEMIS_HAS_DEBUG_USB
  delay(200);
  if (watchdogReset) {
    SerialUSB1.println("[GDS_Teensy] watchdog reset detected");
  }
  SerialUSB1.println("[GDS_Teensy] hardware watchdog armed (12s)");
  SerialUSB1.println("[GDS_Teensy] debug port ready; data port is USB Serial");
  if (radioOk) {
    SerialUSB1.println("[GDS_Teensy] RF23 bridge ready (raw GDS channel + payload channel + RF segmentation)");
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
  wdt_guard::feed();
  updateRadioTrafficLed(millis());

#if ARTEMIS_HAS_DEBUG_USB
  const uint32_t now = millis();
  if ((now - lastDebugStatusMs) >= DEBUG_STATUS_PERIOD_MS) {
    debugPrintCounters("[GDS_Teensy] counters");
    lastDebugStatusMs = now;
  }
#endif
}
