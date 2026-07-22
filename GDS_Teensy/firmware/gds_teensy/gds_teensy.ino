#include <Arduino.h>

#include "src/link_counters.hpp"
#include "src/relay_uart_rf.hpp"
#include "src/rf23_driver.hpp"
#if defined(GDS_TX_LOAD_TEST)
#include "src/gds_tx_load_test.hpp"
#endif

// Teensy 4.1 + RF23BP pinout from EPSCOR demo baseline.
static constexpr int RADIO_CS = 38;
static constexpr int RADIO_INT = 40;
static constexpr uint8_t RADIO_RX_ON_PIN = 30;
static constexpr uint8_t RADIO_TX_ON_PIN = 31;
static constexpr uint8_t RADIO_SDN_PIN = 37;

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
Rf23Driver g_rfDriver(
    RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN, RADIO_SDN_PIN);
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
#if defined(GDS_TX_LOAD_TEST) && ARTEMIS_HAS_DEBUG_USB
GdsTxLoadTest g_txLoadTest(SerialUSB1, g_relay, g_rfDriver, g_linkCounters);
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
  const usb_tx::ChannelCounters* usb0 =
      g_relay.usbTxCounters(link_protocol::CHANNEL_CCSDS);
  const usb_tx::ChannelCounters* usb1 =
      g_relay.usbTxCounters(link_protocol::CHANNEL_PAYLOAD);
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
  SerialUSB1.print(" rf_tx_timeouts=");
  SerialUSB1.print(g_linkCounters.rfTxTimeouts);
  SerialUSB1.print(" rf_recoveries=");
  SerialUSB1.print(g_linkCounters.rfRecoveries);
  SerialUSB1.print(" rf_tx_terminal_failures=");
  SerialUSB1.print(g_linkCounters.rfTxTerminalFailures);
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
  SerialUSB1.print(" rf_duplicate_drops=");
  SerialUSB1.print(g_linkCounters.rfDuplicateDrops);
  SerialUSB1.print(" rf_recovery_purged_uplinks=");
  SerialUSB1.print(g_linkCounters.rfRecoveryPurgedUplinks);
  SerialUSB1.print(" rf_recovery_discarded_bytes=");
  SerialUSB1.print(g_linkCounters.rfRecoveryDiscardedBytes);
  SerialUSB1.print(" rf_reasm_drops=");
  SerialUSB1.print(g_linkCounters.rfReassemblyDrops);
  SerialUSB1.print(" up_q_drops=");
  SerialUSB1.print(g_linkCounters.uplinkQueueDrops);
  SerialUSB1.print(" down_q_drops=");
  SerialUSB1.print(g_linkCounters.downlinkQueueDrops);
  SerialUSB1.print(" rf_state=");
  SerialUSB1.print(g_rfDriver.stateName());
  SerialUSB1.print(" rf_fault=");
  SerialUSB1.print(g_rfDriver.faultName());
  SerialUSB1.print(" rf_init_attempts=");
  SerialUSB1.print(g_rfDriver.initAttempts());
  SerialUSB1.print(" rf_init_failures=");
  SerialUSB1.print(g_rfDriver.initFailures());
  SerialUSB1.print(" rf_sdn_recoveries=");
  SerialUSB1.print(g_rfDriver.sdnRecoveries());
  SerialUSB1.print(" rf_recovery_pending=");
  SerialUSB1.print(g_rfDriver.recoveryPending() ? 1 : 0);
  SerialUSB1.print(" rf_recovery_backoff_ms=");
  SerialUSB1.print(g_rfDriver.recoveryBackoffMs());
  if (usb0 != nullptr && usb1 != nullptr) {
    SerialUSB1.print(" usb0_zero=");
    SerialUSB1.print(usb0->zeroWrites);
    SerialUSB1.print(" usb0_partial=");
    SerialUSB1.print(usb0->partialWrites);
    SerialUSB1.print(" usb0_backpressure=");
    SerialUSB1.print(usb0->backpressureEvents);
    SerialUSB1.print(" usb0_recoveries=");
    SerialUSB1.print(usb0->recoveries);
    SerialUSB1.print(" usb0_high_water=");
    SerialUSB1.print(usb0->queueHighWater);
    SerialUSB1.print(" usb0_discards=");
    SerialUSB1.print(usb0->explicitDiscards);
    SerialUSB1.print(" usb1_zero=");
    SerialUSB1.print(usb1->zeroWrites);
    SerialUSB1.print(" usb1_partial=");
    SerialUSB1.print(usb1->partialWrites);
    SerialUSB1.print(" usb1_backpressure=");
    SerialUSB1.print(usb1->backpressureEvents);
    SerialUSB1.print(" usb1_recoveries=");
    SerialUSB1.print(usb1->recoveries);
    SerialUSB1.print(" usb1_high_water=");
    SerialUSB1.print(usb1->queueHighWater);
    SerialUSB1.print(" usb1_discards=");
    SerialUSB1.print(usb1->explicitDiscards);
  }
  SerialUSB1.println();
#else
  (void)prefix;
#endif
}

void debugPrintRfFaultSnapshot() {
#if ARTEMIS_HAS_DEBUG_USB
  artemis::rf23bp::FaultSnapshot snapshot;
  if (!g_rfDriver.consumeFaultSnapshot(snapshot)) {
    return;
  }
  char line[512] = {0};
  snprintf(line,
           sizeof(line),
           "[GDS_Teensy] RF_FAULT cause=%u captured_ms=%lu nirq=%u rh_mode=%u reg00=%02X reg01=%02X reg02=%02X reg05=%02X reg06=%02X reg07=%02X reg08=%02X reg26=%02X irq03=%02X irq04=%02X",
           static_cast<unsigned int>(snapshot.cause),
           static_cast<unsigned long>(snapshot.captured_ms),
           static_cast<unsigned int>(snapshot.nirq_level),
           static_cast<unsigned int>(snapshot.radiohead_mode),
           static_cast<unsigned int>(snapshot.device_type),
           static_cast<unsigned int>(snapshot.version_code),
           static_cast<unsigned int>(snapshot.device_status),
           static_cast<unsigned int>(snapshot.interrupt_enable1),
           static_cast<unsigned int>(snapshot.interrupt_enable2),
           static_cast<unsigned int>(snapshot.operating_mode1),
           static_cast<unsigned int>(snapshot.operating_mode2),
           static_cast<unsigned int>(snapshot.raw_rssi),
           static_cast<unsigned int>(snapshot.interrupt_status1),
           static_cast<unsigned int>(snapshot.interrupt_status2));
  SerialUSB1.println(line);
#endif
}

void debugPrintRejectedRfPacket() {
#if defined(GDS_TX_LOAD_TEST) && ARTEMIS_HAS_DEBUG_USB
  Rf23RejectedPacketSnapshot snapshot;
  if (!g_rfDriver.consumeRejectedPacketSnapshot(snapshot)) {
    return;
  }
  SerialUSB1.printf(
      "[GDS_DIAG] RF_REJECT reason=%u captured_ms=%lu to=%02X from=%02X network=%02X version=%02X len=%u rssi_dbm=%d payload_prefix=",
      static_cast<unsigned int>(snapshot.reason),
      static_cast<unsigned long>(snapshot.capturedMs),
      static_cast<unsigned int>(snapshot.to),
      static_cast<unsigned int>(snapshot.from),
      static_cast<unsigned int>(snapshot.network),
      static_cast<unsigned int>(snapshot.version),
      static_cast<unsigned int>(snapshot.length),
      static_cast<int>(snapshot.rssiDbm));
  for (uint8_t i = 0; i < snapshot.payloadPrefixLength; ++i) {
    if (snapshot.payloadPrefix[i] < 0x10) {
      SerialUSB1.print('0');
    }
    SerialUSB1.print(snapshot.payloadPrefix[i], HEX);
  }
  SerialUSB1.println();
#endif
}

void debugPrintRfHealthSnapshot() {
#if defined(GDS_TX_LOAD_TEST) && ARTEMIS_HAS_DEBUG_USB
  const Rf23HealthSnapshot s = g_rfDriver.captureHealthSnapshot();
  SerialUSB1.printf(
      "[GDS_DIAG] RF_HEALTH identity_stable=%u nirq=%u rh_mode=%u pins_cs_rx_tx_sdn=%u,%u,%u,%u reg00=%02X reg01=%02X reg02=%02X reg05=%02X reg06=%02X reg07=%02X reg08=%02X reg30=%02X reg58=%02X reg6D=%02X reg75=%02X reg76=%02X reg77=%02X reg7D=%02X reg7E=%02X\n",
      s.identityStable ? 1U : 0U,
      static_cast<unsigned int>(s.nirqLevel),
      static_cast<unsigned int>(s.radioheadMode),
      static_cast<unsigned int>(s.csLevel),
      static_cast<unsigned int>(s.rxOnLevel),
      static_cast<unsigned int>(s.txOnLevel),
      static_cast<unsigned int>(s.sdnLevel),
      static_cast<unsigned int>(s.deviceType),
      static_cast<unsigned int>(s.versionCode),
      static_cast<unsigned int>(s.deviceStatus),
      static_cast<unsigned int>(s.interruptEnable1),
      static_cast<unsigned int>(s.interruptEnable2),
      static_cast<unsigned int>(s.operatingMode1),
      static_cast<unsigned int>(s.operatingMode2),
      static_cast<unsigned int>(s.dataAccessControl),
      static_cast<unsigned int>(s.chargePump),
      static_cast<unsigned int>(s.txPower),
      static_cast<unsigned int>(s.frequencyBand),
      static_cast<unsigned int>(s.frequency1),
      static_cast<unsigned int>(s.frequency0),
      static_cast<unsigned int>(s.txFifoThreshold),
      static_cast<unsigned int>(s.rxFifoThreshold));
#endif
}

void setup() {
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
  // Keep USB clean: no banner prints on this stream.
  g_rfDriver.beginSafeOff();
  const bool radioOk = g_rfDriver.begin();
  g_relay.begin();

#if ARTEMIS_HAS_DEBUG_USB
  delay(200);
  SerialUSB1.println("[GDS_Teensy] debug port ready; data port is USB Serial");
  if (radioOk) {
    SerialUSB1.println("[GDS_Teensy] RF23 bridge ready (raw GDS channel + payload channel + RF segmentation)");
  } else {
    SerialUSB1.println("[GDS_Teensy] RF23 init failed; relay running without RF");
  }
  debugPrintCounters("[GDS_Teensy] counters");
#if defined(GDS_TX_LOAD_TEST)
  g_txLoadTest.begin();
  debugPrintRfHealthSnapshot();
#endif
#else
  (void)radioOk;
#endif
}

void loop() {
#if ARTEMIS_HAS_DEBUG_USB
  static uint32_t lastDebugStatusMs = 0;
#endif

#if defined(GDS_TX_LOAD_TEST) && ARTEMIS_HAS_DEBUG_USB
  g_txLoadTest.pollCommands();
  if (!g_txLoadTest.isolatesRf()) {
    g_relay.poll();
  }
#else
  g_relay.poll();
#endif
  g_rfDriver.serviceRecovery();
  updateRadioTrafficLed(millis());

#if defined(GDS_TX_LOAD_TEST) && ARTEMIS_HAS_DEBUG_USB
  g_txLoadTest.tick(millis());
#endif

#if ARTEMIS_HAS_DEBUG_USB
  debugPrintRfFaultSnapshot();
  debugPrintRejectedRfPacket();
  const uint32_t now = millis();
  if ((now - lastDebugStatusMs) >= DEBUG_STATUS_PERIOD_MS) {
    debugPrintCounters("[GDS_Teensy] counters");
    lastDebugStatusMs = now;
  }
#endif
}
