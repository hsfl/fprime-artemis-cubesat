#include "relay_uart_rf.hpp"

#include <string.h>

#include "link_protocol.hpp"
#include "wdt_guard.hpp"

RelayUartRf::RelayUartRf(Stream& linkIo,
                         Rf23Driver& rfDriver,
                         LinkCounters& counters,
                         const RelayConfig& config,
                         Stream* payloadIo,
                         LocalChannelHandler* localHandler)
    : m_linkIo(linkIo),
      m_payloadIo(payloadIo),
      m_localHandler(localHandler),
      m_rf(rfDriver),
      m_counters(counters),
      m_config(config),
      m_state(ParseState::WAIT_MAGIC_0),
      m_frameChannel(link_protocol::CHANNEL_CCSDS),
      m_frameLength(0),
      m_frameIndex(0),
      m_frameCrc(0),
      m_crcLo(0),
      m_inCommandMode(false),
      m_commandIndex(0),
      m_lastFrameByteMs(0),
      m_nextMsgId(0),
      m_rawUartLen(0),
      m_lastRawUartByteMs(0),
      m_payloadUartLen(0),
      m_lastPayloadUartByteMs(0),
      m_uplinkHead(0),
      m_uplinkTail(0),
      m_uplinkCount(0),
      m_downlinkHead(0),
      m_downlinkTail(0),
      m_downlinkCount(0) {
  memset(m_framePayload, 0, sizeof(m_framePayload));
  memset(m_commandBuffer, 0, sizeof(m_commandBuffer));
  memset(m_reassembly, 0, sizeof(m_reassembly));
  memset(m_rawUartBuf, 0, sizeof(m_rawUartBuf));
  memset(m_payloadUartBuf, 0, sizeof(m_payloadUartBuf));
  memset(m_uplinkQueue, 0, sizeof(m_uplinkQueue));
  memset(m_downlinkQueue, 0, sizeof(m_downlinkQueue));

  if (m_config.uplinkQueueDepth == 0 || m_config.uplinkQueueDepth > MAX_QUEUE_DEPTH) {
    m_config.uplinkQueueDepth = 16;
  }
  if (m_config.downlinkQueueDepth == 0 || m_config.downlinkQueueDepth > MAX_QUEUE_DEPTH) {
    m_config.downlinkQueueDepth = 16;
  }
  if (!link_protocol::isRfChannel(m_config.defaultUartChannel)) {
    m_config.defaultUartChannel = link_protocol::CHANNEL_CCSDS;
  }
}

void RelayUartRf::begin() {
  resetFrameParser(false);
  for (uint8_t channel = 0; channel < link_protocol::CHANNEL_COUNT; channel++) {
    resetReassembly(channel, false, false);
  }
}

void RelayUartRf::poll() {
  wdt_guard::feed();
  if (m_config.enableUartToRf) {
    while (m_linkIo.available() > 0) {
      const uint8_t b = static_cast<uint8_t>(m_linkIo.read());
      m_counters.uartRxBytes += 1;
      if (m_config.uartInputFramed) {
        processUartByte(b);
      } else {
        processRawUartByte(b, m_config.defaultUartChannel);
      }
      wdt_guard::feed();
    }

    if (!m_config.uartInputFramed) {
      flushRawUartIfStale();
    }
  }

  if (m_payloadIo != nullptr) {
    while (m_payloadIo->available() > 0) {
      const uint8_t b = static_cast<uint8_t>(m_payloadIo->read());
      m_counters.uartRxBytes += 1;
      processRawUartByte(b, link_protocol::CHANNEL_PAYLOAD);
      wdt_guard::feed();
    }
    flushPayloadUartIfStale();
  }

  flushRfToUart();
  flushLocalResponseToUart();
  serviceUplinkQueue();
  serviceDownlinkQueue();
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

void RelayUartRf::processRawUartByte(uint8_t b, uint8_t channel) {
  if (channel == link_protocol::CHANNEL_PAYLOAD) {
    m_counters.payloadUartRxBytes += 1;
  }

  uint8_t* rawBuf = m_rawUartBuf;
  uint16_t* rawLen = &m_rawUartLen;
  uint32_t* lastByteMs = &m_lastRawUartByteMs;
  if (channel == link_protocol::CHANNEL_PAYLOAD) {
    rawBuf = m_payloadUartBuf;
    rawLen = &m_payloadUartLen;
    lastByteMs = &m_lastPayloadUartByteMs;
  }

  uint16_t rawChunkLimit = m_config.rawUartChunkBytes;
  if (rawChunkLimit == 0 || rawChunkLimit > link_protocol::FRAME_MAX_PAYLOAD) {
    rawChunkLimit = link_protocol::RF_SEGMENT_MAX_DATA;
  }
  if (*rawLen >= rawChunkLimit) {
    enqueueUplinkMessage(channel, rawBuf, *rawLen);
    *rawLen = 0;
  }

  rawBuf[(*rawLen)++] = b;
  *lastByteMs = millis();

  if (*rawLen >= rawChunkLimit) {
    enqueueUplinkMessage(channel, rawBuf, *rawLen);
    *rawLen = 0;
  }
}

void RelayUartRf::flushRawUartIfStale() {
  if (m_rawUartLen == 0) {
    return;
  }

  const uint32_t now = millis();
  if ((now - m_lastRawUartByteMs) >= m_config.rawUartFlushMs) {
    if (m_config.rawUartChunkBytes > link_protocol::RF_SEGMENT_MAX_DATA) {
      m_counters.framingDrops += 1;
      m_rawUartLen = 0;
      return;
    }
    enqueueUplinkMessage(m_config.defaultUartChannel, m_rawUartBuf, m_rawUartLen);
    m_rawUartLen = 0;
  }
}

void RelayUartRf::flushPayloadUartIfStale() {
  if (m_payloadUartLen == 0) {
    return;
  }

  const uint32_t now = millis();
  if ((now - m_lastPayloadUartByteMs) >= m_config.rawUartFlushMs) {
    enqueueUplinkMessage(link_protocol::CHANNEL_PAYLOAD, m_payloadUartBuf, m_payloadUartLen);
    m_payloadUartLen = 0;
  }
}

void RelayUartRf::flushLocalResponseToUart() {
  if (m_localHandler == nullptr) {
    return;
  }

  uint8_t response[link_protocol::FRAME_MAX_PAYLOAD] = {0};
  uint16_t responseLen = 0;
  if (m_localHandler->pollLocalResponse(response, responseLen)) {
    sendUartFrame(link_protocol::CHANNEL_TEENSY_LOCAL, response, responseLen);
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
        m_state = ParseState::WAIT_CHANNEL;
      } else {
        resetFrameParser(false);
      }
      break;

    case ParseState::WAIT_CHANNEL:
      if (link_protocol::isValidChannel(b)) {
        m_frameChannel = b;
        m_state = ParseState::WAIT_LEN_LO;
      } else {
        m_counters.framingDrops += 1;
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

  const uint32_t now = millis();
  for (uint8_t channel = 0; channel < link_protocol::CHANNEL_COUNT; channel++) {
    ReassemblyState& state = m_reassembly[channel];
    if (state.active && (now - state.lastSegmentMs) > link_protocol::RF_REASSEMBLY_TIMEOUT_MS) {
      resetReassembly(channel, true, true);
    }
  }

  while (m_rf.available()) {
    rfLen = static_cast<uint8_t>(sizeof(rfBuffer));
    const Rf23ReceiveResult result = m_rf.recv(rfBuffer, &rfLen);
    if (acceptRfReceiveResult(result) && rfLen > 0) {
      m_counters.rfRxPackets += 1;
      processRfSegment(rfBuffer, rfLen);
    }
  }
}

void RelayUartRf::resetFrameParser(bool timeoutReset) {
  m_state = ParseState::WAIT_MAGIC_0;
  m_frameChannel = link_protocol::CHANNEL_CCSDS;
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

  if (m_frameChannel == link_protocol::CHANNEL_PAYLOAD) {
    m_counters.payloadUartRxBytes += m_frameLength;
  }
  if (m_frameChannel == link_protocol::CHANNEL_TEENSY_LOCAL) {
    if (m_localHandler == nullptr || !m_localHandler->beginLocalFrame(m_framePayload, m_frameLength)) {
      m_counters.framingDrops += 1;
    }
    return;
  }

  enqueueUplinkMessage(m_frameChannel, m_framePayload, m_frameLength);
}

bool RelayUartRf::sendUartFrame(uint8_t channel, const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.framingDrops += 1;
    return false;
  }
  if (!link_protocol::isValidChannel(channel)) {
    m_counters.framingDrops += 1;
    return false;
  }

  const uint16_t crc = crc16Ccitt(payload, length);

  m_linkIo.write(link_protocol::FRAME_MAGIC_0);
  m_linkIo.write(link_protocol::FRAME_MAGIC_1);
  m_linkIo.write(channel);
  m_linkIo.write(static_cast<uint8_t>(length & 0xFF));
  m_linkIo.write(static_cast<uint8_t>((length >> 8) & 0xFF));
  m_linkIo.write(payload, length);
  m_linkIo.write(static_cast<uint8_t>(crc & 0xFF));
  m_linkIo.write(static_cast<uint8_t>((crc >> 8) & 0xFF));

  m_counters.uartTxBytes += static_cast<uint32_t>(length + 7);
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

bool RelayUartRf::sendPayloadOverRf(uint8_t channel, const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.rfOversizeDrops += 1;
    return false;
  }
  if (!link_protocol::isRfChannel(channel)) {
    m_counters.framingDrops += 1;
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
    rfPacket[0] = link_protocol::magicForChannel(channel);
    rfPacket[1] = msgId;
    rfPacket[2] = segIdx;
    rfPacket[3] = segCount;
    rfPacket[4] = chunkLen;
    memcpy(&rfPacket[link_protocol::RF_SEGMENT_HEADER_LEN], payload + sent, chunkLen);

    const uint8_t rfLen = static_cast<uint8_t>(link_protocol::RF_SEGMENT_HEADER_LEN + chunkLen);
    wdt_guard::feed();
    const bool sentOk = link_protocol::txAckRequiredForChannel(channel)
                            ? sendRfPacketWithAck(rfPacket, rfLen, channel, msgId, segIdx)
                            : sendRfPacket(rfPacket, rfLen);
    wdt_guard::feed();
    if (!sentOk) {
      m_counters.rfTxDrops += 1;
      return false;
    }

    sent = static_cast<uint16_t>(sent + chunkLen);
    m_counters.rfTxPackets += 1;
    m_counters.rfTxSegments += 1;
    if (channel == link_protocol::CHANNEL_PAYLOAD) {
      m_counters.payloadRfTxSegments += 1;
    }

    if (channel == link_protocol::CHANNEL_PAYLOAD) {
      // Payload messages fit in one segment and do not use RF ACKs. Give the
      // peer time to drain each packet before the next preamble.
      wdt_guard::feed();
      delay(link_protocol::RF_PAYLOAD_INTER_PACKET_GAP_MS);
      wdt_guard::feed();
    } else if (segIdx + 1 < segCount) {
      // ACK turnaround already paces CCSDS packets; retain only the original
      // between-segment guard for multi-segment CCSDS messages.
      wdt_guard::feed();
      delay(link_protocol::RF_INTER_SEGMENT_GAP_MS);
      wdt_guard::feed();
    }
  }

  m_counters.rfTxMessages += 1;
  if (channel == link_protocol::CHANNEL_PAYLOAD) {
    m_counters.payloadRfTxMessages += 1;
  }
  return true;
}

bool RelayUartRf::sendRfPacket(const uint8_t* packet, uint8_t packetLen) {
  switch (m_rf.send(packet, packetLen)) {
    case Rf23SendResult::SENT:
      return true;
    case Rf23SendResult::TX_TIMEOUT:
      m_counters.rfTxTimeouts += 1;
      m_counters.rfRecoveries += 1;
      m_counters.rfTxTerminalFailures += 1;
      return false;
    case Rf23SendResult::START_FAILED:
      m_counters.rfTxTerminalFailures += 1;
      return false;
  }
  m_counters.rfTxTerminalFailures += 1;
  return false;
}

bool RelayUartRf::sendRfPacketWithAck(const uint8_t* packet,
                                      uint8_t packetLen,
                                      uint8_t channel,
                                      uint8_t msgId,
                                      uint8_t segIdx) {
  for (uint8_t attempt = 0; attempt <= link_protocol::RF_ACK_RETRIES; attempt++) {
    wdt_guard::feed();
    if (!sendRfPacket(packet, packetLen)) {
      return false;
    }
    if (waitForAck(channel, msgId, segIdx)) {
      return true;
    }
    m_counters.rfAckTimeouts += 1;
    if (attempt < link_protocol::RF_ACK_RETRIES) {
      m_counters.rfRetries += 1;
    }
  }
  return false;
}

bool RelayUartRf::waitForAck(uint8_t channel, uint8_t msgId, uint8_t segIdx) {
  const uint32_t startMs = millis();
  uint8_t rfBuffer[link_protocol::RF_PACKET_MAX_LEN] = {0};

  while ((millis() - startMs) < link_protocol::RF_ACK_TIMEOUT_MS) {
    wdt_guard::feed();
    while (m_rf.available()) {
      uint8_t rfLen = static_cast<uint8_t>(sizeof(rfBuffer));
      const Rf23ReceiveResult result = m_rf.recv(rfBuffer, &rfLen);
      if (acceptRfReceiveResult(result) && rfLen > 0) {
        if (isAckPacket(rfBuffer, rfLen, channel, msgId, segIdx)) {
          m_counters.rfAckRx += 1;
          return true;
        }
        m_counters.rfRxPackets += 1;
        processRfSegment(rfBuffer, rfLen);
        wdt_guard::feed();
      }
    }
  }
  return false;
}

bool RelayUartRf::isAckPacket(const uint8_t* packet,
                              uint8_t packetLen,
                              uint8_t channel,
                              uint8_t msgId,
                              uint8_t segIdx) const {
  return packetLen == link_protocol::RF_SEGMENT_HEADER_LEN &&
         packet[0] == link_protocol::magicForChannel(channel) &&
         packet[1] == msgId &&
         packet[2] == link_protocol::RF_ACK_SEGMENT_INDEX &&
         packet[3] == segIdx &&
         packet[4] == 0;
}

bool RelayUartRf::acceptRfReceiveResult(Rf23ReceiveResult result) {
  switch (result) {
    case Rf23ReceiveResult::ACCEPTED:
      return true;
    case Rf23ReceiveResult::WRONG_NETWORK:
      m_counters.rfWrongNetworkDrops += 1;
      break;
    case Rf23ReceiveResult::WRONG_ADDRESS:
      m_counters.rfWrongAddressDrops += 1;
      break;
    case Rf23ReceiveResult::WRONG_VERSION:
      m_counters.rfVersionDrops += 1;
      break;
    case Rf23ReceiveResult::NO_PACKET:
      break;
  }
  return false;
}

bool RelayUartRf::sendAck(uint8_t channel, uint8_t msgId, uint8_t segIdx) {
  uint8_t ackPacket[link_protocol::RF_SEGMENT_HEADER_LEN] = {
      link_protocol::magicForChannel(channel),
      msgId,
      link_protocol::RF_ACK_SEGMENT_INDEX,
      segIdx,
      0,
  };
  const bool ok = sendRfPacket(ackPacket, sizeof(ackPacket));
  if (ok) {
    m_counters.rfAckTx += 1;
  }
  return ok;
}

void RelayUartRf::processRfSegment(const uint8_t* packet, uint8_t packetLen) {
  if (packetLen < link_protocol::RF_SEGMENT_HEADER_LEN) {
    m_counters.framingDrops += 1;
    return;
  }

  uint8_t channel = link_protocol::CHANNEL_CCSDS;
  if (!link_protocol::channelForMagic(packet[0], channel)) {
    m_counters.framingDrops += 1;
    return;
  }

  const uint8_t msgId = packet[1];
  const uint8_t segIdx = packet[2];
  const uint8_t segCount = packet[3];
  const uint8_t chunkLen = packet[4];

  if (segIdx == link_protocol::RF_ACK_SEGMENT_INDEX) {
    return;
  }

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
  ReassemblyState& state = m_reassembly[channel];
  if (state.active && (now - state.lastSegmentMs) > link_protocol::RF_REASSEMBLY_TIMEOUT_MS) {
    resetReassembly(channel, true, true);
  }

  if (!state.active) {
    if (segIdx != 0) {
      m_counters.rfReassemblyDrops += 1;
      return;
    }

    if (state.seenRxMsgId && static_cast<uint8_t>(state.lastRxMsgId + 1) != msgId) {
      m_counters.rfMsgIdGaps += 1;
    }
    state.active = true;
    state.expectedMsgId = msgId;
    state.expectedSegIndex = 0;
    state.expectedSegCount = segCount;
    state.length = 0;
  }

  if (msgId == state.expectedMsgId && segCount == state.expectedSegCount && segIdx < state.expectedSegIndex) {
    if (link_protocol::rxAckRequiredForChannel(channel)) {
      sendAck(channel, msgId, segIdx);
    }
    return;
  }

  if (msgId != state.expectedMsgId || segCount != state.expectedSegCount || segIdx != state.expectedSegIndex) {
    resetReassembly(channel, false, true);

    if (segIdx == 0) {
      state.active = true;
      state.expectedMsgId = msgId;
      state.expectedSegIndex = 0;
      state.expectedSegCount = segCount;
      state.length = 0;
    } else {
      return;
    }
  }

  if (static_cast<uint16_t>(state.length + chunkLen) > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.rfOversizeDrops += 1;
    resetReassembly(channel, false, true);
    return;
  }

  memcpy(state.buffer + state.length, packet + link_protocol::RF_SEGMENT_HEADER_LEN, chunkLen);
  state.length = static_cast<uint16_t>(state.length + chunkLen);
  state.expectedSegIndex = static_cast<uint8_t>(state.expectedSegIndex + 1);
  state.lastSegmentMs = now;
  m_counters.rfRxSegments += 1;
  if (channel == link_protocol::CHANNEL_PAYLOAD) {
    m_counters.payloadRfRxSegments += 1;
  }
  if (link_protocol::rxAckRequiredForChannel(channel)) {
    sendAck(channel, msgId, segIdx);
  }

  if (segIdx + 1 == segCount) {
    m_counters.rfRxMessages += 1;
    if (channel == link_protocol::CHANNEL_PAYLOAD) {
      m_counters.payloadRfRxMessages += 1;
    }
    state.seenRxMsgId = true;
    state.lastRxMsgId = msgId;
    enqueueDownlinkMessage(channel, state.buffer, state.length);
    resetReassembly(channel, false, false);
  }
}

void RelayUartRf::resetReassembly(uint8_t channel, bool timeoutReset, bool dropReset) {
  if (!link_protocol::isRfChannel(channel)) {
    return;
  }

  ReassemblyState& state = m_reassembly[channel];
  state.active = false;
  state.expectedMsgId = 0;
  state.expectedSegIndex = 0;
  state.expectedSegCount = 0;
  state.length = 0;
  state.lastSegmentMs = 0;

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
  char statusLine[640] = {0};
  const int n =
      snprintf(statusLine,
               sizeof(statusLine),
               "#LINK_STATUS uart_rx=%lu uart_tx=%lu rf_rx_pkt=%lu rf_tx_pkt=%lu rf_rx_msg=%lu rf_tx_msg=%lu rf_rx_seg=%lu rf_tx_seg=%lu crc_drops=%lu framing_drops=%lu uart_timeouts=%lu rf_reasm_timeouts=%lu rf_reasm_drops=%lu rf_oversize_drops=%lu rf_tx_drops=%lu rf_tx_timeouts=%lu rf_recoveries=%lu rf_tx_terminal_failures=%lu rf_msg_id_gaps=%lu rf_ack_rx=%lu rf_ack_tx=%lu rf_retries=%lu rf_ack_timeouts=%lu rf_wrong_network=%lu rf_wrong_address=%lu rf_wrong_version=%lu up_q_drops=%lu down_q_drops=%lu\\n",
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
               static_cast<unsigned long>(m_counters.rfTxDrops),
               static_cast<unsigned long>(m_counters.rfTxTimeouts),
               static_cast<unsigned long>(m_counters.rfRecoveries),
               static_cast<unsigned long>(m_counters.rfTxTerminalFailures),
               static_cast<unsigned long>(m_counters.rfMsgIdGaps),
               static_cast<unsigned long>(m_counters.rfAckRx),
               static_cast<unsigned long>(m_counters.rfAckTx),
               static_cast<unsigned long>(m_counters.rfRetries),
               static_cast<unsigned long>(m_counters.rfAckTimeouts),
               static_cast<unsigned long>(m_counters.rfWrongNetworkDrops),
               static_cast<unsigned long>(m_counters.rfWrongAddressDrops),
               static_cast<unsigned long>(m_counters.rfVersionDrops),
               static_cast<unsigned long>(m_counters.uplinkQueueDrops),
               static_cast<unsigned long>(m_counters.downlinkQueueDrops));

  if (n > 0) {
    const size_t writeLen =
        static_cast<size_t>(n) < sizeof(statusLine) ? static_cast<size_t>(n) : sizeof(statusLine) - 1U;
    m_linkIo.write(reinterpret_cast<const uint8_t*>(statusLine), writeLen);
    m_counters.uartTxBytes += static_cast<uint32_t>(writeLen);
  }
}

bool RelayUartRf::enqueueUplinkMessage(uint8_t channel, const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.rfOversizeDrops += 1;
    return false;
  }
  if (!link_protocol::isRfChannel(channel)) {
    m_counters.framingDrops += 1;
    return false;
  }

  if (m_uplinkCount >= m_config.uplinkQueueDepth) {
    m_counters.uplinkQueueDrops += 1;
    return false;
  }

  QueueEntry& entry = m_uplinkQueue[m_uplinkHead];
  entry.channel = channel;
  entry.length = length;
  memcpy(entry.payload, payload, length);
  m_uplinkHead = static_cast<uint8_t>((m_uplinkHead + 1) % m_config.uplinkQueueDepth);
  m_uplinkCount = static_cast<uint8_t>(m_uplinkCount + 1);
  return true;
}

bool RelayUartRf::enqueueDownlinkMessage(uint8_t channel, const uint8_t* payload, uint16_t length) {
  if (length == 0 || length > link_protocol::FRAME_MAX_PAYLOAD) {
    m_counters.rfOversizeDrops += 1;
    return false;
  }
  if (!link_protocol::isRfChannel(channel) && channel != link_protocol::CHANNEL_TEENSY_LOCAL) {
    m_counters.framingDrops += 1;
    return false;
  }

  if (m_downlinkCount >= m_config.downlinkQueueDepth) {
    m_counters.downlinkQueueDrops += 1;
    return false;
  }

  QueueEntry& entry = m_downlinkQueue[m_downlinkHead];
  entry.channel = channel;
  entry.length = length;
  memcpy(entry.payload, payload, length);
  m_downlinkHead = static_cast<uint8_t>((m_downlinkHead + 1) % m_config.downlinkQueueDepth);
  m_downlinkCount = static_cast<uint8_t>(m_downlinkCount + 1);
  return true;
}

void RelayUartRf::serviceUplinkQueue() {
  if (m_uplinkCount == 0) {
    return;
  }

  QueueEntry& entry = m_uplinkQueue[m_uplinkTail];
  if (link_protocol::isRfChannel(entry.channel)) {
    sendPayloadOverRf(entry.channel, entry.payload, entry.length);
  } else {
    m_counters.framingDrops += 1;
  }
  m_uplinkTail = static_cast<uint8_t>((m_uplinkTail + 1) % m_config.uplinkQueueDepth);
  m_uplinkCount = static_cast<uint8_t>(m_uplinkCount - 1);
}

void RelayUartRf::serviceDownlinkQueue() {
  if (m_downlinkCount == 0) {
    return;
  }

  QueueEntry& entry = m_downlinkQueue[m_downlinkTail];
  if (entry.channel == link_protocol::CHANNEL_PAYLOAD && m_payloadIo != nullptr) {
    const size_t written = m_payloadIo->write(entry.payload, entry.length);
    m_payloadIo->flush();
    m_counters.uartTxBytes += static_cast<uint32_t>(written);
    m_counters.payloadUartTxBytes += static_cast<uint32_t>(written);
  } else if (m_config.uartOutputFramed) {
    sendUartFrame(entry.channel, entry.payload, entry.length);
  } else {
    sendRawToUart(entry.payload, entry.length);
  }

  m_downlinkTail = static_cast<uint8_t>((m_downlinkTail + 1) % m_config.downlinkQueueDepth);
  m_downlinkCount = static_cast<uint8_t>(m_downlinkCount - 1);
}
