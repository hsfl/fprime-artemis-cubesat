#include <Arduino.h>
#include <RH_RF22.h>
#include <RHHardwareSPI1.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr int RADIO_CS = 38;
constexpr int RADIO_INT = 40;
constexpr uint8_t RADIO_RX_ON_PIN = 30;
constexpr uint8_t RADIO_TX_ON_PIN = 31;
constexpr uint8_t LED_PIN = 13;

constexpr float RADIO_FREQ_MHZ = 433.0f;
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t PING_INTERVAL_MS = 1500;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 1200;
constexpr uint32_t TX_COMPLETE_TIMEOUT_MS = 1000;
constexpr uint8_t RADIO_INIT_ATTEMPTS = 5;
constexpr uint32_t RADIO_INIT_BACKOFF_MS = 250;
constexpr uint32_t TEST_SUITE_TIMEOUT_MS = 60000;
constexpr size_t RADIO_MAX_LEN = RH_RF22_MAX_MESSAGE_LEN;
constexpr uint8_t PING_PHASE_PAYLOAD_LEN = 32;

constexpr uint8_t PAYLOAD_SIZES[] = {16, 24, 32, 48, 64, 96};
constexpr char PAYLOAD_PATTERN[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

enum class TestMode : uint8_t {
  Ping,
  Sweep,
  Soak,
};

enum class PendingTest : uint8_t {
  None,
  SelectedMode,
  SmokeSuite,
};

RH_RF22 g_radio(RADIO_CS, RADIO_INT, hardware_spi1);
uint8_t g_txBuffer[RADIO_MAX_LEN];
uint8_t g_rxBuffer[RADIO_MAX_LEN];

uint32_t g_pingCount = 0;
uint32_t g_replyCount = 0;
uint32_t g_timeoutCount = 0;
uint32_t g_parseErrorCount = 0;
uint32_t g_duplicateReplyCount = 0;
uint32_t g_missedSeqCount = 0;
uint32_t g_lastSeq = 0;
uint32_t g_lastRttMs = 0;
uint32_t g_lastSentAtMs = 0;
uint32_t g_totalRttMs = 0;
uint32_t g_minRttMs = 0xFFFFFFFFu;
uint32_t g_maxRttMs = 0;
int g_lastReplyRssi = 0;
uint8_t g_payloadIndex = 0;
TestMode g_mode = TestMode::Ping;
bool g_running = false;
bool g_abortRequested = false;
// Commands queue work here so the sketch stays idle until a human asks for a test.
PendingTest g_pendingTest = PendingTest::None;
uint32_t g_intervalMs = PING_INTERVAL_MS;
char g_commandBuffer[96];
size_t g_commandLen = 0;

void setAmpReceive() {
  // Keep the RF front-end in receive-safe state before probing the radio.
  // Some RF23BP setups come up in a partially powered or latched state after
  // a soft reset, and forcing the amp pins into a known receive bias helps
  // reduce init failures that would otherwise require a full power cycle.
  digitalWrite(RADIO_RX_ON_PIN, LOW);
  digitalWrite(RADIO_TX_ON_PIN, HIGH);
}

void setAmpTransmit() {
  digitalWrite(RADIO_RX_ON_PIN, HIGH);
  digitalWrite(RADIO_TX_ON_PIN, LOW);
}

void setLedActive(bool active) {
  digitalWrite(LED_PIN, active ? HIGH : LOW);
}

void blinkErrorLed(uint8_t count = 3, uint32_t onMs = 80, uint32_t offMs = 120) {
  // Blink only on error conditions so a steady LED means "test active" and a
  // blinking LED means "something needs attention".
  for (uint8_t i = 0; i < count; ++i) {
    digitalWrite(LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(LED_PIN, LOW);
    delay(offMs);
  }
}

bool suiteTimedOut(uint32_t suiteStartMs, const char* phaseName) {
  if (millis() - suiteStartMs <= TEST_SUITE_TIMEOUT_MS) {
    return false;
  }

  Serial.print("[gds-radio] ");
  Serial.print(phaseName);
  Serial.print(" failed: suite timeout after ");
  Serial.print(TEST_SUITE_TIMEOUT_MS);
  Serial.println(" ms");
  blinkErrorLed(4, 60, 120);
  return true;
}

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

void printConfigBanner() {
  Serial.println("[gds-radio] config:");
  Serial.print("[gds-radio]   freq=");
  Serial.print(RADIO_FREQ_MHZ, 1);
  Serial.println(" MHz");
  Serial.println("[gds-radio]   modem=GFSK_Rb125Fd125");
  Serial.println("[gds-radio]   tx_power=RH_RF22_RF23BP_TXPOW_30DBM");
  Serial.println("[gds-radio]   spi1=miso39 mosi26 sck27");
  Serial.println("[gds-radio]   amp=rx_low tx_high / tx_high rx_low");
}

const char* modeName(TestMode mode) {
  switch (mode) {
    case TestMode::Ping:
      return "ping";
    case TestMode::Sweep:
      return "sweep";
    case TestMode::Soak:
      return "soak";
  }
  return "unknown";
}

void printStatsLine(const char* prefix);
bool awaitReply(uint32_t expectedSeq, uint32_t sentAtMs);
void handleAsyncPacket(const uint8_t* data, uint8_t len);

void printHelp() {
  Serial.println("[gds-radio] commands:");
  Serial.println("[gds-radio]   help");
  Serial.println("[gds-radio]   stats");
  Serial.println("[gds-radio]   status");
  Serial.println("[gds-radio]   run");
  Serial.println("[gds-radio]   smoketest");
  Serial.println("[gds-radio]   stop");
  Serial.println("[gds-radio]   mode ping|sweep|soak");
  Serial.println("[gds-radio]   interval <ms>");
}

void printStatus() {
  Serial.print("[gds-radio] status running=");
  Serial.print(g_running ? "yes" : "no");
  Serial.print(" mode=");
  Serial.print(modeName(g_mode));
  Serial.print(" interval=");
  Serial.print(g_intervalMs);
  Serial.print("ms sent=");
  Serial.print(g_pingCount);
  Serial.print(" replies=");
  Serial.print(g_replyCount);
  Serial.print(" timeouts=");
  Serial.print(g_timeoutCount);
  Serial.print(" parse_err=");
  Serial.print(g_parseErrorCount);
  Serial.print(" dup=");
  Serial.print(g_duplicateReplyCount);
  Serial.print(" missed=");
  Serial.println(g_missedSeqCount);
}

void setMode(TestMode mode) {
  g_mode = mode;
  g_payloadIndex = 0;
  if (mode == TestMode::Soak) {
    g_intervalMs = 750;
  } else if (mode == TestMode::Ping) {
    g_intervalMs = 1500;
  } else {
    g_intervalMs = 1500;
  }
  Serial.print("[gds-radio] mode set to ");
  Serial.println(modeName(mode));
}

bool setIntervalFromText(const char* valueText) {
  char* end = nullptr;
  const unsigned long parsed = strtoul(valueText, &end, 10);
  if (end == valueText || parsed < 100 || parsed > 60000) {
    return false;
  }

  g_intervalMs = static_cast<uint32_t>(parsed);
  Serial.print("[gds-radio] interval set to ");
  Serial.print(g_intervalMs);
  Serial.println(" ms");
  return true;
}

void trimLeading(char*& text) {
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
}

void processCommand(char* cmd) {
  trimLeading(cmd);
  if (*cmd == '\0') {
    return;
  }

  for (char* p = cmd; *p != '\0'; ++p) {
    if (*p >= 'A' && *p <= 'Z') {
      *p = static_cast<char>(*p - 'A' + 'a');
    }
  }

  if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
    return;
  }

  if (strcmp(cmd, "stats") == 0) {
    printStatsLine("[gds-radio] stats");
    return;
  }

  if (strcmp(cmd, "status") == 0) {
    printStatus();
    return;
  }

  if (strcmp(cmd, "stop") == 0) {
    g_abortRequested = true;
    g_pendingTest = PendingTest::None;
    g_running = false;
    Serial.println("[gds-radio] stop requested");
    return;
  }

  if (strcmp(cmd, "run") == 0) {
    g_abortRequested = false;
    g_pendingTest = PendingTest::SelectedMode;
    Serial.println("[gds-radio] queued selected mode test");
    return;
  }

  if (strcmp(cmd, "smoketest") == 0) {
    g_abortRequested = false;
    g_pendingTest = PendingTest::SmokeSuite;
    Serial.println("[gds-radio] queued full smoke-test suite");
    return;
  }

  if (strncmp(cmd, "mode ", 5) == 0) {
    const char* modeText = cmd + 5;
    if (strcmp(modeText, "ping") == 0) {
      setMode(TestMode::Ping);
    } else if (strcmp(modeText, "sweep") == 0) {
      setMode(TestMode::Sweep);
    } else if (strcmp(modeText, "soak") == 0) {
      setMode(TestMode::Soak);
    } else {
      Serial.println("[gds-radio] unknown mode");
    }
    return;
  }

  if (strncmp(cmd, "interval ", 9) == 0) {
    if (!setIntervalFromText(cmd + 9)) {
      Serial.println("[gds-radio] interval must be 100..60000 ms");
    }
    return;
  }

  Serial.print("[gds-radio] unknown command: ");
  Serial.println(cmd);
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) {
      break;
    }
    if (c == '\r' || c == '\n') {
      if (g_commandLen > 0) {
        g_commandBuffer[g_commandLen] = '\0';
        processCommand(g_commandBuffer);
        g_commandLen = 0;
      }
      continue;
    }
    if (c == 8 || c == 127) {
      if (g_commandLen > 0) {
        --g_commandLen;
      }
      continue;
    }
    if (g_commandLen + 1 < sizeof(g_commandBuffer)) {
      g_commandBuffer[g_commandLen++] = static_cast<char>(c);
    }
  }
}

bool initRadioOnce() {
  pinMode(RADIO_RX_ON_PIN, OUTPUT);
  pinMode(RADIO_TX_ON_PIN, OUTPUT);
  pinMode(RADIO_CS, OUTPUT);
  setAmpReceive();
  digitalWrite(RADIO_CS, HIGH);

  // Recreate a cold-ish SPI bring-up on each attempt.
  // This is defensive because the failure mode we have seen is not a clean
  // code bug; it is usually a radio/rail startup issue where the module does
  // not answer the first SPI probe until it has been fully power-cycled or
  // given another chance to settle.
  SPI1.end();
  delay(20);
  SPI1.setMISO(39);
  SPI1.setMOSI(26);
  SPI1.setSCK(27);
  SPI1.begin();
  delay(10);

  if (!g_radio.init()) {
    Serial.println("[gds-radio] rf23.init() failed");
    blinkErrorLed();
    return false;
  }
  if (!g_radio.setFrequency(RADIO_FREQ_MHZ)) {
    blinkErrorLed();
    return false;
  }

  g_radio.setModemConfig(RH_RF22::GFSK_Rb125Fd125);
  g_radio.setTxPower(RH_RF22_RF23BP_TXPOW_30DBM);
  g_radio.setModeRx();
  return true;
}

bool initRadio() {
  // The radio has occasionally booted into a state that survives a Teensy
  // reset but disappears after full power removal. Retry with backoff so a
  // marginal startup has multiple chances to recover without physical power
  // cycling.
  for (uint8_t attempt = 1; attempt <= RADIO_INIT_ATTEMPTS; ++attempt) {
    Serial.print("[gds-radio] radio init attempt ");
    Serial.println(attempt);
    if (initRadioOnce()) {
      return true;
    }

    Serial.println("[gds-radio] radio init failed; retrying");
    setAmpReceive();
    digitalWrite(RADIO_CS, HIGH);
    SPI1.end();
    blinkErrorLed(1, 120, 120);
    delay(RADIO_INIT_BACKOFF_MS * attempt);
  }

  return false;
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

void buildPayload(uint32_t seq, uint8_t payloadLen) {
  if (payloadLen > RADIO_MAX_LEN) {
    payloadLen = RADIO_MAX_LEN;
  }

  // Keep the structured header comfortably below the packet length.
  // The smoke test parser relies on `seq=` and `len=` surviving intact, and a
  // 16-byte payload was truncating the header badly enough to make the logged
  // frame misleading during ping-mode debugging.
  int headerLen = snprintf(reinterpret_cast<char*>(g_txBuffer), payloadLen + 1,
                           "PING seq=%lu len=%u t=%lu ",
                           static_cast<unsigned long>(seq),
                           static_cast<unsigned>(payloadLen),
                           static_cast<unsigned long>(millis()));
  if (headerLen < 0) {
    headerLen = 0;
  }
  if (headerLen > static_cast<int>(payloadLen)) {
    headerLen = payloadLen;
  }

  const size_t patternLen = sizeof(PAYLOAD_PATTERN) - 1;
  for (uint8_t i = static_cast<uint8_t>(headerLen); i < payloadLen; ++i) {
    g_txBuffer[i] = PAYLOAD_PATTERN[(seq + i) % patternLen];
  }
}

bool waitWithAbort(uint32_t durationMs) {
  const uint32_t start = millis();
  while (millis() - start < durationMs) {
    pollSerialCommands();
    if (g_abortRequested) {
      return false;
    }
    delay(10);
  }
  return true;
}

bool sendPing(uint8_t payloadLen) {
  setLedActive(true);
  setAmpTransmit();
  const bool sent = g_radio.send(g_txBuffer, payloadLen);
  bool txComplete = false;
  if (sent) {
    // RadioHead's no-arg waitPacketSent() can block forever if the radio never
    // raises the TX-done condition. Bound the wait so a bad interrupt or wedged
    // radio cannot make the whole smoke test look frozen.
    txComplete = static_cast<RHGenericDriver&>(g_radio).waitPacketSent(TX_COMPLETE_TIMEOUT_MS);
  }
  setAmpReceive();
  g_radio.setModeRx();

  if (!sent) {
    return false;
  }

  if (!txComplete) {
    blinkErrorLed(2, 60, 120);
    Serial.print("[gds-radio] tx complete timeout after ");
    Serial.print(TX_COMPLETE_TIMEOUT_MS);
    Serial.println(" ms");
    return false;
  }

  return true;
}

void printStatsLine(const char* prefix) {
  Serial.print(prefix);
  Serial.print(" sent=");
  Serial.print(g_pingCount);
  Serial.print(" replies=");
  Serial.print(g_replyCount);
  Serial.print(" timeouts=");
  Serial.print(g_timeoutCount);
  Serial.print(" parse_err=");
  Serial.print(g_parseErrorCount);
  Serial.print(" dup=");
  Serial.print(g_duplicateReplyCount);
  Serial.print(" missed=");
  Serial.print(g_missedSeqCount);
  Serial.print(" last_rtt=");
  Serial.print(g_lastRttMs);
  Serial.print("ms last_rssi=");
  Serial.print(g_lastReplyRssi);
  Serial.println(" dBm");
}

bool runPacket(uint8_t payloadLen, const char* phaseName, uint32_t packetIndex) {
  ++g_pingCount;
  const uint32_t seq = g_pingCount;

  buildPayload(seq, payloadLen);
  Serial.print("[gds-radio] tx ");
  if (phaseName != nullptr) {
    Serial.print(phaseName);
    Serial.print(' ');
  }
  Serial.print("pkt=");
  Serial.print(packetIndex);
  Serial.print(' ');
  Serial.print("seq=");
  Serial.print(seq);
  Serial.print(" len=");
  Serial.print(payloadLen);
  Serial.print(" payload=");
  printPayload(g_txBuffer, payloadLen);
  Serial.println();

  if (!sendPing(payloadLen)) {
    blinkErrorLed(1, 60, 160);
    Serial.print("[gds-radio] ");
    Serial.print(phaseName != nullptr ? phaseName : "test");
    Serial.print(" failed: tx error on pkt=");
    Serial.print(packetIndex);
    Serial.print(" seq=");
    Serial.println(seq);
    return false;
  }

  g_lastSentAtMs = millis();
  if (!awaitReply(seq, g_lastSentAtMs)) {
    Serial.print("[gds-radio] ");
    Serial.print(phaseName != nullptr ? phaseName : "test");
    Serial.print(" failed: no reply for pkt=");
    Serial.print(packetIndex);
    Serial.print(" seq=");
    Serial.println(seq);
    return false;
  }

  return true;
}

bool runFixedPhase(const char* phaseName, uint32_t count, uint8_t payloadLen, uint32_t suiteStartMs) {
  Serial.print("[gds-radio] phase ");
  Serial.print(phaseName);
  Serial.print(" count=");
  Serial.print(count);
  Serial.print(" payload=");
  Serial.println(payloadLen);

  for (uint32_t i = 0; i < count; ++i) {
    if (g_abortRequested) {
      return false;
    }
    if (suiteTimedOut(suiteStartMs, phaseName)) {
      setLedActive(false);
      return false;
    }
    if (!runPacket(payloadLen, phaseName, i + 1)) {
      Serial.print("[gds-radio] ");
      Serial.print(phaseName);
      Serial.print(" failed on packet ");
      Serial.println(i + 1);
      setLedActive(false);
      return false;
    }
    if (i + 1 < count && !waitWithAbort(g_intervalMs)) {
      Serial.print("[gds-radio] ");
      Serial.print(phaseName);
      Serial.println(" aborted by user");
      setLedActive(false);
      return false;
    }
  }

  return true;
}

bool runSweepPhase(uint32_t suiteStartMs) {
  Serial.println("[gds-radio] phase sweep");
  for (size_t i = 0; i < sizeof(PAYLOAD_SIZES) / sizeof(PAYLOAD_SIZES[0]); ++i) {
    if (g_abortRequested) {
      return false;
    }
    if (suiteTimedOut(suiteStartMs, "sweep")) {
      setLedActive(false);
      return false;
    }
    if (!runPacket(PAYLOAD_SIZES[i], "sweep", static_cast<uint32_t>(i + 1))) {
      Serial.print("[gds-radio] sweep failed on packet ");
      Serial.println(i + 1);
      setLedActive(false);
      return false;
    }
    if (i + 1 < sizeof(PAYLOAD_SIZES) / sizeof(PAYLOAD_SIZES[0]) && !waitWithAbort(g_intervalMs)) {
      Serial.println("[gds-radio] sweep aborted by user");
      setLedActive(false);
      return false;
    }
  }

  return true;
}

void runSelectedModeTest() {
  const uint32_t suiteStartMs = millis();
  g_running = true;
  setLedActive(true);
  if (g_mode == TestMode::Ping) {
    if (!runFixedPhase("ping", 5, PING_PHASE_PAYLOAD_LEN, suiteStartMs)) {
      Serial.println("[gds-radio] selected mode failed");
    }
  } else if (g_mode == TestMode::Sweep) {
    if (!runSweepPhase(suiteStartMs)) {
      Serial.println("[gds-radio] selected mode failed");
    }
  } else {
    if (!runFixedPhase("soak", 10, 96, suiteStartMs)) {
      Serial.println("[gds-radio] selected mode failed");
    }
  }
  g_running = false;
  g_pendingTest = PendingTest::None;
  g_abortRequested = false;
  setLedActive(false);
  printStatsLine("[gds-radio] stats");
}

void runSmokeTestSuite() {
  const uint32_t suiteStartMs = millis();
  g_running = true;
  setLedActive(true);
  // Full suite = quick ping phase, payload sweep, then a larger soak phase.
  // This is the on-demand bench test path; nothing transmits until this runs.
  Serial.println("[gds-radio] smoke suite begin");
  if (!runFixedPhase("ping", 5, PING_PHASE_PAYLOAD_LEN, suiteStartMs)) {
    g_running = false;
    g_pendingTest = PendingTest::None;
    g_abortRequested = false;
    setLedActive(false);
    Serial.println("[gds-radio] smoke suite aborted");
    return;
  }
  if (!runSweepPhase(suiteStartMs)) {
    g_running = false;
    g_pendingTest = PendingTest::None;
    g_abortRequested = false;
    setLedActive(false);
    Serial.println("[gds-radio] smoke suite aborted");
    return;
  }
  if (!runFixedPhase("soak", 10, 96, suiteStartMs)) {
    g_running = false;
    g_pendingTest = PendingTest::None;
    g_abortRequested = false;
    setLedActive(false);
    Serial.println("[gds-radio] smoke suite aborted");
    return;
  }
  g_running = false;
  g_pendingTest = PendingTest::None;
  g_abortRequested = false;
  setLedActive(false);
  Serial.println("[gds-radio] smoke suite complete");
  printStatsLine("[gds-radio] stats");
}

void handleReply(const uint8_t* data, uint8_t len, uint32_t expectedSeq, uint32_t sentAtMs) {
  char frame[128];
  sanitizeFrame(data, len, frame, sizeof(frame));

  uint32_t replySeq = 0;
  uint32_t replyLen = 0;
  uint32_t satUptime = 0;
  uint32_t satRx = 0;
  uint32_t satTx = 0;
  uint32_t satBad = 0;
  bool ok = true;

  ok = ok && parseFieldU32(frame, "seq=", &replySeq);
  ok = ok && parseFieldU32(frame, "len=", &replyLen);
  ok = ok && parseFieldU32(frame, "up=", &satUptime);
  ok = ok && parseFieldU32(frame, "rx=", &satRx);
  ok = ok && parseFieldU32(frame, "tx=", &satTx);
  ok = ok && parseFieldU32(frame, "bad=", &satBad);

  const uint32_t rttMs = millis() - sentAtMs;
  g_lastRttMs = rttMs;
  g_totalRttMs += rttMs;
  if (rttMs < g_minRttMs) {
    g_minRttMs = rttMs;
  }
  if (rttMs > g_maxRttMs) {
    g_maxRttMs = rttMs;
  }
  g_lastReplyRssi = g_radio.lastRssi();
  ++g_replyCount;

  if (!ok) {
    ++g_parseErrorCount;
  }

  if (replySeq != expectedSeq) {
    ++g_parseErrorCount;
    blinkErrorLed(1, 60, 60);
    Serial.print("[gds-radio] sequence mismatch expected=");
    Serial.print(expectedSeq);
    Serial.print(" got=");
    Serial.println(replySeq);
  }
  if (replySeq == g_lastSeq) {
    ++g_duplicateReplyCount;
  } else if (replySeq > g_lastSeq + 1) {
    g_missedSeqCount += (replySeq - g_lastSeq - 1);
  }
  g_lastSeq = replySeq;

  Serial.print("[gds-radio] reply seq=");
  Serial.print(replySeq);
  Serial.print(" len=");
  Serial.print(replyLen);
  Serial.print(" rtt=");
  Serial.print(rttMs);
  Serial.print("ms rssi=");
  Serial.print(g_lastReplyRssi);
  Serial.print(" dBm sat_up=");
  Serial.print(satUptime);
  Serial.print(" sat_rx=");
  Serial.print(satRx);
  Serial.print(" sat_tx=");
  Serial.print(satTx);
  Serial.print(" sat_bad=");
  Serial.print(satBad);
  Serial.print(" raw=");
  printPayload(data, len);
  if (!ok) {
    Serial.print(" [parse error]");
  }
  Serial.println();

  (void)expectedSeq;
}

void handleAsyncPacket(const uint8_t* data, uint8_t len) {
  char frame[128];
  sanitizeFrame(data, len, frame, sizeof(frame));

  Serial.print("[gds-radio] async rssi=");
  Serial.print(g_radio.lastRssi());
  Serial.print(" dBm raw=");
  printPayload(data, len);
  Serial.println();
}

bool awaitReply(uint32_t expectedSeq, uint32_t sentAtMs) {
  const uint32_t start = millis();
  while (millis() - start < RESPONSE_TIMEOUT_MS) {
    pollSerialCommands();
    if (g_abortRequested) {
      return false;
    }
    uint8_t len = sizeof(g_rxBuffer);
    if (g_radio.available() && g_radio.recv(g_rxBuffer, &len)) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      if (len >= 4 && memcmp(g_rxBuffer, "PONG", 4) == 0) {
        handleReply(g_rxBuffer, len, expectedSeq, sentAtMs);
        return true;
      }

      handleAsyncPacket(g_rxBuffer, len);
    }
    delay(10);
  }

  ++g_timeoutCount;
  blinkErrorLed(1, 60, 160);
  Serial.print("[gds-radio] timeout seq=");
  Serial.print(expectedSeq);
  Serial.print(" after ");
  Serial.print(RESPONSE_TIMEOUT_MS);
  Serial.println(" ms");
  return false;
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial && millis() < 5000) {
    delay(10);
  }

  pinMode(LED_PIN, OUTPUT);
  setLedActive(false);

  delay(250);
  Serial.println("[gds-radio] boot");

  if (!initRadio()) {
    Serial.println("[gds-radio] radio init failed");
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }

  printConfigBanner();
  Serial.println("[gds-radio] radio ready");
  Serial.println("[gds-radio] idle; type 'help', 'run', or 'smoketest'");
  printHelp();
  printStatus();
}

void loop() {
  pollSerialCommands();

  if (g_pendingTest == PendingTest::SelectedMode) {
    runSelectedModeTest();
  } else if (g_pendingTest == PendingTest::SmokeSuite) {
    runSmokeTestSuite();
  } else {
    uint8_t len = sizeof(g_rxBuffer);
    if (g_radio.available() && g_radio.recv(g_rxBuffer, &len)) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      handleAsyncPacket(g_rxBuffer, len);
    }
  }
}
