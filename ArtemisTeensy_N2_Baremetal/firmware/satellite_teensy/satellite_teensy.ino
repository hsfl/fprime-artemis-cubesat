#include <Arduino.h>

#include "src/link_counters.hpp"
#include "src/local_teensy_router.hpp"
#include "src/pdu_proxy.hpp"
#include "src/relay_uart_rf.hpp"
#include "src/rf23_driver.hpp"
#include "src/wdt_guard.hpp"

// Teensy 4.1 + RF23BP pinout from EPSCOR demo baseline.
static constexpr int RADIO_CS = 38;
static constexpr int RADIO_INT = 40;
static constexpr uint8_t RADIO_RX_ON_PIN = 30;
static constexpr uint8_t RADIO_TX_ON_PIN = 31;
static constexpr uint8_t RADIO_SDN_PIN = 37;
static constexpr uint8_t RPI_ENABLE_PIN = 36;
static constexpr uint8_t TEENSY_LED_PIN = 13;

static constexpr uint32_t UART_BAUD = link_protocol::UART_BAUD;
static constexpr uint32_t PDU_UART_BAUD = 9600;
static constexpr uint32_t DEBUG_UART_BAUD = 115200;
static constexpr uint16_t RAW_UART_FLUSH_MS = 12;
static constexpr uint8_t UPLINK_QUEUE_DEPTH = 32;
static constexpr uint8_t DOWNLINK_QUEUE_DEPTH = 32;
static constexpr uint32_t DEBUG_STATUS_PERIOD_MS = 1000;
static constexpr uint32_t RADIO_TRAFFIC_LED_BLINK_MS = 60;
static constexpr uint32_t RADIO_OFF_LED_HALF_PERIOD_MS = 500;
static constexpr size_t RPI_UART_RX_BUFFER_SIZE = 4096;
static constexpr uint16_t CCSDS_TM_FRAME_BYTES = 128;

LinkCounters g_linkCounters;
Rf23Driver g_rfDriver(RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN, RADIO_SDN_PIN);
PduProxy g_pduProxy(Serial1);
LocalTeensyRouter g_localRouter(g_pduProxy, g_rfDriver, g_linkCounters);
static uint8_t g_rpiUartRxBuffer[RPI_UART_RX_BUFFER_SIZE];
// Channelized bridge mode:
// - channel 0: CCSDS/GDS bytes forwarded over RF
// - channel 1: payload blob packets forwarded over RF
// - channel 2: Teensy-local subsystem RPC, terminated on this Teensy
RelayConfig g_relayConfig{
    true,
    true,
    false,
    true,
    RAW_UART_FLUSH_MS,
    UPLINK_QUEUE_DEPTH,
    DOWNLINK_QUEUE_DEPTH,
    CCSDS_TM_FRAME_BYTES,
    link_protocol::CHANNEL_CCSDS};
RelayUartRf g_relay(Serial2, g_rfDriver, g_linkCounters, g_relayConfig, nullptr, &g_localRouter);
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
  if (!g_rfDriver.isReady()) {
    digitalWrite(TEENSY_LED_PIN,
                 ((now / RADIO_OFF_LED_HALF_PERIOD_MS) & 1U) == 0U ? LOW : HIGH);
    return;
  }

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
  Serial.print(" payload_uart_rx=");
  Serial.print(g_linkCounters.payloadUartRxBytes);
  Serial.print(" payload_uart_tx=");
  Serial.print(g_linkCounters.payloadUartTxBytes);
  Serial.print(" payload_rf_rx_msg=");
  Serial.print(g_linkCounters.payloadRfRxMessages);
  Serial.print(" payload_rf_tx_msg=");
  Serial.print(g_linkCounters.payloadRfTxMessages);
  Serial.print(" payload_rf_rx_seg=");
  Serial.print(g_linkCounters.payloadRfRxSegments);
  Serial.print(" payload_rf_tx_seg=");
  Serial.print(g_linkCounters.payloadRfTxSegments);
  Serial.print(" rf_tx_drops=");
  Serial.print(g_linkCounters.rfTxDrops);
  Serial.print(" rf_tx_timeouts=");
  Serial.print(g_linkCounters.rfTxTimeouts);
  Serial.print(" rf_recoveries=");
  Serial.print(g_linkCounters.rfRecoveries);
  Serial.print(" rf_tx_terminal_failures=");
  Serial.print(g_linkCounters.rfTxTerminalFailures);
  Serial.print(" radio_state=");
  Serial.print(g_rfDriver.state());
  Serial.print(" radio_fault=");
  Serial.print(g_rfDriver.fault());
  Serial.print(" radio_init_attempts=");
  Serial.print(g_rfDriver.initAttempts());
  Serial.print(" crc_drops=");
  Serial.print(g_linkCounters.crcDrops);
  Serial.print(" framing_drops=");
  Serial.print(g_linkCounters.framingDrops);
  Serial.print(" uart_timeouts=");
  Serial.print(g_linkCounters.timeoutEvents);
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
  Serial.print(" rf_wrong_network=");
  Serial.print(g_linkCounters.rfWrongNetworkDrops);
  Serial.print(" rf_wrong_address=");
  Serial.print(g_linkCounters.rfWrongAddressDrops);
  Serial.print(" rf_wrong_version=");
  Serial.print(g_linkCounters.rfVersionDrops);
  Serial.print(" rf_reasm_drops=");
  Serial.print(g_linkCounters.rfReassemblyDrops);
  Serial.print(" up_q_drops=");
  Serial.print(g_linkCounters.uplinkQueueDrops);
  Serial.print(" down_q_drops=");
  Serial.println(g_linkCounters.downlinkQueueDrops);
}

void setup() {
  const bool watchdogReset = wdt_guard::consumeWatchdogResetFlag();

  g_rfDriver.beginSafeOff(watchdogReset);
  pinMode(RPI_ENABLE_PIN, OUTPUT);
  digitalWrite(RPI_ENABLE_PIN, HIGH);
  pinMode(TEENSY_LED_PIN, OUTPUT);
  digitalWrite(TEENSY_LED_PIN, HIGH);
  Serial.begin(DEBUG_UART_BAUD);
  if (watchdogReset) {
    Serial.println("[ArtemisTeensy] watchdog reset detected");
  }
  wdt_guard::begin();
  Serial.println("[ArtemisTeensy] hardware watchdog armed (12s)");
  Serial.println("[ArtemisTeensy] LED asserted (pin 13 HIGH)");
  Serial.println("[ArtemisTeensy] RFM23BP held OFF with SDN HIGH (pin 37)");
  Serial.println("[ArtemisTeensy] RPI power enable asserted (pin 36 HIGH)");

  Serial2.addMemoryForRead(g_rpiUartRxBuffer, sizeof(g_rpiUartRxBuffer));
  Serial2.begin(UART_BAUD);
  g_pduProxy.begin(PDU_UART_BAUD);
  g_relay.begin();

  Serial.println("[ArtemisTeensy] Relay/channel 2 ready; F Prime owns radio enable policy");
  debugPrintCounters("[ArtemisTeensy] counters");
}

void loop() {
  static uint32_t lastDebugStatusMs = 0;

  g_relay.poll();
  wdt_guard::feed();

  const uint32_t now = millis();
  updateRadioTrafficLed(now);

  if ((now - lastDebugStatusMs) >= DEBUG_STATUS_PERIOD_MS) {
    debugPrintCounters("[ArtemisTeensy] counters");
    lastDebugStatusMs = now;
  }
}
