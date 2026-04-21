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

LinkCounters g_linkCounters;
Rf23Driver g_rfDriver(RADIO_CS, RADIO_INT, RADIO_RX_ON_PIN, RADIO_TX_ON_PIN);
// Transparent bridge mode for HIL:
// - raw UART bytes from Pi are RF-relayed as payload
// - RF-reassembled bytes are emitted raw to Pi UART
RelayConfig g_relayConfig{true, false, false, false, 8};
RelayUartRf g_relay(Serial2, g_rfDriver, g_linkCounters, g_relayConfig);

void setup() {
  Serial.begin(115200);

  // Match EPSCOR payload baseline: assert Pi power-enable at boot.
  pinMode(RPI_ENABLE_PIN, OUTPUT);
  digitalWrite(RPI_ENABLE_PIN, HIGH);
  pinMode(TEENSY_LED_PIN, OUTPUT);
  digitalWrite(TEENSY_LED_PIN, HIGH);
  Serial.println("[ArtemisTeensy] RPI power enable asserted (pin 36 HIGH)");
  Serial.println("[ArtemisTeensy] LED asserted (pin 13 HIGH)");

  Serial2.begin(UART_BAUD);
  const bool radioOk = g_rfDriver.begin();
  g_relay.begin();

  if (radioOk) {
    Serial.println("[ArtemisTeensy] Relay bridge ready (raw UART byte tunnel + RF segmentation)");
  } else {
    Serial.println("[ArtemisTeensy] RF23 init failed; relay running without RF");
  }
}

void loop() {
  g_relay.poll();
}
