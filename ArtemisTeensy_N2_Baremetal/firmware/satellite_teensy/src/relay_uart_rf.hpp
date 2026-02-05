#ifndef ARTEMIS_TEENSY_RELAY_UART_RF_HPP
#define ARTEMIS_TEENSY_RELAY_UART_RF_HPP

#include <Arduino.h>

#include "link_counters.hpp"
#include "rf23_driver.hpp"

class RelayUartRf {
 public:
  RelayUartRf(HardwareSerial& linkSerial, Rf23Driver& rfDriver, LinkCounters& counters);

  void begin(uint32_t baudRate);
  void poll();

 private:
  enum class ParseState {
    WAIT_MAGIC_0,
    WAIT_MAGIC_1,
    WAIT_LEN_LO,
    WAIT_LEN_HI,
    WAIT_PAYLOAD,
    WAIT_CRC_LO,
    WAIT_CRC_HI
  };

  void processUartByte(uint8_t b);
  void processCommandByte(uint8_t b);
  void processFrameByte(uint8_t b);
  void flushRfToUart();

  void resetFrameParser(bool timeoutReset);
  void handleCompletedFrame();
  bool sendUartFrame(const uint8_t* payload, uint16_t length);

  uint16_t crc16Ccitt(const uint8_t* data, uint16_t len) const;
  void emitLinkStatus();

  HardwareSerial& m_linkSerial;
  Rf23Driver& m_rf;
  LinkCounters& m_counters;

  ParseState m_state;
  uint8_t m_framePayload[64];
  uint16_t m_frameLength;
  uint16_t m_frameIndex;
  uint16_t m_frameCrc;
  uint8_t m_crcLo;
  bool m_inCommandMode;
  char m_commandBuffer[64];
  size_t m_commandIndex;
  uint32_t m_lastFrameByteMs;
};

#endif
