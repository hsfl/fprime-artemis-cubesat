#include <Arduino.h>
#include "../../../../ArtemisTeensy_N2_Baremetal/firmware/libs/rf23bp/artemis_rf23bp.hpp"
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t LED_PIN = 13;

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t LOOP_INTERVAL_MS = 2000;
constexpr uint32_t RESPONSE_WAIT_MS = 1000;
constexpr uint16_t TX_COMPLETE_TIMEOUT_MS = 500;
constexpr size_t RADIO_MAX_LEN = RH_RF22_MAX_MESSAGE_LEN;
constexpr char SATELLITE_PONG[] = "pong from satellite";

constexpr artemis::rf23bp::RadioPins kRadioPins{};
constexpr artemis::rf23bp::RadioProfile kRadioProfile{};

RH_RF22 g_radio(kRadioPins.cs_pin, kRadioPins.irq_pin, hardware_spi1);
uint8_t g_rxBuffer[RADIO_MAX_LEN];
uint32_t g_sequence = 0;
unsigned long g_nextTxAt = 0;

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

bool sendBytesToSatellite(const uint8_t* data, uint8_t len, unsigned long timeoutMs = TX_COMPLETE_TIMEOUT_MS) {
  digitalWrite(LED_PIN, HIGH);
  const bool sent = artemis::rf23bp::sendPacket(g_radio, kRadioPins, kRadioProfile, data, len,
                                                 static_cast<uint16_t>(timeoutMs));
  digitalWrite(LED_PIN, LOW);
  return sent;
}

bool initRadio() {
  pinMode(LED_PIN, OUTPUT);
  return artemis::rf23bp::initRadio(g_radio, kRadioPins, kRadioProfile, &Serial);
}

bool isExpectedPong(const uint8_t* data, uint8_t len) {
  const uint8_t literalLen = static_cast<uint8_t>(strlen(SATELLITE_PONG));
  return len == literalLen && memcmp(data, SATELLITE_PONG, literalLen) == 0;
}

bool sendPing(uint32_t seq) {
  const uint8_t ping = 'g';
  Serial.print("\n[Ping ");
  Serial.print(seq);
  Serial.println("] Sending ping to satellite...");

  if (!sendBytesToSatellite(&ping, 1, 500)) {
    Serial.println("[ground] tx failed");
    return false;
  }

  const unsigned long waitStart = millis();
  bool gotReply = false;

  while (millis() - waitStart < RESPONSE_WAIT_MS) {
    uint8_t len = sizeof(g_rxBuffer) - 1;
    int16_t packetRssiDbm = 0;
    if (artemis::rf23bp::receivePacket(g_radio, kRadioPins, kRadioProfile, g_rxBuffer,
                                       &len, &packetRssiDbm)) {
      g_rxBuffer[len] = '\0';
      Serial.print("[ground] rx len=");
      Serial.print(len);
      Serial.print(" rssi=");
      Serial.print(packetRssiDbm);
      Serial.print(" payload=");
      printPayload(g_rxBuffer, len);
      Serial.println();

      if (isExpectedPong(g_rxBuffer, len)) {
        Serial.print("[ground] rx: ");
        Serial.println(reinterpret_cast<const char*>(g_rxBuffer));
        gotReply = true;
        break;
      }
    }
    delay(10);
  }

  Serial.print("Last RSSI: ");
  Serial.print(g_radio.lastRssi());
  Serial.println(" dBm");
  if (!gotReply) {
    Serial.println("(No new response detected during this interval; reporting most recent RSSI)");
  }

  return gotReply;
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 5000) {
    delay(10);
  }

  delay(250);

  if (!initRadio()) {
    Serial.println("[ground] radio init failed");
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }

  Serial.println("[ground] radio ready");
  Serial.println("[ground] starting ping test loop");
  g_nextTxAt = millis();
}

void loop() {
  const unsigned long now = millis();
  if (now < g_nextTxAt) {
    delay(10);
    return;
  }

  ++g_sequence;
  sendPing(g_sequence);
  g_nextTxAt = millis() + LOOP_INTERVAL_MS;
}
