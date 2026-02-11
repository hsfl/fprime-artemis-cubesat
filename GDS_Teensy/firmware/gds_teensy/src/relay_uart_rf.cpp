#include "relay_uart_rf.hpp"

#include <string.h>

#include "link_protocol.hpp"

RelayUartRf::RelayUartRf(Stream& linkIo,
                         Rf23Driver& rfDriver,
                         LinkCounters& counters,
                         const RelayConfig& config)
    : m_linkIo(linkIo),
      m_rf(rfDriver),
      m_counters(counters),
      m_config(config),
      m_state(ParseState::WAIT_MAGIC_0),
      m_frameLength(0),
      m_frameIndex(0),
      m_frameCrc(0),
      m_crcLo(0),
      m_inCommandMode(false),
      m_commandIndex(0),
      m_lastFrameByteMs(0),
      m_nextMsgId(0),
      m_reassemblyActive(false),
      m_expectedMsgId(0),
      m_expectedSegIndex(0),
      m_expectedSegCount(0),
      m_reassemblyLen(0),
      m_lastSegmentMs(0),
      m_rawUartLen(0),
      m_lastRawUartByteMs(0) {
  memset(m_framePayload, 0, sizeof(m_framePayload));
  memset(m_commandBuffer, 0, sizeof(m_commandBuffer));
  memset(m_reassemblyBuf, 0, sizeof(m_reassemblyBuf));
  memset(m_rawUartBuf, 0, sizeof(m_rawUartBuf));
}

void RelayUartRf::begin() {
  resetFrameParser(false);
  resetReassembly(false, false);
}

void RelayUartRf::poll() {
  if (m_config.enableUartToRf) {
    while (m_linkIo.available() > 0) {
      const uint8_t b = static_cast<uint8_t>(m_linkIo.read());
      m_counters.uartRxBytes += 1;
      if (m_config.uartInputFramed) {
        processUartByte(b);
      } else {
        processRawUartByte(b);
      }
    }

    if (!m_config.uartInputFramed) {
      flushRawUartIfStale();
    }
  }

  flushRfToUart();
}

void RelayUartRf::processUartByte(uint8_t b) {
  const bool atFrameBoundary = (m_state == ParseState::WAIT_MAGIC_0);
  if (m_config.enableCommandMode &&
      (m_inCommandMode ||
       (atFrameBoundary && b == static_cast<uint8_t>(link_protocol::COMMAND_PREFIX)))) {
    processCommandByte(b);
    return;
  }

  processFrameByte(b);
}

void RelayUartRf::processRawUartByte(uint8_t b) {
  if (m_rawUartLen >= sizeof(m_rawUartBuf)) {
    sendPayloadOverRf(m_rawUartBuf, m_rawUartLen);
    m_rawUartLen = 0;
  }

  m_rawUartBuf[m_rawUartLen++] = b;
  m_lastRawUartByteMs = millis();

  if (m_rawUartLen >= sizeof(m_rawUartBuf)) {
    sendPayloadOverRf(m_rawUartBuf, m_rawUartLen);
    m_rawUartLen = 0;
  }
}

void RelayUartRf::flushRawUartIfStale() {
  if (m_rawUartLen == 0) {
    return;
  }

  const uint32_t now = millis();
  if ((now - m_lastRawUartByteMs) >= m_config.rawUartFlushMs) {
    sendPayloadOverRf(m_rawUartBuf, m_rawUartLen);
    m_rawUartLen = 0;
  }
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
      m_linkIo.write(reinterpret_cast<const uint8_t*>(link_protocol::RESP_PONG), n);
      m_counters.uartTxBytes += static_cast<uint32_t>(n);
    } else if (strcmp(m_commandBuffer, link_protocol::CMD_LINK_STATUS) == 0) {
      emitLinkStatus();
    } else if (strcmp(m_commandBuffer, link_protocol::CMD_RESET_COUNTERS) == 0) {
      m_counters.reset();
      const size_t n = strlen(link_protocol::RESP_RESET_OK);
      m_linkIo.write(reinterpret_cast<const uint8_t*>(link_protocol::RESP_RESET_OK), n);
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
  uint8_t rfBuffer[link_protocol::RF_PACKET_MAX_LEN] = {0};
  uint8_t rfLen = static_cast<uint8_t>(sizeof(rfBuffer));

  if (m_reassemblyActive && (millis() - m_lastSegmentMs) > link_protocol::RF_REASSEMBLY_TIMEOUT_MS) {
    resetReassembly(true, true);
  }

  while (m_rf.available()) {
    rfLen = static_cast<uint8_t>(sizeof(rfBuffer));
    if (m_rf.recv(rfBuffer, &rfLen) && rfLen > 0) {
      m_counters.rfRxPackets += 1;
      processRfSegment(rfBuffer, rfLen);
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

  sendPayloadOverRf(m_framePayload, m_frameLength);
}

bool RelayUartRf::sendUartFrame(const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.framingDrops += 1;
    return false;
  }

  const uint16_t crc = crc16Ccitt(payload, length);

  m_linkIo.write(link_protocol::FRAME_MAGIC_0);
  m_linkIo.write(link_protocol::FRAME_MAGIC_1);
  m_linkIo.write(static_cast<uint8_t>(length & 0xFF));
  m_linkIo.write(static_cast<uint8_t>((length >> 8) & 0xFF));
  m_linkIo.write(payload, length);
  m_linkIo.write(static_cast<uint8_t>(crc & 0xFF));
  m_linkIo.write(static_cast<uint8_t>((crc >> 8) & 0xFF));

  m_counters.uartTxBytes += static_cast<uint32_t>(length + 6);
  return true;
}

bool RelayUartRf::sendRawToUart(const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.framingDrops += 1;
    return false;
  }

  m_linkIo.write(payload, length);
  m_counters.uartTxBytes += static_cast<uint32_t>(length);
  return true;
}

bool RelayUartRf::sendPayloadOverRf(const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.rfOversizeDrops += 1;
    return false;
  }

  const uint16_t segCountU16 =
      static_cast<uint16_t>((length + link_protocol::RF_SEGMENT_MAX_DATA - 1) /
                            link_protocol::RF_SEGMENT_MAX_DATA);
  if (segCountU16 == 0 || segCountU16 > 255) {
    m_counters.rfOversizeDrops += 1;
    return false;
  }

  const uint8_t segCount = static_cast<uint8_t>(segCountU16);
  const uint8_t msgId = m_nextMsgId++;

  uint16_t sent = 0;
  for (uint8_t segIdx = 0; segIdx < segCount; segIdx++) {
    const uint16_t remaining = static_cast<uint16_t>(length - sent);
    const uint8_t chunkLen =
        static_cast<uint8_t>(remaining > link_protocol::RF_SEGMENT_MAX_DATA
                                 ? link_protocol::RF_SEGMENT_MAX_DATA
                                 : remaining);

    uint8_t rfPacket[link_protocol::RF_PACKET_MAX_LEN] = {0};
    rfPacket[0] = link_protocol::RF_SEGMENT_MAGIC;
    rfPacket[1] = msgId;
    rfPacket[2] = segIdx;
    rfPacket[3] = segCount;
    rfPacket[4] = chunkLen;
    memcpy(&rfPacket[link_protocol::RF_SEGMENT_HEADER_LEN], payload + sent, chunkLen);

    const uint8_t rfLen = static_cast<uint8_t>(link_protocol::RF_SEGMENT_HEADER_LEN + chunkLen);
    if (!m_rf.send(rfPacket, rfLen)) {
      m_counters.rfTxDrops += 1;
      return false;
    }

    sent = static_cast<uint16_t>(sent + chunkLen);
    m_counters.rfTxPackets += 1;
    m_counters.rfTxSegments += 1;
  }

  m_counters.rfTxMessages += 1;
  return true;
}

void RelayUartRf::processRfSegment(const uint8_t* packet, uint8_t packetLen) {
  if (packetLen < link_protocol::RF_SEGMENT_HEADER_LEN) {
    m_counters.framingDrops += 1;
    return;
  }

  if (packet[0] != link_protocol::RF_SEGMENT_MAGIC) {
    m_counters.framingDrops += 1;
    return;
  }

  const uint8_t msgId = packet[1];
  const uint8_t segIdx = packet[2];
  const uint8_t segCount = packet[3];
  const uint8_t chunkLen = packet[4];

  if (segCount == 0 || segIdx >= segCount) {
    m_counters.framingDrops += 1;
    return;
  }

  if (chunkLen == 0) {
    m_counters.framingDrops += 1;
    return;
  }

  if (packetLen != static_cast<uint8_t>(link_protocol::RF_SEGMENT_HEADER_LEN + chunkLen)) {
    m_counters.framingDrops += 1;
    return;
  }

  const uint32_t now = millis();
  if (m_reassemblyActive && (now - m_lastSegmentMs) > link_protocol::RF_REASSEMBLY_TIMEOUT_MS) {
    resetReassembly(true, true);
  }

  if (!m_reassemblyActive) {
    if (segIdx != 0) {
      m_counters.rfReassemblyDrops += 1;
      return;
    }

    m_reassemblyActive = true;
    m_expectedMsgId = msgId;
    m_expectedSegIndex = 0;
    m_expectedSegCount = segCount;
    m_reassemblyLen = 0;
  }

  if (msgId != m_expectedMsgId || segCount != m_expectedSegCount || segIdx != m_expectedSegIndex) {
    resetReassembly(false, true);

    if (segIdx == 0) {
      m_reassemblyActive = true;
      m_expectedMsgId = msgId;
      m_expectedSegIndex = 0;
      m_expectedSegCount = segCount;
      m_reassemblyLen = 0;
    } else {
      return;
    }
  }

  if (static_cast<uint16_t>(m_reassemblyLen + chunkLen) > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.rfOversizeDrops += 1;
    resetReassembly(false, true);
    return;
  }

  memcpy(m_reassemblyBuf + m_reassemblyLen, packet + link_protocol::RF_SEGMENT_HEADER_LEN, chunkLen);
  m_reassemblyLen = static_cast<uint16_t>(m_reassemblyLen + chunkLen);
  m_expectedSegIndex = static_cast<uint8_t>(m_expectedSegIndex + 1);
  m_lastSegmentMs = now;
  m_counters.rfRxSegments += 1;

  if (segIdx + 1 == segCount) {
    m_counters.rfRxMessages += 1;
    if (m_config.uartOutputFramed) {
      sendUartFrame(m_reassemblyBuf, m_reassemblyLen);
    } else {
      sendRawToUart(m_reassemblyBuf, m_reassemblyLen);
    }
    resetReassembly(false, false);
  }
}

void RelayUartRf::resetReassembly(bool timeoutReset, bool dropReset) {
  m_reassemblyActive = false;
  m_expectedMsgId = 0;
  m_expectedSegIndex = 0;
  m_expectedSegCount = 0;
  m_reassemblyLen = 0;
  m_lastSegmentMs = 0;

  if (timeoutReset) {
    m_counters.rfReassemblyTimeouts += 1;
  }
  if (dropReset) {
    m_counters.rfReassemblyDrops += 1;
  }
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
  char statusLine[360] = {0};
  const int n =
      snprintf(statusLine,
               sizeof(statusLine),
               "#LINK_STATUS uart_rx=%lu uart_tx=%lu rf_rx_pkt=%lu rf_tx_pkt=%lu rf_rx_msg=%lu rf_tx_msg=%lu rf_rx_seg=%lu rf_tx_seg=%lu crc_drops=%lu framing_drops=%lu uart_timeouts=%lu rf_reasm_timeouts=%lu rf_reasm_drops=%lu rf_oversize_drops=%lu rf_tx_drops=%lu\\n",
               static_cast<unsigned long>(m_counters.uartRxBytes),
               static_cast<unsigned long>(m_counters.uartTxBytes),
               static_cast<unsigned long>(m_counters.rfRxPackets),
               static_cast<unsigned long>(m_counters.rfTxPackets),
               static_cast<unsigned long>(m_counters.rfRxMessages),
               static_cast<unsigned long>(m_counters.rfTxMessages),
               static_cast<unsigned long>(m_counters.rfRxSegments),
               static_cast<unsigned long>(m_counters.rfTxSegments),
               static_cast<unsigned long>(m_counters.crcDrops),
               static_cast<unsigned long>(m_counters.framingDrops),
               static_cast<unsigned long>(m_counters.timeoutEvents),
               static_cast<unsigned long>(m_counters.rfReassemblyTimeouts),
               static_cast<unsigned long>(m_counters.rfReassemblyDrops),
               static_cast<unsigned long>(m_counters.rfOversizeDrops),
               static_cast<unsigned long>(m_counters.rfTxDrops));

  if (n > 0) {
    m_linkIo.write(reinterpret_cast<const uint8_t*>(statusLine), static_cast<size_t>(n));
    m_counters.uartTxBytes += static_cast<uint32_t>(n);
  }
}
