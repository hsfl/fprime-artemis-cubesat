#include <Arduino.h>

#include "src/link_counters.hpp"
#include "src/relay_uart_rf.hpp"
#include "src/rf23_driver.hpp"

// Teensy 4.1 + RF23BP pinout from EPSCOR demo baseline.
static constexpr int RADIO_CS = 38;
static constexpr int RADIO_INT = 40;
static constexpr uint8_t RADIO_RX_ON_PIN = 30;
static constexpr uint8_t RADIO_TX_ON_PIN = 31;
static constexpr uint8_t RPI_ENABLE_PIN = 36;
static constexpr uint8_t TEENSY_LED_PIN = 13;

static constexpr uint32_t UART_BAUD = 115200;
static constexpr uint32_t DEBUG_UART_BAUD = 115200;
static constexpr uint16_t RAW_UART_FLUSH_MS = 12;
static constexpr uint8_t UPLINK_QUEUE_DEPTH = 32;
static constexpr uint8_t DOWNLINK_QUEUE_DEPTH = 32;
static constexpr uint32_t DEBUG_STATUS_PERIOD_MS = 1000;
static constexpr size_t RPI_UART_RX_BUFFER_SIZE = 4096;
static constexpr uint16_t CCSDS_TM_FRAME_BYTES = 128;

LinkCounters g_linkCounters;
Rf23Driver g_rfDriver(RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN);
static uint8_t g_rpiUartRxBuffer[RPI_UART_RX_BUFFER_SIZE];
// Transparent bridge mode for HIL:
// - raw UART bytes from Pi are RF-relayed as payload
// - RF-reassembled bytes are emitted raw to Pi UART
RelayConfig g_relayConfig{
    true, false, false, false, RAW_UART_FLUSH_MS, UPLINK_QUEUE_DEPTH, DOWNLINK_QUEUE_DEPTH, CCSDS_TM_FRAME_BYTES};
RelayUartRf g_relay(Serial2, g_rfDriver, g_linkCounters, g_relayConfig);

void debugPrintCounters(const char* prefix) {
  Serial.print(prefix);
  Serial.print(" uart_rx=");
  Serial.print(g_linkCounters.uartRxBytes);
  Serial.print(" uart_tx=");
  Serial.print(g_linkCounters.uartTxBytes);
  Serial.print(" rf_rx_pkt=");
  Serial.print(g_linkCounters.rfRxPackets);
  Serial.print(" rf_tx_pkt=");
  Serial.print(g_linkCounters.rfTxPackets);
  Serial.print(" rf_rx_msg=");
  Serial.print(g_linkCounters.rfRxMessages);
  Serial.print(" rf_tx_msg=");
  Serial.print(g_linkCounters.rfTxMessages);
  Serial.print(" rf_rx_seg=");
  Serial.print(g_linkCounters.rfRxSegments);
  Serial.print(" rf_tx_seg=");
  Serial.print(g_linkCounters.rfTxSegments);
  Serial.print(" rf_tx_drops=");
  Serial.print(g_linkCounters.rfTxDrops);
  Serial.print(" rf_msg_id_gaps=");
  Serial.print(g_linkCounters.rfMsgIdGaps);
  Serial.print(" rf_ack_rx=");
  Serial.print(g_linkCounters.rfAckRx);
  Serial.print(" rf_ack_tx=");
  Serial.print(g_linkCounters.rfAckTx);
  Serial.print(" rf_retries=");
  Serial.print(g_linkCounters.rfRetries);
  Serial.print(" rf_ack_timeouts=");
  Serial.print(g_linkCounters.rfAckTimeouts);
  Serial.print(" rf_reasm_drops=");
  Serial.print(g_linkCounters.rfReassemblyDrops);
  Serial.print(" up_q_drops=");
  Serial.print(g_linkCounters.uplinkQueueDrops);
  Serial.print(" down_q_drops=");
  Serial.println(g_linkCounters.downlinkQueueDrops);
}

void setup() {
  Serial.begin(DEBUG_UART_BAUD);

  // LED on early as a "firmware alive" indicator.
  pinMode(TEENSY_LED_PIN, OUTPUT);
  digitalWrite(TEENSY_LED_PIN, HIGH);
  Serial.println("[ArtemisTeensy] LED asserted (pin 13 HIGH)");

  Serial2.addMemoryForRead(g_rpiUartRxBuffer, sizeof(g_rpiUartRxBuffer));
  Serial.println("[ArtemisTeensy] Serial2 RX buffer size: " +
                 String(sizeof(g_rpiUartRxBuffer)) + " bytes");
  Serial2.begin(UART_BAUD);
  Serial.println("[ArtemisTeensy] Serial2 UART started at " + String(UART_BAUD) + " baud");

  // Bring up the radio on a stable rail BEFORE enabling Pi power. The Pi's
  // power-on inrush can brown out a shared supply and stall the RF23BP
  // chip-ready handshake during init (this previously hung boot here). The
  // init is now timeout-guarded, so a missing/unready radio degrades to a
  // UART-only bridge instead of blocking.
  const bool radioOk = g_rfDriver.begin();
  Serial.println("[ArtemisTeensy] RF23 begin() returned " + String(radioOk ? "OK" : "FAIL"));
  g_relay.begin();
  Serial.println("[ArtemisTeensy] Relay bridge initialized (raw UART byte tunnel + RF segmentation)");

  if (radioOk) {
    Serial.println("[ArtemisTeensy] Relay bridge ready (raw UART byte tunnel + RF segmentation)");
  } else {
    Serial.println("[ArtemisTeensy] RF23 init failed/timed out; relay running without RF");
  }

  // Radio is up (or cleanly skipped); now power the Pi.
  pinMode(RPI_ENABLE_PIN, OUTPUT);
  digitalWrite(RPI_ENABLE_PIN, HIGH);
  Serial.println("[ArtemisTeensy] RPI power enable asserted (pin 36 HIGH)");

  debugPrintCounters("[ArtemisTeensy] counters");
}

void loop() {
  static uint32_t lastDebugStatusMs = 0;

  g_relay.poll();

  const uint32_t now = millis();
  if ((now - lastDebugStatusMs) >= DEBUG_STATUS_PERIOD_MS) {
    debugPrintCounters("[ArtemisTeensy] counters");
    lastDebugStatusMs = now;
  }
}
