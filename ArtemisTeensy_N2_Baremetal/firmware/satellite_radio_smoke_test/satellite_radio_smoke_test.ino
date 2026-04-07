#include <Arduino.h>
#include <RH_RF22.h>
#include <RHHardwareSPI1.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SAT_RADIO_DEBUG
// Release-like default: keep the Teensy quiet unless a developer opts in to
// serial tracing. This is useful when the board is acting as a simple radio
// endpoint and we want to avoid extra USB chatter during normal tests.
#define SAT_RADIO_DEBUG 0
#endif

namespace {

constexpr int RADIO_CS = 38;
constexpr int RADIO_INT = 40;
constexpr uint8_t RADIO_RX_ON_PIN = 30;
constexpr uint8_t RADIO_TX_ON_PIN = 31;
constexpr uint8_t LED_PIN = 13;

constexpr float RADIO_FREQ_MHZ = 433.0f;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint8_t RADIO_INIT_ATTEMPTS = 5;
constexpr uint32_t RADIO_INIT_BACKOFF_MS = 250;
constexpr size_t RADIO_MAX_LEN = RH_RF22_MAX_MESSAGE_LEN;

RH_RF22 g_radio(RADIO_CS, RADIO_INT, hardware_spi1);
uint8_t g_rxBuffer[RADIO_MAX_LEN];
uint8_t g_txBuffer[RADIO_MAX_LEN];

uint32_t g_rxCount = 0;
uint32_t g_txCount = 0;
uint32_t g_badFrameCount = 0;
int g_lastRxRssi = 0;
uint32_t g_lastUptimeMs = 0;

#if SAT_RADIO_DEBUG
void dbgPrintPayload(const uint8_t* data, uint8_t len) {
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

void printConfigBanner() {
  Serial.println("[sat-radio] config:");
  Serial.print("[sat-radio]   freq=");
  Serial.print(RADIO_FREQ_MHZ, 1);
  Serial.println(" MHz");
  Serial.println("[sat-radio]   modem=GFSK_Rb125Fd125");
  Serial.println("[sat-radio]   tx_power=RH_RF22_RF23BP_TXPOW_30DBM");
  Serial.println("[sat-radio]   spi1=miso39 mosi26 sck27");
  Serial.println("[sat-radio]   amp=rx_low tx_high / tx_high rx_low");
}
#endif

void setAmpReceive() {
  // Default the RF front-end to receive bias before touching the radio.
  // That keeps the module in a known state during startup and reduces the
  // chance of a stuck TX bias or half-awake PA confusing the first probe.
  digitalWrite(RADIO_RX_ON_PIN, LOW);
  digitalWrite(RADIO_TX_ON_PIN, HIGH);
}

void setAmpTransmit() {
  digitalWrite(RADIO_RX_ON_PIN, HIGH);
  digitalWrite(RADIO_TX_ON_PIN, LOW);
}

bool initRadioOnce() {
  pinMode(RADIO_RX_ON_PIN, OUTPUT);
  pinMode(RADIO_TX_ON_PIN, OUTPUT);
  pinMode(RADIO_CS, OUTPUT);
  setAmpReceive();
  digitalWrite(RADIO_CS, HIGH);

  // Re-run SPI setup on every attempt so a bad power-up or stale peripheral
  // state does not get reused. The failure we are defending against is
  // usually a radio that only responds after a real power drop.
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

bool initRadio() {
  // Retry because the observed failure mode is "radio survives Teensy reset
  // badly" rather than "software consistently broken". A short backoff gives
  // the module time to settle without requiring a hard power pull.
  for (uint8_t attempt = 1; attempt <= RADIO_INIT_ATTEMPTS; ++attempt) {
    if (initRadioOnce()) {
      return true;
    }

    setAmpReceive();
    digitalWrite(RADIO_CS, HIGH);
    SPI1.end();
    delay(RADIO_INIT_BACKOFF_MS * attempt);
  }

  return false;
}

void sanitizeFrame(const uint8_t* data, uint8_t len, char* out, size_t outSize) {
  if (outSize == 0) {
    return;
  }

  const size_t count = (len < (outSize - 1)) ? len : (outSize - 1);
  for (size_t i = 0; i < count; ++i) {
    const char ch = static_cast<char>(data[i]);
    out[i] = (ch >= 32 && ch <= 126) ? ch : '.';
  }
  out[count] = '\0';
}

bool parseFieldU32(const char* text, const char* key, uint32_t* value) {
  const char* found = strstr(text, key);
  if (!found) {
    return false;
  }

  found += strlen(key);
  char* end = nullptr;
  const unsigned long parsed = strtoul(found, &end, 10);
  if (end == found) {
    return false;
  }

  *value = static_cast<uint32_t>(parsed);
  return true;
}

uint8_t buildReply(uint32_t seq, uint8_t rxLen, int rxRssi) {
  const int written = snprintf(reinterpret_cast<char*>(g_txBuffer), RADIO_MAX_LEN,
                               "PONG seq=%lu len=%u rx_rssi=%d up=%lu rx=%lu tx=%lu bad=%lu",
                               static_cast<unsigned long>(seq),
                               static_cast<unsigned>(rxLen),
                               rxRssi,
                               static_cast<unsigned long>(millis()),
                               static_cast<unsigned long>(g_rxCount),
                               static_cast<unsigned long>(g_txCount),
                               static_cast<unsigned long>(g_badFrameCount));
  if (written < 0) {
    return 0;
  }

  return static_cast<uint8_t>((written < static_cast<int>(RADIO_MAX_LEN)) ? written : (RADIO_MAX_LEN - 1));
}

uint8_t buildBootAnnouncement() {
  const int written = snprintf(reinterpret_cast<char*>(g_txBuffer), RADIO_MAX_LEN,
                               "BOOT satellite up=%lu rx=%lu tx=%lu bad=%lu",
                               static_cast<unsigned long>(millis()),
                               static_cast<unsigned long>(g_rxCount),
                               static_cast<unsigned long>(g_txCount),
                               static_cast<unsigned long>(g_badFrameCount));
  if (written < 0) {
    return 0;
  }

  return static_cast<uint8_t>((written < static_cast<int>(RADIO_MAX_LEN)) ? written : (RADIO_MAX_LEN - 1));
}

void sendBootAnnouncement() {
  const uint8_t msgLen = buildBootAnnouncement();
  if (msgLen == 0) {
    ++g_badFrameCount;
    return;
  }

  setAmpTransmit();
  const bool sent = g_radio.send(g_txBuffer, msgLen);
  g_radio.waitPacketSent();
  setAmpReceive();
  g_radio.setModeRx();

  if (sent) {
    ++g_txCount;
#if SAT_RADIO_DEBUG
    Serial.print("[sat-radio] boot announcement=");
    dbgPrintPayload(g_txBuffer, msgLen);
    Serial.println();
#endif
  } else {
    ++g_badFrameCount;
#if SAT_RADIO_DEBUG
    Serial.println("[sat-radio] boot announcement failed");
#endif
  }
}

void sendReply(uint32_t seq, uint8_t rxLen, int rxRssi) {
  const uint8_t replyLen = buildReply(seq, rxLen, rxRssi);
  if (replyLen == 0) {
    ++g_badFrameCount;
    return;
  }

  setAmpTransmit();
  const bool sent = g_radio.send(g_txBuffer, replyLen);
  g_radio.waitPacketSent();
  setAmpReceive();
  g_radio.setModeRx();

  if (sent) {
    ++g_txCount;
#if SAT_RADIO_DEBUG
    Serial.print("[sat-radio] tx seq=");
    Serial.print(seq);
    Serial.print(" len=");
    Serial.print(replyLen);
    Serial.print(" payload=");
    dbgPrintPayload(g_txBuffer, replyLen);
    Serial.println();
#endif
  } else {
    ++g_badFrameCount;
#if SAT_RADIO_DEBUG
    Serial.print("[sat-radio] tx failed seq=");
    Serial.println(seq);
#endif
  }
}

void processFrame(const uint8_t* data, uint8_t len) {
  char frame[128];
  sanitizeFrame(data, len, frame, sizeof(frame));

  uint32_t seq = 0;
  uint32_t expectedLen = 0;
  bool ok = true;
  ok = ok && parseFieldU32(frame, "seq=", &seq);
  ok = ok && parseFieldU32(frame, "len=", &expectedLen);

  ++g_rxCount;
  g_lastRxRssi = g_radio.lastRssi();
  g_lastUptimeMs = millis();

#if SAT_RADIO_DEBUG
  Serial.print("[sat-radio] rx seq=");
  Serial.print(ok ? seq : 0);
  Serial.print(" len=");
  Serial.print(len);
  Serial.print(" rssi=");
  Serial.print(g_lastRxRssi);
  Serial.print(" dBm payload=");
  dbgPrintPayload(data, len);

  if (!ok) {
    ++g_badFrameCount;
    Serial.print(" [parse error]");
  } else if (expectedLen != len) {
    ++g_badFrameCount;
    Serial.print(" [len mismatch expected=");
    Serial.print(expectedLen);
    Serial.print("]");
  }

  Serial.println();
#else
  (void)ok;
  (void)expectedLen;
#endif

  sendReply(seq, len, g_lastRxRssi);
}

}  // namespace

void setup() {
#if SAT_RADIO_DEBUG
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 5000) {
    delay(10);
  }
#endif

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, false);

  delay(250);
#if SAT_RADIO_DEBUG
  Serial.println("[sat-radio] boot");
  Serial.println("[sat-radio] leaving RPi power pin untouched, entering RX mode");
#endif

  if (!initRadio()) {
#if SAT_RADIO_DEBUG
    Serial.println("[sat-radio] radio init failed");
#endif
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }

#if SAT_RADIO_DEBUG
  printConfigBanner();
  Serial.println("[sat-radio] radio ready");
  Serial.println("[sat-radio] waiting for pings");
#endif
  sendBootAnnouncement();
}

void loop() {
  uint8_t len = sizeof(g_rxBuffer);
  if (g_radio.available() && g_radio.recv(g_rxBuffer, &len)) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    processFrame(g_rxBuffer, len);
  }
}
