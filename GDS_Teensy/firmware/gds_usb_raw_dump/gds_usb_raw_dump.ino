#include <Arduino.h>
#include "ccsds_replay_sample.hpp"

// Debug helper sketch for laptop-side GDS traffic inspection.
//
// Intended use:
// - USB CDC #0 (Serial): input from fprime-gds raw bytes
// - USB CDC #1 (SerialUSB1): human-readable hex dump for Arduino IDE monitor
// - UART1 (Serial1): raw pass-through link (optional downstream wiring)
//
// IMPORTANT:
// Build with Tools > USB Type = "Dual Serial" (or Triple Serial), otherwise
// debug text and input bytes will share the same USB stream.

namespace {

constexpr uint32_t USB_BAUD = 115200;
constexpr uint32_t UART1_BAUD = 115200;
constexpr bool ENABLE_UART1_PASSTHROUGH = true;
constexpr bool ENABLE_LOCAL_ECHO_TO_GDS = false;
constexpr bool ENABLE_REPLAY_SAMPLE_TO_GDS = true;
constexpr uint32_t REPLAY_GUARD_MS = 120;

constexpr size_t HEX_LINE_BYTES = 32;
constexpr uint32_t FLUSH_IDLE_MS = 20;

#if defined(USB_DUAL_SERIAL) || defined(USB_TRIPLE_SERIAL)
#define HAS_SECOND_USB_SERIAL 1
#else
#define HAS_SECOND_USB_SERIAL 0
#endif

Print& debugPort() {
#if HAS_SECOND_USB_SERIAL
  return SerialUSB1;
#else
  return Serial;
#endif
}

void printHexByte(Print& out, uint8_t value) {
  if (value < 0x10) {
    out.print('0');
  }
  out.print(value, HEX);
}

void flushHexLine(const uint8_t* buffer, size_t lineLen, uint32_t lineStartIndex, const char* prefix) {
  if (lineLen == 0) {
    return;
  }

  Print& out = debugPort();
  out.print(prefix);
  out.print('[');
  out.print(lineStartIndex);
  out.print("..");
  out.print(lineStartIndex + lineLen - 1);
  out.print("] ");

  for (size_t i = 0; i < lineLen; ++i) {
    printHexByte(out, buffer[i]);
    if (i + 1 < lineLen) {
      out.print(' ');
    }
  }
  out.println();
}

void appendByte(uint8_t* buffer, size_t& lineLen, uint8_t value, uint32_t totalCount, const char* prefix) {
  buffer[lineLen++] = value;
  if (lineLen >= HEX_LINE_BYTES) {
    const uint32_t start = totalCount - lineLen + 1;
    flushHexLine(buffer, lineLen, start, prefix);
    lineLen = 0;
  }
}

}  // namespace

void setup() {
  Serial.begin(USB_BAUD);
#if HAS_SECOND_USB_SERIAL
  SerialUSB1.begin(USB_BAUD);
#endif

  if (ENABLE_UART1_PASSTHROUGH) {
    Serial1.begin(UART1_BAUD);
  }

  delay(200);
  Print& out = debugPort();
  out.println("[gds-usb-raw-dump] ready");
#if HAS_SECOND_USB_SERIAL
  out.println("[gds-usb-raw-dump] input=Serial output=SerialUSB1 passthrough=Serial1");
  out.println("[gds-usb-raw-dump] U0=GDS->Teensy  D1=Serial1->GDS");
  out.println("[gds-usb-raw-dump] replay=on (captured CCSDS sample)");
#else
  out.println("[gds-usb-raw-dump] WARNING: build with USB Dual Serial for separate debug output");
#endif
}

void loop() {
  bool anyActivity = false;
  static uint8_t u0Line[HEX_LINE_BYTES];
  static size_t u0Len = 0;
  static uint32_t u0Total = 0;
  static uint32_t u0LastMs = 0;

  static uint8_t d1Line[HEX_LINE_BYTES];
  static size_t d1Len = 0;
  static uint32_t d1Total = 0;
  static uint32_t d1LastMs = 0;
  static uint32_t lastReplayMs = 0;

  bool sawUplink = false;
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      break;
    }

    const uint8_t value = static_cast<uint8_t>(raw);
    if (ENABLE_UART1_PASSTHROUGH) {
      Serial1.write(value);
    }
    if (ENABLE_LOCAL_ECHO_TO_GDS) {
      Serial.write(value);
    }

    ++u0Total;
    appendByte(u0Line, u0Len, value, u0Total, "U0");
    u0LastMs = millis();
    anyActivity = true;
    sawUplink = true;
  }

  if (ENABLE_REPLAY_SAMPLE_TO_GDS && sawUplink) {
    const uint32_t nowMs = millis();
    if ((nowMs - lastReplayMs) >= REPLAY_GUARD_MS) {
      for (size_t i = 0; i < kReplaySampleSize; ++i) {
        const uint8_t value = kReplaySampleBytes[i];
        Serial.write(value);
        ++d1Total;
        appendByte(d1Line, d1Len, value, d1Total, "RS");
      }
      d1LastMs = nowMs;
      lastReplayMs = nowMs;
      anyActivity = true;
    }
  }

  while (ENABLE_UART1_PASSTHROUGH && Serial1.available() > 0) {
    const int raw = Serial1.read();
    if (raw < 0) {
      break;
    }

    const uint8_t value = static_cast<uint8_t>(raw);
    Serial.write(value);

    ++d1Total;
    appendByte(d1Line, d1Len, value, d1Total, "D1");
    d1LastMs = millis();
    anyActivity = true;
  }

  if (!anyActivity && u0Len > 0) {
    const uint32_t nowMs = millis();
    if ((nowMs - u0LastMs) >= FLUSH_IDLE_MS) {
      const uint32_t start = u0Total - u0Len;
      flushHexLine(u0Line, u0Len, start, "U0");
      u0Len = 0;
    }
  }

  if (!anyActivity && d1Len > 0) {
    const uint32_t nowMs = millis();
    if ((nowMs - d1LastMs) >= FLUSH_IDLE_MS) {
      const uint32_t start = d1Total - d1Len;
      flushHexLine(d1Line, d1Len, start, "D1");
      d1Len = 0;
    }
  }
}
