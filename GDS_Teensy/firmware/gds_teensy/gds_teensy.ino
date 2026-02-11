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

LinkCounters g_linkCounters;
Rf23Driver g_rfDriver(RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN);
RelayUartRf g_relay(Serial, g_rfDriver, g_linkCounters, RelayConfig{true, false, false, false, 8});

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
