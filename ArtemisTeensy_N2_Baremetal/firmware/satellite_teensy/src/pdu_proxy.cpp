#include "pdu_proxy.hpp"

#include <string.h>

PduProxy::PduProxy(HardwareSerial& pduUart)
    : m_pduUart(pduUart),
      m_state(State::IDLE),
      m_requestId(0),
      m_pduRxLen(0),
      m_expectedPduLen(0),
      m_deadlineMs(0),
      m_localResponseLen(0) {
  memset(m_pduRx, 0, sizeof(m_pduRx));
  memset(m_localResponse, 0, sizeof(m_localResponse));
}

void PduProxy::begin(uint32_t baud) {
  m_pduUart.begin(baud);
  resetPduRx();
  m_state = State::IDLE;
}

bool PduProxy::beginLocalFrame(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length < LOCAL_HEADER_LEN) {
    prepareLocalResponse(0, link_protocol::TEENSY_STATUS_BAD_REQUEST, nullptr, 0);
    return true;
  }

  const uint8_t target = payload[0];
  const uint8_t requestId = payload[1];
  const uint8_t payloadLen = payload[2];
  if (target != link_protocol::TEENSY_TARGET_PDU ||
      length < static_cast<uint16_t>(LOCAL_HEADER_LEN + payloadLen) ||
      payloadLen == 0 || payloadLen > PDU_V2_MAX_FRAME_LEN) {
    prepareLocalResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST, nullptr, 0);
    return true;
  }

  if (m_state == State::WAITING_FOR_PDU) {
    prepareLocalResponse(requestId, link_protocol::TEENSY_STATUS_BUSY, nullptr, 0);
    return true;
  }

  resetPduRx();
  m_requestId = requestId;
  while (m_pduUart.available() > 0) {
    (void)m_pduUart.read();
  }
  m_pduUart.write(&payload[LOCAL_HEADER_LEN], payloadLen);
  m_pduUart.flush();
  m_deadlineMs = millis() + PDU_RESPONSE_TIMEOUT_MS;
  m_state = State::WAITING_FOR_PDU;
  return true;
}

bool PduProxy::pollLocalResponse(uint8_t* payload, uint16_t& length) {
  length = 0;
  if (m_state == State::WAITING_FOR_PDU) {
    pollPduUart();
    if (m_state == State::WAITING_FOR_PDU &&
        static_cast<int32_t>(millis() - m_deadlineMs) >= 0) {
      prepareLocalResponse(m_requestId, link_protocol::TEENSY_STATUS_TIMEOUT, nullptr, 0);
    }
  }

  if (m_state != State::RESPONSE_READY) {
    return false;
  }
  if (payload == nullptr || m_localResponseLen == 0) {
    m_state = State::IDLE;
    return false;
  }
  memcpy(payload, m_localResponse, m_localResponseLen);
  length = m_localResponseLen;
  m_localResponseLen = 0;
  m_state = State::IDLE;
  return true;
}

void PduProxy::prepareLocalResponse(uint8_t requestId, uint8_t status, const uint8_t* pduFrame, uint16_t pduFrameLen) {
  if (pduFrameLen > PDU_V2_MAX_FRAME_LEN) {
    pduFrameLen = 0;
    status = link_protocol::TEENSY_STATUS_TARGET_ERROR;
  }
  m_localResponse[0] = link_protocol::TEENSY_TARGET_PDU;
  m_localResponse[1] = requestId;
  m_localResponse[2] = status;
  m_localResponse[3] = static_cast<uint8_t>(pduFrameLen);
  if (pduFrame != nullptr && pduFrameLen > 0) {
    memcpy(&m_localResponse[LOCAL_HEADER_LEN], pduFrame, pduFrameLen);
  }
  m_localResponseLen = static_cast<uint16_t>(LOCAL_HEADER_LEN + pduFrameLen);
  m_state = State::RESPONSE_READY;
}

void PduProxy::pollPduUart() {
  while (m_pduUart.available() > 0 && m_state == State::WAITING_FOR_PDU) {
    const uint8_t value = static_cast<uint8_t>(m_pduUart.read());
    if (m_pduRxLen == 0 && value != PDU_V2_SOF) {
      continue;
    }
    if (m_pduRxLen >= PDU_V2_MAX_FRAME_LEN) {
      prepareLocalResponse(m_requestId, link_protocol::TEENSY_STATUS_TARGET_ERROR, nullptr, 0);
      return;
    }
    m_pduRx[m_pduRxLen++] = value;
    if (m_pduRxLen == PDU_V2_HEADER_LEN) {
      const uint8_t payloadLen = m_pduRx[6];
      if (payloadLen > PDU_V2_MAX_PAYLOAD_LEN) {
        prepareLocalResponse(m_requestId, link_protocol::TEENSY_STATUS_TARGET_ERROR, nullptr, 0);
        return;
      }
      m_expectedPduLen = static_cast<uint16_t>(PDU_V2_HEADER_LEN + payloadLen + PDU_V2_CRC_LEN);
    }
    if (m_expectedPduLen > 0 && m_pduRxLen == m_expectedPduLen) {
      prepareLocalResponse(m_requestId, link_protocol::TEENSY_STATUS_OK, m_pduRx, m_pduRxLen);
      return;
    }
  }
}

void PduProxy::resetPduRx() {
  m_pduRxLen = 0;
  m_expectedPduLen = 0;
  memset(m_pduRx, 0, sizeof(m_pduRx));
}
