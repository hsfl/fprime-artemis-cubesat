#include <Arduino.h>
#include <RH_RF22.h>
#include <RHHardwareSPI1.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr int RADIO_CS = 38;
constexpr int RADIO_INT = 40;
constexpr uint8_t RADIO_RX_ON_PIN = 30;
constexpr uint8_t RADIO_TX_ON_PIN = 31;
constexpr uint8_t LED_PIN = 13;

constexpr float RADIO_FREQ_MHZ = 433.0f;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t LOOP_INTERVAL_MS = 2000;
constexpr uint32_t RESPONSE_WAIT_MS = 1000;
constexpr uint32_t TX_COMPLETE_TIMEOUT_MS = 500;
constexpr size_t RADIO_MAX_LEN = RH_RF22_MAX_MESSAGE_LEN;
constexpr char SATELLITE_PONG[] = "pong from satellite";

RH_RF22 g_radio(RADIO_CS, RADIO_INT, hardware_spi1);
uint8_t g_rxBuffer[RADIO_MAX_LEN];
char g_txBuffer[RADIO_MAX_LEN];
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

void setAmpReceive() {
  digitalWrite(RADIO_RX_ON_PIN, LOW);
  digitalWrite(RADIO_TX_ON_PIN, HIGH);
  delayMicroseconds(300);
}

void setAmpTransmit() {
  digitalWrite(RADIO_RX_ON_PIN, HIGH);
  digitalWrite(RADIO_TX_ON_PIN, LOW);
  delayMicroseconds(300);
}

bool sendBytesToSatellite(const uint8_t* data, uint8_t len, unsigned long timeoutMs = TX_COMPLETE_TIMEOUT_MS) {
  digitalWrite(LED_PIN, HIGH);
  setAmpTransmit();
  const bool queued = g_radio.send(data, len);
  bool sent = false;
  if (queued) {
    sent = static_cast<RHGenericDriver&>(g_radio).waitPacketSent(timeoutMs);
  }
  digitalWrite(LED_PIN, LOW);
  setAmpReceive();
  g_radio.setModeRx();
  return queued && sent;
}

bool initRadio() {
  pinMode(RADIO_RX_ON_PIN, OUTPUT);
  pinMode(RADIO_TX_ON_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  //wtf dont set the cs pin?

  setAmpReceive();

  SPI1.end();
  delay(20);
  SPI1.setMISO(39);
  SPI1.setMOSI(26);
  SPI1.setSCK(27);
  SPI1.begin();
  delay(10);

  if (!g_radio.init()) {
    return false;
  }
  if (!g_radio.setFrequency(RADIO_FREQ_MHZ)) {
    return false;
  }

  g_radio.setModemConfig(RH_RF22::GFSK_Rb125Fd125);
  g_radio.setTxPower(RH_RF22_RF23BP_TXPOW_30DBM);
  g_radio.setModeRx();
  return true;
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
    if (g_radio.available() && g_radio.recv(g_rxBuffer, &len)) {
      g_rxBuffer[len] = '\0';
      Serial.print("[ground] rx len=");
      Serial.print(len);
      Serial.print(" rssi=");
      Serial.print(g_radio.lastRssi());
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
