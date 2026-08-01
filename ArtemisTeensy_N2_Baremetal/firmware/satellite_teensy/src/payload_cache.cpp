#include "payload_cache.hpp"

#include <string.h>

PayloadCache::PayloadCache()
    : m_state(link_protocol::PAYLOAD_CACHE_STATE_EMPTY),
      m_transferId(0),
      m_productId(0),
      m_totalBytes(0),
      m_receivedBytes(0),
      m_expectedCrc(0),
      m_valid(false),
      m_txPhase(TX_IDLE),
      m_headersRemaining(0),
      m_nextPacketIndex(0),
      m_totalPackets(0),
      m_retryCount(0),
      m_retryCursor(0),
      m_packetPending(false),
      m_commitRequestId(0),
      m_responseLen(0) {
  memset(m_cache, 0, sizeof(m_cache));
  memset(m_retryPackets, 0, sizeof(m_retryPackets));
  memset(m_response, 0, sizeof(m_response));
}

bool PayloadCache::beginLocalFrame(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length < LOCAL_HEADER_LEN) {
    return false;
  }
  const uint8_t requestId = payload[1];
  const uint8_t bodyLen = payload[2];
  if (payload[0] != link_protocol::TEENSY_TARGET_PAYLOAD_CACHE || payload[3] != 0U ||
      length != static_cast<uint16_t>(LOCAL_HEADER_LEN + bodyLen) || bodyLen == 0U) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, 0U);
    return true;
  }

  const uint8_t* body = &payload[LOCAL_HEADER_LEN];
  switch (body[0]) {
    case link_protocol::PAYLOAD_CACHE_OP_BEGIN:
      handleBegin(requestId, body, bodyLen);
      break;
    case link_protocol::PAYLOAD_CACHE_OP_CHUNK:
      handleChunk(requestId, body, bodyLen);
      break;
    case link_protocol::PAYLOAD_CACHE_OP_COMMIT_AND_SEND:
      handleCommit(requestId, body, bodyLen);
      break;
    case link_protocol::PAYLOAD_CACHE_OP_ABORT:
      handleAbort(requestId, body, bodyLen);
      break;
    default:
      prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
      break;
  }
  return true;
}

bool PayloadCache::pollLocalResponse(uint8_t* payload, uint16_t& length) {
  length = 0;
  if (m_responseLen == 0U || payload == nullptr) {
    return false;
  }
  memcpy(payload, m_response, m_responseLen);
  length = m_responseLen;
  m_responseLen = 0;
  return true;
}

bool PayloadCache::isTransferActive() const {
  return m_state == link_protocol::PAYLOAD_CACHE_STATE_RECEIVING ||
         m_state == link_protocol::PAYLOAD_CACHE_STATE_SENDING;
}

void PayloadCache::rejectBusy(uint8_t requestId, uint8_t operation) {
  // Keep the normal target-specific response shape so the Pi can correlate a
  // rejected request exactly like every other payload-cache response.
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, operation);
}

void PayloadCache::handleBegin(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  if (bodyLen != 12U) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  const uint8_t transferId = body[1];
  const uint32_t productId = readLe32(&body[2]);
  const uint32_t totalBytes = readLe32(&body[6]);
  const uint16_t crc = readLe16(&body[10]);
  if (transferId == 0U || totalBytes == 0U || totalBytes > link_protocol::PAYLOAD_CACHE_MAX_BYTES) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  if (m_state == link_protocol::PAYLOAD_CACHE_STATE_SENDING) {
    if (!identityMatches(transferId, productId, totalBytes, crc)) {
      prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, body[0]);
      return;
    }
    // A lost UART response can repeat BEGIN after RF transmission starts.
    // Acknowledge the same transaction without rewinding its packet sequencer.
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
    return;
  }
  if (m_valid && identityMatches(transferId, productId, totalBytes, crc)) {
    m_state = link_protocol::PAYLOAD_CACHE_STATE_READY;
    m_receivedBytes = m_totalBytes;
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
    return;
  }

  invalidate(link_protocol::PAYLOAD_CACHE_STATE_RECEIVING);
  m_transferId = transferId;
  m_productId = productId;
  m_totalBytes = totalBytes;
  m_expectedCrc = crc;
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
}

void PayloadCache::handleChunk(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  if (bodyLen < 8U || body[1] != m_transferId) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  const uint32_t offset = readLe32(&body[2]);
  const uint8_t chunkLen = body[6];
  if (chunkLen == 0U || bodyLen != static_cast<uint8_t>(7U + chunkLen) ||
      chunkLen > link_protocol::PAYLOAD_CACHE_CHUNK_BYTES ||
      offset + chunkLen > m_totalBytes) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  if (m_state != link_protocol::PAYLOAD_CACHE_STATE_RECEIVING) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, body[0]);
    return;
  }
  if (offset < m_receivedBytes) {
    if (offset + chunkLen <= m_receivedBytes &&
        memcmp(&m_cache[offset], &body[7], chunkLen) == 0) {
      prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
    } else {
      prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    }
    return;
  }
  if (offset != m_receivedBytes) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  memcpy(&m_cache[offset], &body[7], chunkLen);
  m_receivedBytes += chunkLen;
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
}

void PayloadCache::handleCommit(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  if (bodyLen != 2U || body[1] != m_transferId) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  if (!m_valid) {
    if (m_state != link_protocol::PAYLOAD_CACHE_STATE_RECEIVING || m_receivedBytes != m_totalBytes ||
        crc16Ccitt(m_cache, m_totalBytes) != m_expectedCrc) {
      invalidate(link_protocol::PAYLOAD_CACHE_STATE_ERROR);
      prepareResponse(requestId, link_protocol::TEENSY_STATUS_TARGET_ERROR, body[0]);
      return;
    }
    m_valid = true;
  }
  startTransmit(requestId);
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
}

void PayloadCache::handleAbort(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  if (bodyLen != 2U || (m_transferId != 0U && body[1] != m_transferId)) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  invalidate(link_protocol::PAYLOAD_CACHE_STATE_EMPTY);
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
}

void PayloadCache::prepareResponse(uint8_t requestId, uint8_t status, uint8_t operation) {
  m_response[0] = link_protocol::TEENSY_TARGET_PAYLOAD_CACHE;
  m_response[1] = requestId;
  m_response[2] = status;
  m_response[3] = RESPONSE_BODY_LEN;
  m_response[4] = operation;
  m_response[5] = m_transferId;
  m_response[6] = m_state;
  m_response[7] = 0U;
  writeLe32(&m_response[8], m_receivedBytes);
  m_responseLen = sizeof(m_response);
}

void PayloadCache::startTransmit(uint8_t requestId) {
  m_state = link_protocol::PAYLOAD_CACHE_STATE_SENDING;
  m_txPhase = TX_HEADERS;
  m_headersRemaining = 3U;
  m_nextPacketIndex = 0U;
  m_totalPackets = static_cast<uint16_t>(
      (m_totalBytes + link_protocol::PAYLOAD_PACKET_DATA_BYTES - 1U) /
      link_protocol::PAYLOAD_PACKET_DATA_BYTES);
  m_retryCount = 0U;
  m_retryCursor = 0U;
  m_packetPending = false;
  m_commitRequestId = requestId;
}

bool PayloadCache::handlePayloadControl(const uint8_t* payload, uint16_t length) {
  if (!m_valid || payload == nullptr || length < 7U ||
      payload[0] != link_protocol::PAYLOAD_MAGIC_0 ||
      payload[1] != link_protocol::PAYLOAD_MAGIC_1 ||
      payload[2] != PACKET_RETRY_REQUEST || payload[3] != m_transferId) {
    return false;
  }
  const uint16_t startIndex = readLe16(&payload[4]);
  const uint8_t bitmapBytes = payload[6];
  if (bitmapBytes == 0U || bitmapBytes > 36U ||
      length != static_cast<uint16_t>(7U + bitmapBytes)) {
    return false;
  }

  m_retryCount = 0U;
  m_retryCursor = 0U;
  for (uint8_t byteIndex = 0; byteIndex < bitmapBytes; ++byteIndex) {
    const uint8_t bits = payload[7U + byteIndex];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((bits & (1U << bit)) == 0U) {
        continue;
      }
      const uint32_t packetIndex =
          static_cast<uint32_t>(startIndex) + static_cast<uint32_t>(byteIndex) * 8U + bit;
      if (packetIndex < m_totalPackets && m_retryCount < MAX_RETRY_PACKETS) {
        m_retryPackets[m_retryCount++] = static_cast<uint16_t>(packetIndex);
      }
    }
  }
  if (m_retryCount > 0U) {
    m_state = link_protocol::PAYLOAD_CACHE_STATE_SENDING;
    m_txPhase = TX_REPAIR;
    m_packetPending = false;
  }
  return true;
}

bool PayloadCache::nextPayloadPacket(uint8_t* payload, uint16_t& length) {
  length = 0U;
  if (!m_valid || m_state != link_protocol::PAYLOAD_CACHE_STATE_SENDING ||
      m_txPhase == TX_IDLE || payload == nullptr) {
    return false;
  }
  memset(payload, 0, link_protocol::RF_SEGMENT_MAX_DATA);
  payload[0] = link_protocol::PAYLOAD_MAGIC_0;
  payload[1] = link_protocol::PAYLOAD_MAGIC_1;
  payload[3] = m_transferId;

  if (m_txPhase == TX_HEADERS) {
    payload[2] = PACKET_HEADER;
    writeLe32(&payload[4], m_productId);
    writeLe32(&payload[8], m_totalBytes);
    writeLe16(&payload[12], m_totalPackets);
    payload[14] = link_protocol::PAYLOAD_PACKET_DATA_BYTES;
    writeLe16(&payload[15], m_expectedCrc);
    length = 17U;
  } else if (m_txPhase == TX_DATA || m_txPhase == TX_REPAIR) {
    const uint16_t packetIndex =
        (m_txPhase == TX_DATA) ? m_nextPacketIndex : m_retryPackets[m_retryCursor];
    const uint32_t offset = static_cast<uint32_t>(packetIndex) *
                            link_protocol::PAYLOAD_PACKET_DATA_BYTES;
    uint32_t dataLen = m_totalBytes - offset;
    if (dataLen > link_protocol::PAYLOAD_PACKET_DATA_BYTES) {
      dataLen = link_protocol::PAYLOAD_PACKET_DATA_BYTES;
    }
    payload[2] = PACKET_DATA;
    writeLe16(&payload[4], packetIndex);
    payload[6] = static_cast<uint8_t>(dataLen);
    memcpy(&payload[7], &m_cache[offset], dataLen);
    const uint16_t crcOffset = static_cast<uint16_t>(7U + dataLen);
    writeLe16(&payload[crcOffset], crc16Ccitt(payload, crcOffset));
    length = static_cast<uint16_t>(crcOffset + 2U);
  } else if (m_txPhase == TX_END) {
    payload[2] = PACKET_END;
    writeLe16(&payload[4], m_totalPackets);
    writeLe16(&payload[6], m_expectedCrc);
    length = 8U;
  }
  m_packetPending = length > 0U;
  return m_packetPending;
}

void PayloadCache::payloadPacketSent(bool sent) {
  if (!m_packetPending || !sent) {
    return;
  }
  m_packetPending = false;
  if (m_txPhase == TX_HEADERS) {
    if (--m_headersRemaining == 0U) {
      m_txPhase = TX_DATA;
    }
  } else if (m_txPhase == TX_DATA) {
    if (++m_nextPacketIndex >= m_totalPackets) {
      m_txPhase = TX_END;
    }
  } else if (m_txPhase == TX_REPAIR) {
    if (++m_retryCursor >= m_retryCount) {
      m_txPhase = TX_END;
    }
  } else if (m_txPhase == TX_END) {
    m_txPhase = TX_IDLE;
    m_state = link_protocol::PAYLOAD_CACHE_STATE_READY;
    prepareResponse(m_commitRequestId,
                    link_protocol::TEENSY_STATUS_OK,
                    link_protocol::PAYLOAD_CACHE_OP_COMMIT_AND_SEND);
  }
}

void PayloadCache::invalidate(uint8_t state) {
  m_state = state;
  m_transferId = 0U;
  m_productId = 0U;
  m_totalBytes = 0U;
  m_receivedBytes = 0U;
  m_expectedCrc = 0U;
  m_valid = false;
  m_txPhase = TX_IDLE;
  m_headersRemaining = 0U;
  m_nextPacketIndex = 0U;
  m_totalPackets = 0U;
  m_retryCount = 0U;
  m_retryCursor = 0U;
  m_packetPending = false;
  m_commitRequestId = 0U;
}

bool PayloadCache::identityMatches(uint8_t transferId,
                                   uint32_t productId,
                                   uint32_t totalBytes,
                                   uint16_t crc) const {
  return m_transferId == transferId && m_productId == productId &&
         m_totalBytes == totalBytes && m_expectedCrc == crc;
}

uint16_t PayloadCache::crc16Ccitt(const uint8_t* data, uint32_t length) const {
  uint16_t crc = 0xFFFFU;
  for (uint32_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      crc = (crc & 0x8000U) != 0U
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

uint16_t PayloadCache::readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t PayloadCache::readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

void PayloadCache::writeLe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void PayloadCache::writeLe32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFUL);
  data[1] = static_cast<uint8_t>((value >> 8UL) & 0xFFUL);
  data[2] = static_cast<uint8_t>((value >> 16UL) & 0xFFUL);
  data[3] = static_cast<uint8_t>((value >> 24UL) & 0xFFUL);
}
