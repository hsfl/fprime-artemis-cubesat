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
constexpr uint32_t TX_COMPLETE_TIMEOUT_MS = 250;
constexpr size_t RADIO_MAX_LEN = RH_RF22_MAX_MESSAGE_LEN;
constexpr char SATELLITE_PONG[] = "pong from satellite";

RH_RF22 g_radio(RADIO_CS, RADIO_INT, hardware_spi1);
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

bool isPingPacket(const uint8_t* data, uint8_t len) {
  return len == 1 && (data[0] == 'g' || data[0] == 'G');
}

bool initRadio() {
  pinMode(RADIO_RX_ON_PIN, OUTPUT);
  pinMode(RADIO_TX_ON_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  //NEVER set the cs pin only for init for radiohead library but like it breaks the amplifier why?
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

bool sendPacket(const char* text) {
  const uint8_t len = static_cast<uint8_t>(strlen(text));
  setAmpTransmit();
  const bool sent = g_radio.send(reinterpret_cast<const uint8_t*>(text), len);
  const bool txDone = sent && static_cast<RHGenericDriver&>(g_radio).waitPacketSent(TX_COMPLETE_TIMEOUT_MS);
  setAmpReceive();
  g_radio.setModeRx();
  return sent && txDone;
}

void handlePacket(const uint8_t* data, uint8_t len) {
  Serial.print("[satellite] rx len=");
  Serial.print(len);
  Serial.print(" rssi=");
  Serial.print(g_radio.lastRssi());
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
  if (g_radio.available() && g_radio.recv(g_rxBuffer, &len)) {
    g_rxBuffer[len] = '\0';
    handlePacket(g_rxBuffer, len);
  }
}
