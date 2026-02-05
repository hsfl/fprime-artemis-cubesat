#include "relay_uart_rf.hpp"

#include <string.h>

#include "link_protocol.hpp"

RelayUartRf::RelayUartRf(HardwareSerial& linkSerial, Rf23Driver& rfDriver, LinkCounters& counters)
    : m_linkSerial(linkSerial),
      m_rf(rfDriver),
      m_counters(counters),
      m_state(ParseState::WAIT_MAGIC_0),
      m_frameLength(0),
      m_frameIndex(0),
      m_frameCrc(0),
      m_crcLo(0),
      m_inCommandMode(false),
      m_commandIndex(0),
      m_lastFrameByteMs(0) {
  memset(m_framePayload, 0, sizeof(m_framePayload));
  memset(m_commandBuffer, 0, sizeof(m_commandBuffer));
}

void RelayUartRf::begin(uint32_t baudRate) {
  m_linkSerial.begin(baudRate);
  resetFrameParser(false);
}

void RelayUartRf::poll() {
  while (m_linkSerial.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(m_linkSerial.read());
    m_counters.uartRxBytes += 1;
    processUartByte(b);
  }

  flushRfToUart();
}

void RelayUartRf::processUartByte(uint8_t b) {
  if (m_inCommandMode || b == static_cast<uint8_t>(link_protocol::COMMAND_PREFIX)) {
    processCommandByte(b);
    return;
  }

  processFrameByte(b);
}

void RelayUartRf::processCommandByte(uint8_t b) {
  if (!m_inCommandMode) {
    m_inCommandMode = true;
    m_commandIndex = 0;
    memset(m_commandBuffer, 0, sizeof(m_commandBuffer));
    return;
  }

  if (b == '\n' || b == '\r') {
    m_commandBuffer[m_commandIndex] = '\0';
    m_inCommandMode = false;

    if (strcmp(m_commandBuffer, link_protocol::CMD_PING) == 0) {
      const size_t n = strlen(link_protocol::RESP_PONG);
      m_linkSerial.write(reinterpret_cast<const uint8_t*>(link_protocol::RESP_PONG), n);
      m_counters.uartTxBytes += static_cast<uint32_t>(n);
    } else if (strcmp(m_commandBuffer, link_protocol::CMD_LINK_STATUS) == 0) {
      emitLinkStatus();
    } else if (strcmp(m_commandBuffer, link_protocol::CMD_RESET_COUNTERS) == 0) {
      m_counters.reset();
      const size_t n = strlen(link_protocol::RESP_RESET_OK);
      m_linkSerial.write(reinterpret_cast<const uint8_t*>(link_protocol::RESP_RESET_OK), n);
      m_counters.uartTxBytes += static_cast<uint32_t>(n);
    }
    return;
  }

  if (m_commandIndex + 1 < sizeof(m_commandBuffer)) {
    m_commandBuffer[m_commandIndex++] = static_cast<char>(b);
  }
}

void RelayUartRf::processFrameByte(uint8_t b) {
  const uint32_t now = millis();
  if (m_state != ParseState::WAIT_MAGIC_0 && (now - m_lastFrameByteMs) > link_protocol::FRAME_TIMEOUT_MS) {
    resetFrameParser(true);
  }
  m_lastFrameByteMs = now;

  switch (m_state) {
    case ParseState::WAIT_MAGIC_0:
      if (b == link_protocol::FRAME_MAGIC_0) {
        m_state = ParseState::WAIT_MAGIC_1;
      }
      break;

    case ParseState::WAIT_MAGIC_1:
      if (b == link_protocol::FRAME_MAGIC_1) {
        m_state = ParseState::WAIT_LEN_LO;
      } else {
        resetFrameParser(false);
      }
      break;

    case ParseState::WAIT_LEN_LO:
      m_frameLength = b;
      m_state = ParseState::WAIT_LEN_HI;
      break;

    case ParseState::WAIT_LEN_HI:
      m_frameLength |= static_cast<uint16_t>(b) << 8;
      if (m_frameLength == 0 || m_frameLength > link_protocol::FRAME_MAX_PAYLOAD) {
        m_counters.framingDrops += 1;
        resetFrameParser(false);
      } else {
        m_frameIndex = 0;
        m_state = ParseState::WAIT_PAYLOAD;
      }
      break;

    case ParseState::WAIT_PAYLOAD:
      m_framePayload[m_frameIndex++] = b;
      if (m_frameIndex >= m_frameLength) {
        m_state = ParseState::WAIT_CRC_LO;
      }
      break;

    case ParseState::WAIT_CRC_LO:
      m_crcLo = b;
      m_state = ParseState::WAIT_CRC_HI;
      break;

    case ParseState::WAIT_CRC_HI:
      m_frameCrc = static_cast<uint16_t>(m_crcLo) | (static_cast<uint16_t>(b) << 8);
      handleCompletedFrame();
      resetFrameParser(false);
      break;
  }
}

void RelayUartRf::flushRfToUart() {
  uint8_t rfBuffer[64] = {0};
  uint8_t rfLen = static_cast<uint8_t>(sizeof(rfBuffer));

  while (m_rf.available()) {
    rfLen = static_cast<uint8_t>(sizeof(rfBuffer));
    if (m_rf.recv(rfBuffer, &rfLen) && rfLen > 0) {
      m_counters.rfRxPackets += 1;
      if (sendUartFrame(rfBuffer, rfLen)) {
        // counted in sendUartFrame
      }
    }
  }
}

void RelayUartRf::resetFrameParser(bool timeoutReset) {
  m_state = ParseState::WAIT_MAGIC_0;
  m_frameLength = 0;
  m_frameIndex = 0;
  m_frameCrc = 0;
  m_crcLo = 0;
  if (timeoutReset) {
    m_counters.timeoutEvents += 1;
  }
}

void RelayUartRf::handleCompletedFrame() {
  const uint16_t calc = crc16Ccitt(m_framePayload, m_frameLength);
  if (calc != m_frameCrc) {
    m_counters.crcDrops += 1;
    return;
  }

  if (m_rf.send(m_framePayload, static_cast<uint8_t>(m_frameLength))) {
    m_counters.rfTxPackets += 1;
  }
}

bool RelayUartRf::sendUartFrame(const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.framingDrops += 1;
    return false;
  }

  const uint16_t crc = crc16Ccitt(payload, length);

  m_linkSerial.write(link_protocol::FRAME_MAGIC_0);
  m_linkSerial.write(link_protocol::FRAME_MAGIC_1);
  m_linkSerial.write(static_cast<uint8_t>(length & 0xFF));
  m_linkSerial.write(static_cast<uint8_t>((length >> 8) & 0xFF));
  m_linkSerial.write(payload, length);
  m_linkSerial.write(static_cast<uint8_t>(crc & 0xFF));
  m_linkSerial.write(static_cast<uint8_t>((crc >> 8) & 0xFF));

  m_counters.uartTxBytes += static_cast<uint32_t>(length + 6);
  return true;
}

uint16_t RelayUartRf::crc16Ccitt(const uint8_t* data, uint16_t len) const {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x8000U) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021U);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

void RelayUartRf::emitLinkStatus() {
  char statusLine[220] = {0};
  const int n = snprintf(statusLine,
                         sizeof(statusLine),
                         "#LINK_STATUS uart_rx=%lu uart_tx=%lu rf_rx=%lu rf_tx=%lu crc_drops=%lu framing_drops=%lu timeouts=%lu\n",
                         static_cast<unsigned long>(m_counters.uartRxBytes),
                         static_cast<unsigned long>(m_counters.uartTxBytes),
                         static_cast<unsigned long>(m_counters.rfRxPackets),
                         static_cast<unsigned long>(m_counters.rfTxPackets),
                         static_cast<unsigned long>(m_counters.crcDrops),
                         static_cast<unsigned long>(m_counters.framingDrops),
                         static_cast<unsigned long>(m_counters.timeoutEvents));

  if (n > 0) {
    m_linkSerial.write(reinterpret_cast<const uint8_t*>(statusLine), static_cast<size_t>(n));
    m_counters.uartTxBytes += static_cast<uint32_t>(n);
  }
}
