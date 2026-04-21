#include <Arduino.h>
#include "../../../../ArtemisTeensy_N2_Baremetal/firmware/libs/rf23bp/artemis_rf23bp.hpp"
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t LED_PIN = 13;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint16_t TX_COMPLETE_TIMEOUT_MS = 250;
constexpr size_t RADIO_MAX_LEN = RH_RF22_MAX_MESSAGE_LEN;
constexpr char SATELLITE_PONG[] = "pong from satellite";

constexpr artemis::rf23bp::RadioPins kRadioPins{};
constexpr artemis::rf23bp::RadioProfile kRadioProfile{};

RH_RF22 g_radio(kRadioPins.cs_pin, kRadioPins.irq_pin, hardware_spi1);
uint8_t g_rxBuffer[RADIO_MAX_LEN];
char g_txBuffer[RADIO_MAX_LEN];

void printPayload(const uint8_t* data, uint8_t len) {
  Serial.print('"');
  for (uint8_t i = 0; i < len; ++i) {
    const char ch = static_cast<char>(data[i]);
    if (ch >= 32 && ch <= 126) {
      Serial.print(ch);
    } else {
      Serial.print("\\x");
      if (data[i] < 16) {
        Serial.print('0');
      }
      Serial.print(data[i], HEX);
    }
  }
  Serial.print('"');
}

bool isPingPacket(const uint8_t* data, uint8_t len) {
  return len == 1 && (data[0] == 'g' || data[0] == 'G');
}

bool initRadio() {
  pinMode(LED_PIN, OUTPUT);
  return artemis::rf23bp::initRadio(g_radio, kRadioPins, kRadioProfile, &Serial);
}

bool sendPacket(const char* text) {
  const uint8_t len = static_cast<uint8_t>(strlen(text));
  return artemis::rf23bp::sendPacket(
      g_radio, kRadioPins, kRadioProfile,
      reinterpret_cast<const uint8_t*>(text), len, TX_COMPLETE_TIMEOUT_MS);
}

void handlePacket(const uint8_t* data, uint8_t len, int16_t rssiDbm) {
  Serial.print("[satellite] rx len=");
  Serial.print(len);
  Serial.print(" rssi=");
  Serial.print(rssiDbm);
  Serial.print(" payload=");
  printPayload(data, len);
  Serial.println();

  if (!isPingPacket(data, len)) {
    Serial.println("[satellite] ignoring non-ping packet");
    return;
  }

  snprintf(g_txBuffer, sizeof(g_txBuffer), "%s", SATELLITE_PONG);
  if (!sendPacket(g_txBuffer)) {
    Serial.println("[satellite] tx failed");
    return;
  }

  digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  Serial.print("[satellite] tx: ");
  Serial.println(g_txBuffer);
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 5000) {
    delay(10);
  }

  delay(250);

  if (!initRadio()) {
    Serial.println("[satellite] radio init failed");
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }

  Serial.println("[satellite] radio ready");
  Serial.println("[satellite] dumping every received packet and replying with PONG");
}

void loop() {
  uint8_t len = sizeof(g_rxBuffer) - 1;
  int16_t lastRssiDbm = 0;
  if (artemis::rf23bp::receivePacket(g_radio, kRadioPins, kRadioProfile, g_rxBuffer,
                                     &len, &lastRssiDbm)) {
    g_rxBuffer[len] = '\0';
    handlePacket(g_rxBuffer, len, lastRssiDbm);
  }
}
