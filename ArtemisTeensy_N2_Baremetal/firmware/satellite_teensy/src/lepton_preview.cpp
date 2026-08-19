#include "lepton_preview.hpp"

#include <string.h>

LeptonPreview::LeptonPreview(LinkCounters& counters)
    : m_state(link_protocol::LEPTON_PREVIEW_STATE_EMPTY),
      m_session(0U),
      m_frameSequence(0U),
      m_totalBytes(0U),
      m_receivedBytes(0U),
      m_expectedCrc(0U),
      m_width(0U),
      m_height(0U),
      m_pixelFormat(0U),
      m_fragmentCount(0U),
      m_nextFragment(0U),
      m_pending(false),
      m_anySendFailure(false),
      m_commitRequestId(0U),
      m_responseLen(0U),
      m_counters(counters) {
  memset(m_frame, 0, sizeof(m_frame));
  memset(m_response, 0, sizeof(m_response));
}

bool LeptonPreview::beginLocalFrame(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length < LOCAL_HEADER_LEN) {
    return false;
  }
  const uint8_t requestId = payload[1];
  const uint8_t bodyLen = payload[2];
  if (payload[0] != link_protocol::TEENSY_TARGET_LEPTON_PREVIEW || payload[3] != 0U ||
      length != static_cast<uint16_t>(LOCAL_HEADER_LEN + bodyLen) || bodyLen == 0U) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, 0U);
    return true;
  }

  const uint8_t* body = &payload[LOCAL_HEADER_LEN];
  switch (body[0]) {
    case link_protocol::LEPTON_PREVIEW_OP_BEGIN:
      handleBegin(requestId, body, bodyLen);
      break;
    case link_protocol::LEPTON_PREVIEW_OP_CHUNK:
      handleChunk(requestId, body, bodyLen);
      break;
    case link_protocol::LEPTON_PREVIEW_OP_COMMIT_AND_SEND:
      handleCommit(requestId, body, bodyLen);
      break;
    case link_protocol::LEPTON_PREVIEW_OP_ABORT:
      handleAbort(requestId, body, bodyLen);
      break;
    default:
      prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
      break;
  }
  return true;
}

bool LeptonPreview::pollLocalResponse(uint8_t* payload, uint16_t& length) {
  length = 0U;
  if (payload == nullptr || m_responseLen == 0U) {
    return false;
  }
  memcpy(payload, m_response, m_responseLen);
  length = m_responseLen;
  m_responseLen = 0U;
  return true;
}

bool LeptonPreview::isTransferActive() const {
  return m_state == link_protocol::LEPTON_PREVIEW_STATE_RECEIVING ||
         m_state == link_protocol::LEPTON_PREVIEW_STATE_SENDING;
}

void LeptonPreview::rejectBusy(uint8_t requestId, uint8_t operation) {
  // Match normal target-4 responses: [target, request, status, body_len=8,
  // op, session, state, received_bytes]. This must not fall back to the
  // router's four-byte RF-status error shape.
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, operation);
}

void LeptonPreview::handleBegin(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  // op, session, sequence, 80x60 U8 metadata, length, CRC16
  if (bodyLen != 14U) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  const uint16_t session = readLe16(&body[1]);
  const uint32_t frameSequence = readLe32(&body[3]);
  const uint8_t width = body[7];
  const uint8_t height = body[8];
  const uint8_t pixelFormat = body[9];
  const uint16_t totalBytes = readLe16(&body[10]);
  const uint16_t crc = readLe16(&body[12]);
  if (session == 0U || width != link_protocol::LEPTON_PREVIEW_WIDTH ||
      height != link_protocol::LEPTON_PREVIEW_HEIGHT ||
      pixelFormat != link_protocol::LEPTON_PREVIEW_PIXEL_FORMAT_U8 || totalBytes == 0U ||
      totalBytes > link_protocol::LEPTON_PREVIEW_MAX_FRAME_BYTES ||
      totalBytes != static_cast<uint16_t>(width) * height) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }

  if (m_state == link_protocol::LEPTON_PREVIEW_STATE_SENDING) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, body[0]);
    return;
  }
  if (m_state == link_protocol::LEPTON_PREVIEW_STATE_READY &&
      identityMatches(session, frameSequence, totalBytes, crc)) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
    return;
  }

  invalidate(link_protocol::LEPTON_PREVIEW_STATE_RECEIVING);
  m_session = session;
  m_frameSequence = frameSequence;
  m_totalBytes = totalBytes;
  m_expectedCrc = crc;
  m_width = width;
  m_height = height;
  m_pixelFormat = pixelFormat;
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
}

void LeptonPreview::handleChunk(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  if (bodyLen < 7U || readLe16(&body[1]) != m_session) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  const uint16_t offset = readLe16(&body[3]);
  const uint8_t chunkLen = body[5];
  if (chunkLen == 0U || chunkLen > link_protocol::LEPTON_PREVIEW_CHUNK_BYTES ||
      bodyLen != static_cast<uint8_t>(6U + chunkLen) ||
      static_cast<uint32_t>(offset) + chunkLen > m_totalBytes ||
      m_state != link_protocol::LEPTON_PREVIEW_STATE_RECEIVING) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  if (offset < m_receivedBytes) {
    if (static_cast<uint32_t>(offset) + chunkLen <= m_receivedBytes &&
        memcmp(&m_frame[offset], &body[6], chunkLen) == 0) {
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
  memcpy(&m_frame[offset], &body[6], chunkLen);
  m_receivedBytes = static_cast<uint16_t>(m_receivedBytes + chunkLen);
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
}

void LeptonPreview::handleCommit(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  if (bodyLen != 3U || readLe16(&body[1]) != m_session) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  if (m_state == link_protocol::LEPTON_PREVIEW_STATE_SENDING) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, body[0]);
    return;
  }
  if (m_state != link_protocol::LEPTON_PREVIEW_STATE_RECEIVING ||
      m_receivedBytes != m_totalBytes || crc16Ccitt(m_frame, m_totalBytes) != m_expectedCrc) {
    invalidate(link_protocol::LEPTON_PREVIEW_STATE_ERROR);
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_TARGET_ERROR, body[0]);
    return;
  }

  const uint16_t fragments = static_cast<uint16_t>(
      (m_totalBytes + PREVIEW_FRAGMENT_DATA_BYTES - 1U) / PREVIEW_FRAGMENT_DATA_BYTES);
  if (fragments == 0U || fragments > 200U) {
    invalidate(link_protocol::LEPTON_PREVIEW_STATE_ERROR);
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_TARGET_ERROR, body[0]);
    return;
  }
  m_fragmentCount = static_cast<uint8_t>(fragments);
  m_nextFragment = 0U;
  m_pending = false;
  m_anySendFailure = false;
  m_commitRequestId = requestId;
  m_state = link_protocol::LEPTON_PREVIEW_STATE_SENDING;
  m_counters.previewFramesCommitted += 1U;
  // Completion response follows the final one-shot send attempt.
}

void LeptonPreview::handleAbort(uint8_t requestId, const uint8_t* body, uint8_t bodyLen) {
  if (bodyLen != 3U || (m_session != 0U && readLe16(&body[1]) != m_session)) {
    prepareResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, body[0]);
    return;
  }
  invalidate(link_protocol::LEPTON_PREVIEW_STATE_EMPTY);
  prepareResponse(requestId, link_protocol::TEENSY_STATUS_OK, body[0]);
}

bool LeptonPreview::nextPreviewPacket(uint8_t* payload, uint16_t& length) {
  length = 0U;
  if (payload == nullptr || m_state != link_protocol::LEPTON_PREVIEW_STATE_SENDING || m_pending ||
      m_nextFragment >= m_fragmentCount) {
    return false;
  }
  const uint16_t offset = static_cast<uint16_t>(m_nextFragment) * PREVIEW_FRAGMENT_DATA_BYTES;
  const uint16_t remaining = static_cast<uint16_t>(m_totalBytes - offset);
  const uint8_t dataLength = static_cast<uint8_t>(
      remaining > PREVIEW_FRAGMENT_DATA_BYTES ? PREVIEW_FRAGMENT_DATA_BYTES : remaining);
  payload[0] = link_protocol::LEPTON_PREVIEW_WIRE_MAGIC_0;
  payload[1] = link_protocol::LEPTON_PREVIEW_WIRE_MAGIC_1;
  payload[2] = link_protocol::LEPTON_PREVIEW_WIRE_VERSION;
  payload[3] = link_protocol::LEPTON_PREVIEW_WIRE_TYPE_FRAGMENT;
  writeLe16(&payload[4], m_session);
  writeLe32(&payload[6], m_frameSequence);
  payload[10] = m_width;
  payload[11] = m_height;
  payload[12] = m_pixelFormat;
  payload[13] = m_nextFragment;
  payload[14] = m_fragmentCount;
  payload[15] = dataLength;
  writeLe16(&payload[16], m_totalBytes);
  writeLe16(&payload[18], m_expectedCrc);
  memcpy(&payload[PREVIEW_WIRE_HEADER_LEN], &m_frame[offset], dataLength);
  length = static_cast<uint16_t>(PREVIEW_WIRE_HEADER_LEN + dataLength);
  m_pending = true;
  return true;
}

void LeptonPreview::previewPacketAttempted(bool sent) {
  if (!m_pending) {
    return;
  }
  m_pending = false;
  m_counters.previewFragmentsAttempted += 1U;
  if (!sent) {
    m_anySendFailure = true;
    m_counters.previewFragmentFailures += 1U;
  }
  m_nextFragment = static_cast<uint8_t>(m_nextFragment + 1U);
  if (m_nextFragment >= m_fragmentCount) {
    m_state = link_protocol::LEPTON_PREVIEW_STATE_READY;
    prepareResponse(m_commitRequestId,
                    m_anySendFailure ? link_protocol::TEENSY_STATUS_TARGET_ERROR
                                     : link_protocol::TEENSY_STATUS_OK,
                    link_protocol::LEPTON_PREVIEW_OP_COMMIT_AND_SEND);
  }
}

void LeptonPreview::prepareResponse(uint8_t requestId, uint8_t status, uint8_t operation) {
  m_response[0] = link_protocol::TEENSY_TARGET_LEPTON_PREVIEW;
  m_response[1] = requestId;
  m_response[2] = status;
  m_response[3] = RESPONSE_BODY_LEN;
  m_response[4] = operation;
  writeLe16(&m_response[5], m_session);
  m_response[7] = m_state;
  writeLe32(&m_response[8], m_receivedBytes);
  m_responseLen = sizeof(m_response);
}

void LeptonPreview::invalidate(uint8_t state) {
  m_state = state;
  m_session = 0U;
  m_frameSequence = 0U;
  m_totalBytes = 0U;
  m_receivedBytes = 0U;
  m_expectedCrc = 0U;
  m_width = 0U;
  m_height = 0U;
  m_pixelFormat = 0U;
  m_fragmentCount = 0U;
  m_nextFragment = 0U;
  m_pending = false;
  m_anySendFailure = false;
  m_commitRequestId = 0U;
}

bool LeptonPreview::identityMatches(uint16_t session,
                                    uint32_t frameSequence,
                                    uint16_t totalBytes,
                                    uint16_t crc) const {
  return m_session == session && m_frameSequence == frameSequence &&
         m_totalBytes == totalBytes && m_expectedCrc == crc;
}

uint16_t LeptonPreview::crc16Ccitt(const uint8_t* data, uint16_t length) const {
  uint16_t crc = 0xFFFFU;
  for (uint16_t i = 0U; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8U;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      crc = (crc & 0x8000U) != 0U
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

uint16_t LeptonPreview::readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t LeptonPreview::readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

void LeptonPreview::writeLe16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void LeptonPreview::writeLe32(uint8_t* data, uint32_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFUL);
  data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFUL);
  data[2] = static_cast<uint8_t>((value >> 16U) & 0xFFUL);
  data[3] = static_cast<uint8_t>((value >> 24U) & 0xFFUL);
}
