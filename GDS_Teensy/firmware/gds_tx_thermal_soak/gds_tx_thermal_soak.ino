#include <Arduino.h>

// Standalone, intentionally TX-only thermal-soak firmware for the GDS OBC.
// It does not include the normal relay, GDS USB bridge, or receive path.
#include <artemis_rf23bp.hpp>

static constexpr int RADIO_CS = 38;
static constexpr int RADIO_INT = 40;
static constexpr uint8_t RADIO_RX_ON_PIN = 30;
static constexpr uint8_t RADIO_TX_ON_PIN = 31;
static constexpr uint8_t RADIO_SDN_PIN = 37;
static constexpr uint8_t STATUS_LED_PIN = 13;

static constexpr uint8_t RF_PACKET_BYTES = 49;
static constexpr uint8_t TX_TIMEOUTS_BEFORE_RECOVERY = 3;
static constexpr uint16_t TX_COMPLETE_TIMEOUT_MS = 500;
static constexpr uint32_t STATUS_PERIOD_MS = 1000;
static constexpr uint32_t TX_LED_TOGGLE_MS = 250;

// RadioHead's RF23BP PA data sheet defines 28/29/30 dBm at codes 5/6/7.
// Lower codes are retained for controlled threshold diagnostics but are not
// calibrated RF23BP output-power claims.  The build helper supplies this
// value as -DGDS_TX_SOAK_POWER_CODE=<0..7>.
#ifndef GDS_TX_SOAK_POWER_CODE
#define GDS_TX_SOAK_POWER_CODE RH_RF22_RF23BP_TXPOW_30DBM
#endif

#define GDS_TX_SOAK_STRINGIFY_INNER(value) #value
#define GDS_TX_SOAK_STRINGIFY(value) GDS_TX_SOAK_STRINGIFY_INNER(value)

static constexpr uint8_t TX_POWER_CODE = GDS_TX_SOAK_POWER_CODE;
static constexpr char TX_POWER_CODE_BUILD_MARKER[] =
    "GDS_TX_SOAK_POWER_CODE=" GDS_TX_SOAK_STRINGIFY(GDS_TX_SOAK_POWER_CODE);

artemis::rf23bp::RadioPins g_pins;
artemis::rf23bp::RadioProfile g_profile;
artemis::rf23bp::BoundedRf22 g_radio(RADIO_CS, RADIO_INT, hardware_spi1);

uint8_t g_packet[RF_PACKET_BYTES] = {0};
uint32_t g_sequence = 0;
uint32_t g_txSuccesses = 0;
uint32_t g_txFailures = 0;
uint32_t g_txTimeouts = 0;
uint32_t g_recoveries = 0;
uint8_t g_consecutiveTimeouts = 0;
bool g_radioReady = false;
uint32_t g_lastStatusMs = 0;
uint32_t g_lastLedToggleMs = 0;
bool g_ledOn = false;
bool g_ledStalled = true;

void setLedStalled() {
  g_ledStalled = true;
  g_ledOn = true;
  digitalWrite(STATUS_LED_PIN, HIGH);
}

void indicateTxSuccess() {
  const uint32_t now = millis();
  if (g_ledStalled) {
    g_ledStalled = false;
    g_ledOn = false;
    digitalWrite(STATUS_LED_PIN, LOW);
    g_lastLedToggleMs = now;
    return;
  }
  if (now - g_lastLedToggleMs >= TX_LED_TOGGLE_MS) {
    g_ledOn = !g_ledOn;
    digitalWrite(STATUS_LED_PIN, g_ledOn ? HIGH : LOW);
    g_lastLedToggleMs = now;
  }
}

void printStatus() {
  SerialUSB1.print(F("[GDS_TX_SOAK] tx_success="));
  SerialUSB1.print(g_txSuccesses);
  SerialUSB1.print(F(" tx_failures="));
  SerialUSB1.print(g_txFailures);
  SerialUSB1.print(F(" tx_timeouts="));
  SerialUSB1.print(g_txTimeouts);
  SerialUSB1.print(F(" recoveries="));
  SerialUSB1.print(g_recoveries);
  SerialUSB1.print(F(" tx_power_code="));
  SerialUSB1.print(TX_POWER_CODE);
  SerialUSB1.print(F(" rh_mode="));
  SerialUSB1.print(static_cast<unsigned int>(g_radio.mode()));
  SerialUSB1.print(F(" nirq="));
  SerialUSB1.print(digitalRead(RADIO_INT));
  SerialUSB1.print(F(" led="));
  SerialUSB1.println(g_ledStalled ? F("stalled") : F("tx"));
}

bool initializeRadio() {
  g_radioReady = artemis::rf23bp::initRadio(g_radio, g_pins, g_profile, &SerialUSB1);
  if (g_radioReady) {
    SerialUSB1.println(F("[GDS_TX_SOAK] RF ready: autonomous 49-byte 30 dBm TX-only loop"));
  } else {
    SerialUSB1.println(F("[GDS_TX_SOAK] RF init failed; retrying"));
    setLedStalled();
  }
  return g_radioReady;
}

void buildPacket() {
  ++g_sequence;
  g_packet[0] = 0xEE;
  g_packet[1] = static_cast<uint8_t>(g_sequence & 0xFFU);
  g_packet[2] = static_cast<uint8_t>((g_sequence >> 8U) & 0xFFU);
  g_packet[3] = static_cast<uint8_t>((g_sequence >> 16U) & 0xFFU);
  g_packet[4] = static_cast<uint8_t>((g_sequence >> 24U) & 0xFFU);
  for (uint8_t i = 5; i < RF_PACKET_BYTES; ++i) {
    g_packet[i] = static_cast<uint8_t>((g_sequence + i) & 0xFFU);
  }
}

void setup() {
  Serial.begin(115200);
  SerialUSB1.begin(115200);
  delay(200);
  pinMode(STATUS_LED_PIN, OUTPUT);
  setLedStalled();

  g_pins.cs_pin = RADIO_CS;
  g_pins.irq_pin = RADIO_INT;
  g_pins.rx_on_pin = RADIO_RX_ON_PIN;
  g_pins.tx_on_pin = RADIO_TX_ON_PIN;
  g_pins.sdn_pin = RADIO_SDN_PIN;
  g_profile.frequency_mhz = 433.0f;
  g_profile.modem = RH_RF22::GFSK_Rb125Fd125;
  g_profile.tx_power = TX_POWER_CODE;
  g_profile.start_in_receive = false;
  g_profile.settle_us = 300;

  SerialUSB1.println(F("[GDS_TX_SOAK] standalone autonomous TX-only thermal-soak image"));
  SerialUSB1.println(TX_POWER_CODE_BUILD_MARKER);
  SerialUSB1.print(F("[GDS_TX_SOAK] tx_power_code="));
  SerialUSB1.print(TX_POWER_CODE);
  SerialUSB1.println(F(", full 49-byte packets, zero intentional inter-packet delay"));
  initializeRadio();
}

void loop() {
  if (!g_radioReady) {
    delay(250);
    initializeRadio();
    return;
  }

  buildPacket();
  artemis::rf23bp::FaultSnapshot fault;
  const artemis::rf23bp::SendResult result = artemis::rf23bp::sendPacket(
      g_radio, g_pins, g_profile, g_packet, sizeof(g_packet), TX_COMPLETE_TIMEOUT_MS,
      &SerialUSB1, &fault);
  if (result == artemis::rf23bp::SendResult::SENT) {
    ++g_txSuccesses;
    g_consecutiveTimeouts = 0;
    indicateTxSuccess();
  } else {
    ++g_txFailures;
    setLedStalled();
    if (result == artemis::rf23bp::SendResult::TX_TIMEOUT) {
      ++g_txTimeouts;
      ++g_consecutiveTimeouts;
    }
    if (fault.valid) {
      SerialUSB1.printf(
          "[GDS_TX_SOAK] RF_FAULT cause=%u nirq=%u rh_mode=%u reg00=%02X irq03=%02X irq04=%02X\n",
          static_cast<unsigned int>(fault.cause),
          static_cast<unsigned int>(fault.nirq_level),
          static_cast<unsigned int>(fault.radiohead_mode),
          static_cast<unsigned int>(fault.device_type),
          static_cast<unsigned int>(fault.interrupt_status1),
          static_cast<unsigned int>(fault.interrupt_status2));
    }
    if (g_consecutiveTimeouts >= TX_TIMEOUTS_BEFORE_RECOVERY) {
      SerialUSB1.println(F("[GDS_TX_SOAK] SDN recovery after consecutive TX timeouts"));
      artemis::rf23bp::shutdownRadio(g_pins);
      g_radioReady = false;
      g_consecutiveTimeouts = 0;
      ++g_recoveries;
    }
  }

  const uint32_t now = millis();
  if (now - g_lastStatusMs >= STATUS_PERIOD_MS) {
    printStatus();
    g_lastStatusMs = now;
  }
}
