#ifndef ARTEMIS_GDS_TX_LOAD_TEST_HPP
#define ARTEMIS_GDS_TX_LOAD_TEST_HPP

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "link_counters.hpp"
#include "link_protocol.hpp"
#include "relay_uart_rf.hpp"
#include "rf23_driver.hpp"

class GdsTxLoadTest {
 public:
  GdsTxLoadTest(Stream& control,
                RelayUartRf& relay,
                Rf23Driver& radio,
                LinkCounters& counters)
      : m_control(control), m_relay(relay), m_radio(radio), m_counters(counters) {}

  void begin() {
    m_control.println(F("[GDS_LOAD] dedicated TX load-test image"));
    m_control.println(F("[GDS_LOAD] no load starts automatically; type 'load help'"));
  }

  bool isolatesRf() const { return m_mode == Mode::RF; }

  void pollCommands() {
    while (m_control.available() > 0) {
      const int value = m_control.read();
      if (value < 0) {
        break;
      }
      const char c = static_cast<char>(value);
      if (c == '\r' || c == '\n') {
        if (m_commandLength > 0) {
          m_command[m_commandLength] = '\0';
          processCommand(m_command);
          m_commandLength = 0;
        }
      } else if ((c == '\b' || c == 127) && m_commandLength > 0) {
        --m_commandLength;
      } else if (m_commandLength + 1 < sizeof(m_command)) {
        m_command[m_commandLength++] = c;
      }
    }
  }

  void tick(uint32_t now) {
    if (m_mode == Mode::RF) {
      tickRf(now);
    } else if (m_mode == Mode::USB) {
      tickUsb(now);
    }

    if (m_mode != Mode::IDLE && now - m_lastStatusMs >= STATUS_PERIOD_MS) {
      printStatus();
      m_lastStatusMs = now;
    }
  }

 private:
  enum class Mode : uint8_t { IDLE = 0, RF = 1, USB = 2 };

  static constexpr uint32_t STATUS_PERIOD_MS = 1000;
  static constexpr uint32_t MAX_RF_PACKETS = 100000;
  static constexpr uint32_t MAX_USB_BYTES = 100000000;

  static bool parseU32(const char* text, uint32_t& value) {
    if (text == nullptr || *text == '\0') {
      return false;
    }
    char* end = nullptr;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
      return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
  }

  static char* nextToken(char*& cursor) {
    while (*cursor == ' ' || *cursor == '\t') {
      ++cursor;
    }
    if (*cursor == '\0') {
      return nullptr;
    }
    char* token = cursor;
    while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t') {
      ++cursor;
    }
    if (*cursor != '\0') {
      *cursor++ = '\0';
    }
    return token;
  }

  void printHelp() {
    m_control.println(F("[GDS_LOAD] commands:"));
    m_control.println(F("[GDS_LOAD]   load rf <packets> <interval_ms> <28|29|30> [packet_bytes]"));
    m_control.println(F("[GDS_LOAD]   load usb <bytes> <chunk_bytes>"));
    m_control.println(F("[GDS_LOAD]   load status"));
    m_control.println(F("[GDS_LOAD]   load stop"));
  }

  void processCommand(char* command) {
    char* cursor = command;
    char* load = nextToken(cursor);
    char* action = nextToken(cursor);
    if (load == nullptr || strcmp(load, "load") != 0 || action == nullptr) {
      m_control.println(F("[GDS_LOAD] invalid command; type 'load help'"));
      return;
    }

    if (strcmp(action, "help") == 0) {
      printHelp();
    } else if (strcmp(action, "status") == 0) {
      printStatus();
    } else if (strcmp(action, "stop") == 0) {
      stop("operator");
    } else if (strcmp(action, "rf") == 0) {
      startRf(cursor);
    } else if (strcmp(action, "usb") == 0) {
      startUsb(cursor);
    } else {
      m_control.println(F("[GDS_LOAD] unknown load mode; type 'load help'"));
    }
  }

  void startRf(char* cursor) {
    uint32_t packets = 0;
    uint32_t intervalMs = 0;
    uint32_t powerDbm = 0;
    uint32_t packetBytes = link_protocol::RF_PACKET_MAX_LEN;
    char* packetsText = nextToken(cursor);
    char* intervalText = nextToken(cursor);
    char* powerText = nextToken(cursor);
    char* packetBytesText = nextToken(cursor);
    if (!parseU32(packetsText, packets) || !parseU32(intervalText, intervalMs) ||
        !parseU32(powerText, powerDbm) || packets == 0 || packets > MAX_RF_PACKETS ||
        intervalMs < 10 || intervalMs > 60000 || powerDbm < 28 || powerDbm > 30 ||
        (packetBytesText != nullptr && !parseU32(packetBytesText, packetBytes)) ||
        packetBytes == 0 || packetBytes > link_protocol::RF_PACKET_MAX_LEN ||
        nextToken(cursor) != nullptr) {
      m_control.println(F("[GDS_LOAD] usage: load rf <1..100000> <10..60000 ms> <28|29|30> [1..49 bytes]"));
      return;
    }
    if (m_mode != Mode::IDLE) {
      m_control.println(F("[GDS_LOAD] stop the active test first"));
      return;
    }
    if (!m_radio.setTxPowerDbm(static_cast<uint8_t>(powerDbm))) {
      m_control.println(F("[GDS_LOAD] unsupported TX power"));
      return;
    }

    resetRunState();
    m_mode = Mode::RF;
    m_target = packets;
    m_intervalMs = intervalMs;
    m_rfPacketBytes = static_cast<uint8_t>(packetBytes);
    m_nextActionMs = millis();
    m_control.print(F("[GDS_LOAD] LOAD_BEGIN mode=rf packets="));
    m_control.print(m_target);
    m_control.print(F(" packet_bytes="));
    m_control.print(m_rfPacketBytes);
    m_control.print(F(" interval_ms="));
    m_control.print(m_intervalMs);
    m_control.print(F(" power_dbm="));
    m_control.println(m_radio.txPowerDbm());
  }

  void startUsb(char* cursor) {
    uint32_t bytes = 0;
    uint32_t chunkBytes = 0;
    char* bytesText = nextToken(cursor);
    char* chunkText = nextToken(cursor);
    if (!parseU32(bytesText, bytes) || !parseU32(chunkText, chunkBytes) || bytes == 0 ||
        bytes > MAX_USB_BYTES || chunkBytes == 0 ||
        chunkBytes > link_protocol::FRAME_MAX_PAYLOAD || nextToken(cursor) != nullptr) {
      m_control.println(F("[GDS_LOAD] usage: load usb <1..100000000 bytes> <1..220 chunk_bytes>"));
      return;
    }
    if (m_mode != Mode::IDLE) {
      m_control.println(F("[GDS_LOAD] stop the active test first"));
      return;
    }

    resetRunState();
    m_mode = Mode::USB;
    m_target = bytes;
    m_chunkBytes = static_cast<uint16_t>(chunkBytes);
    m_startUartTxBytes = m_counters.uartTxBytes;
    m_control.print(F("[GDS_LOAD] LOAD_BEGIN mode=usb bytes="));
    m_control.print(m_target);
    m_control.print(F(" chunk_bytes="));
    m_control.println(m_chunkBytes);
  }

  void resetRunState() {
    m_completed = 0;
    m_failures = 0;
    m_generated = 0;
    m_sequence = 0;
    m_startedMs = millis();
    m_lastStatusMs = m_startedMs;
    m_startRfTimeouts = m_counters.rfTxTimeouts;
    m_startRfRecoveries = m_counters.rfRecoveries;
    m_startRfTerminalFailures = m_counters.rfTxTerminalFailures;
    const usb_tx::ChannelCounters* usb0 =
        m_relay.usbTxCounters(link_protocol::CHANNEL_CCSDS);
    if (usb0 != nullptr) {
      m_startUsbBackpressure = usb0->backpressureEvents;
      m_startUsbZero = usb0->zeroWrites;
      m_startUsbPartial = usb0->partialWrites;
      m_startUsbRecoveries = usb0->recoveries;
      m_startUsbDiscards = usb0->explicitDiscards;
    }
  }

  void buildRfPacket() {
    memset(m_packet, 0, sizeof(m_packet));
    m_packet[0] = 0xEE;  // Intentionally not an Artemis segment magic.
    m_packet[1] = static_cast<uint8_t>(m_sequence & 0xFFU);
    m_packet[2] = static_cast<uint8_t>((m_sequence >> 8U) & 0xFFU);
    m_packet[3] = static_cast<uint8_t>((m_sequence >> 16U) & 0xFFU);
    m_packet[4] = static_cast<uint8_t>((m_sequence >> 24U) & 0xFFU);
    for (uint8_t i = 5; i < m_rfPacketBytes; ++i) {
      m_packet[i] = static_cast<uint8_t>((m_sequence + i) & 0xFFU);
    }
  }

  void tickRf(uint32_t now) {
    if (m_completed + m_failures >= m_target) {
      complete();
      return;
    }
    if (!m_radio.isReady() || static_cast<int32_t>(now - m_nextActionMs) < 0) {
      return;
    }

    ++m_sequence;
    buildRfPacket();
    const Rf23SendResult result = m_radio.send(m_packet, m_rfPacketBytes);
    if (result == Rf23SendResult::SENT) {
      ++m_completed;
      m_counters.rfTxPackets += 1;
    } else {
      ++m_failures;
      m_counters.rfTxDrops += 1;
      m_counters.rfTxTerminalFailures += 1;
      if (result == Rf23SendResult::TX_TIMEOUT) {
        m_counters.rfTxTimeouts += 1;
        if (m_radio.consumeTxTimeoutRecoveryRequest()) {
          m_counters.rfRecoveries += 1;
        }
      }
    }
    m_nextActionMs = millis() + m_intervalMs;
  }

  void tickUsb(uint32_t) {
    if (m_generated < m_target) {
      const uint32_t remaining = m_target - m_generated;
      const uint16_t length = remaining < m_chunkBytes ? static_cast<uint16_t>(remaining)
                                                        : m_chunkBytes;
      for (uint16_t i = 0; i < length; ++i) {
        m_usbPayload[i] = static_cast<uint8_t>((m_sequence + i) & 0xFFU);
      }
      if (m_relay.diagnosticEnqueueDownlink(
              link_protocol::CHANNEL_CCSDS, m_usbPayload, length)) {
        m_generated += length;
        ++m_sequence;
      } else {
        ++m_failures;
      }
    }

    const uint32_t written = m_counters.uartTxBytes - m_startUartTxBytes;
    m_completed = written;
    if (m_generated >= m_target &&
        m_relay.diagnosticDownlinkPending(link_protocol::CHANNEL_CCSDS) == 0 &&
        written >= m_target) {
      complete();
    }
  }

  void printStatus() {
    const usb_tx::ChannelCounters* usb0 =
        m_relay.usbTxCounters(link_protocol::CHANNEL_CCSDS);
    m_control.print(F("[GDS_LOAD] LOAD_STATUS mode="));
    m_control.print(m_mode == Mode::RF ? "rf" : (m_mode == Mode::USB ? "usb" : "idle"));
    m_control.print(F(" target="));
    m_control.print(m_target);
    m_control.print(F(" completed="));
    m_control.print(m_completed);
    m_control.print(F(" generated="));
    m_control.print(m_generated);
    m_control.print(F(" failures="));
    m_control.print(m_failures);
    m_control.print(F(" elapsed_ms="));
    m_control.print(millis() - m_startedMs);
    m_control.print(F(" rf_tx_timeouts="));
    m_control.print(m_counters.rfTxTimeouts - m_startRfTimeouts);
    m_control.print(F(" rf_recoveries="));
    m_control.print(m_counters.rfRecoveries - m_startRfRecoveries);
    m_control.print(F(" rf_terminal_failures="));
    m_control.print(m_counters.rfTxTerminalFailures - m_startRfTerminalFailures);
    m_control.print(F(" usb0_pending="));
    m_control.print(m_relay.diagnosticDownlinkPending(link_protocol::CHANNEL_CCSDS));
    if (usb0 != nullptr) {
      m_control.print(F(" usb0_backpressure="));
      m_control.print(usb0->backpressureEvents - m_startUsbBackpressure);
      m_control.print(F(" usb0_zero="));
      m_control.print(usb0->zeroWrites - m_startUsbZero);
      m_control.print(F(" usb0_partial="));
      m_control.print(usb0->partialWrites - m_startUsbPartial);
      m_control.print(F(" usb0_recoveries="));
      m_control.print(usb0->recoveries - m_startUsbRecoveries);
      m_control.print(F(" usb0_high_water="));
      m_control.print(usb0->queueHighWater);
      m_control.print(F(" usb0_discards="));
      m_control.print(usb0->explicitDiscards - m_startUsbDiscards);
    }
    m_control.println();
  }

  void complete() {
    printStatus();
    m_control.println(F("[GDS_LOAD] LOAD_COMPLETE"));
    m_mode = Mode::IDLE;
  }

  void stop(const char* reason) {
    if (m_mode == Mode::IDLE) {
      m_control.println(F("[GDS_LOAD] no active test"));
      return;
    }
    printStatus();
    m_control.print(F("[GDS_LOAD] LOAD_STOP reason="));
    m_control.println(reason);
    m_mode = Mode::IDLE;
  }

  Stream& m_control;
  RelayUartRf& m_relay;
  Rf23Driver& m_radio;
  LinkCounters& m_counters;
  Mode m_mode = Mode::IDLE;
  char m_command[128] = {0};
  size_t m_commandLength = 0;
  uint32_t m_target = 0;
  uint32_t m_completed = 0;
  uint32_t m_generated = 0;
  uint32_t m_failures = 0;
  uint32_t m_sequence = 0;
  uint32_t m_intervalMs = 0;
  uint32_t m_nextActionMs = 0;
  uint32_t m_startedMs = 0;
  uint32_t m_lastStatusMs = 0;
  uint32_t m_startUartTxBytes = 0;
  uint32_t m_startRfTimeouts = 0;
  uint32_t m_startRfRecoveries = 0;
  uint32_t m_startRfTerminalFailures = 0;
  uint32_t m_startUsbBackpressure = 0;
  uint32_t m_startUsbZero = 0;
  uint32_t m_startUsbPartial = 0;
  uint32_t m_startUsbRecoveries = 0;
  uint32_t m_startUsbDiscards = 0;
  uint16_t m_chunkBytes = 0;
  uint8_t m_rfPacketBytes = link_protocol::RF_PACKET_MAX_LEN;
  uint8_t m_packet[link_protocol::RF_PACKET_MAX_LEN] = {0};
  uint8_t m_usbPayload[link_protocol::FRAME_MAX_PAYLOAD] = {0};
};

#endif
