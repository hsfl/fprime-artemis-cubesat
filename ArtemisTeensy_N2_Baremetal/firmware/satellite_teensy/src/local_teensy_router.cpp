#include "local_teensy_router.hpp"

#include <string.h>

LocalTeensyRouter::LocalTeensyRouter(PduProxy& pduProxy,
                                     Rf23Driver& rfDriver,
                                     LinkCounters& counters,
                                     PayloadCache& payloadCache,
                                     LeptonPreview& leptonPreview)
    : m_pduProxy(pduProxy),
      m_rfDriver(rfDriver),
      m_counters(counters),
      m_payloadCache(payloadCache),
      m_leptonPreview(leptonPreview),
      m_rfResponseLen(0) {
  memset(m_rfResponse, 0, sizeof(m_rfResponse));
}

bool LocalTeensyRouter::beginLocalFrame(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length < LOCAL_HEADER_LEN) {
    prepareErrorResponse(0, link_protocol::TEENSY_STATUS_BAD_REQUEST);
    return true;
  }

  const uint8_t target = payload[0];
  const uint8_t requestId = payload[1];
  const uint8_t requestOperation =
      length > LOCAL_HEADER_LEN ? payload[LOCAL_HEADER_LEN] : 0U;
  if (target == link_protocol::TEENSY_TARGET_PDU) {
    return m_pduProxy.beginLocalFrame(payload, length);
  }
  if (target == link_protocol::TEENSY_TARGET_PAYLOAD_CACHE) {
    if (m_leptonPreview.isTransferActive()) {
      m_payloadCache.rejectBusy(requestId, requestOperation);
      return true;
    }
    return m_payloadCache.beginLocalFrame(payload, length);
  }
  if (target == link_protocol::TEENSY_TARGET_LEPTON_PREVIEW) {
    if (m_payloadCache.isTransferActive()) {
      m_leptonPreview.rejectBusy(requestId, requestOperation);
      return true;
    }
    return m_leptonPreview.beginLocalFrame(payload, length);
  }

  const uint8_t payloadLen = payload[2];
  if (target != link_protocol::TEENSY_TARGET_RF_STATUS ||
      payload[3] != 0U ||
      length != static_cast<uint16_t>(LOCAL_HEADER_LEN + payloadLen) ||
      payloadLen < 1U) {
    prepareErrorResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST);
    return true;
  }

  const uint8_t operation = payload[LOCAL_HEADER_LEN];
  if (operation == link_protocol::TEENSY_RF_OP_STATUS && payloadLen == 1U) {
    prepareRfStatusResponse(requestId);
    return true;
  }

  if (operation == link_protocol::TEENSY_RF_OP_SET_ENABLED && payloadLen == 2U &&
      payload[LOCAL_HEADER_LEN + 1U] <= 1U) {
    prepareRfSetEnabledResponse(requestId, payload[LOCAL_HEADER_LEN + 1U] != 0U);
    return true;
  }

  prepareErrorResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST);
  return true;
}

bool LocalTeensyRouter::pollLocalResponse(uint8_t* payload, uint16_t& length) {
  length = 0;
  if (m_rfResponseLen > 0) {
    if (payload == nullptr) {
      m_rfResponseLen = 0;
      return false;
    }
    memcpy(payload, m_rfResponse, m_rfResponseLen);
    length = m_rfResponseLen;
    m_rfResponseLen = 0;
    return true;
  }

  if (m_payloadCache.pollLocalResponse(payload, length)) {
    return true;
  }
  if (m_leptonPreview.pollLocalResponse(payload, length)) {
    return true;
  }
  return m_pduProxy.pollLocalResponse(payload, length);
}

void LocalTeensyRouter::prepareErrorResponse(uint8_t requestId, uint8_t status, uint8_t target) {
  m_rfResponse[0] = target;
  m_rfResponse[1] = requestId;
  m_rfResponse[2] = status;
  m_rfResponse[3] = 0;
  m_rfResponseLen = LOCAL_HEADER_LEN;
}

void LocalTeensyRouter::prepareRfStatusResponse(uint8_t requestId) {
  const artemis::rf23bp::LinkStats stats = m_rfDriver.linkStats();
  m_rfResponse[0] = link_protocol::TEENSY_TARGET_RF_STATUS;
  m_rfResponse[1] = requestId;
  m_rfResponse[2] = link_protocol::TEENSY_STATUS_OK;
  m_rfResponse[3] = RF_STATUS_PAYLOAD_LEN;
  m_rfResponse[4] = link_protocol::TEENSY_RF_OP_STATUS;
  writeLe16(&m_rfResponse[5], static_cast<uint16_t>(stats.last_rssi_dbm));
  writeLe16(&m_rfResponse[7], stats.rx_good);
  writeLe16(&m_rfResponse[9], stats.rx_bad);
  writeLe16(&m_rfResponse[11], stats.tx_good);
  writeLe32(&m_rfResponse[13], m_counters.rfRxPackets);
  writeLe32(&m_rfResponse[17], m_counters.rfTxPackets);
  writeLe32(&m_rfResponse[21], m_counters.rfTxDrops);
  m_rfResponse[25] = m_rfDriver.state();
  m_rfResponse[26] = m_rfDriver.fault();
  m_rfResponse[27] = m_rfDriver.bootFlags();
  m_rfResponse[28] = m_rfDriver.rssiValid() ? 1U : 0U;
  writeLe32(&m_rfResponse[29], m_rfDriver.lastAcceptedRssiAgeMs());
  writeLe32(&m_rfResponse[33], m_rfDriver.initAttempts());
  m_rfResponseLen = LOCAL_MAX_RESPONSE_LEN;
}

void LocalTeensyRouter::prepareRfSetEnabledResponse(uint8_t requestId, bool enabled) {
  const bool operationOk = m_rfDriver.setEnabled(enabled);
  m_rfResponse[0] = link_protocol::TEENSY_TARGET_RF_STATUS;
  m_rfResponse[1] = requestId;
  m_rfResponse[2] = operationOk ? link_protocol::TEENSY_STATUS_OK
                                : link_protocol::TEENSY_STATUS_TARGET_ERROR;
  m_rfResponse[3] = RF_SET_ENABLED_PAYLOAD_LEN;
  m_rfResponse[4] = link_protocol::TEENSY_RF_OP_SET_ENABLED;
  m_rfResponse[5] = enabled ? 1U : 0U;
  m_rfResponse[6] = m_rfDriver.state();
  m_rfResponse[7] = m_rfDriver.fault();
  m_rfResponseLen = LOCAL_HEADER_LEN + RF_SET_ENABLED_PAYLOAD_LEN;
}

void LocalTeensyRouter::writeLe16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFU);
  out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void LocalTeensyRouter::writeLe32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFFUL);
  out[1] = static_cast<uint8_t>((value >> 8UL) & 0xFFUL);
  out[2] = static_cast<uint8_t>((value >> 16UL) & 0xFFUL);
  out[3] = static_cast<uint8_t>((value >> 24UL) & 0xFFUL);
}
