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
static constexpr uint16_t RAW_UART_FLUSH_MS = 12;
static constexpr uint8_t UPLINK_QUEUE_DEPTH = 32;
static constexpr uint8_t DOWNLINK_QUEUE_DEPTH = 32;

LinkCounters g_linkCounters;
Rf23Driver g_rfDriver(RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN);
// Demo bridge mode:
// - read raw CCSDS bytes from laptop GDS on USB Serial
// - aggregate and segment over RF
// - reassemble RF return traffic and write raw back to GDS
RelayConfig g_relayConfig{true, false, false, false, RAW_UART_FLUSH_MS, UPLINK_QUEUE_DEPTH, DOWNLINK_QUEUE_DEPTH};
RelayUartRf g_relay(Serial, g_rfDriver, g_linkCounters, g_relayConfig);

void setup() {
  // USB serial to laptop GDS.
  Serial.begin(USB_UART_BAUD);

  // Keep USB clean: no banner prints on this stream.
  g_rfDriver.begin();
  g_relay.begin();
}

void loop() {
  g_relay.poll();
}
