#include "local_teensy_router.hpp"

#include <string.h>

LocalTeensyRouter::LocalTeensyRouter(PduProxy& pduProxy, Rf23Driver& rfDriver, LinkCounters& counters)
    : m_pduProxy(pduProxy),
      m_rfDriver(rfDriver),
      m_counters(counters),
      m_rfResponseLen(0) {
  memset(m_rfResponse, 0, sizeof(m_rfResponse));
}

bool LocalTeensyRouter::beginLocalFrame(const uint8_t* payload, uint16_t length) {
  if (payload == nullptr || length < LOCAL_HEADER_LEN) {
    prepareRfStatusResponse(0, link_protocol::TEENSY_STATUS_BAD_REQUEST);
    return true;
  }

  const uint8_t target = payload[0];
  if (target == link_protocol::TEENSY_TARGET_PDU) {
    return m_pduProxy.beginLocalFrame(payload, length);
  }

  const uint8_t requestId = payload[1];
  const uint8_t payloadLen = payload[2];
  if (target != link_protocol::TEENSY_TARGET_RF_STATUS ||
      length < static_cast<uint16_t>(LOCAL_HEADER_LEN + payloadLen) ||
      payloadLen < 1U ||
      payload[LOCAL_HEADER_LEN] != link_protocol::TEENSY_RF_OP_LINK_STATS) {
    prepareRfStatusResponse(requestId, link_protocol::TEENSY_STATUS_BAD_REQUEST);
    return true;
  }

  prepareRfStatusResponse(requestId, link_protocol::TEENSY_STATUS_OK);
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

  return m_pduProxy.pollLocalResponse(payload, length);
}

void LocalTeensyRouter::prepareRfStatusResponse(uint8_t requestId, uint8_t status) {
  m_rfResponse[0] = link_protocol::TEENSY_TARGET_RF_STATUS;
  m_rfResponse[1] = requestId;
  m_rfResponse[2] = status;
  m_rfResponse[3] = 0;

  if (status == link_protocol::TEENSY_STATUS_OK) {
    const artemis::rf23bp::LinkStats stats = m_rfDriver.linkStats();
    m_rfResponse[3] = RF_STATS_PAYLOAD_LEN;
    m_rfResponse[4] = link_protocol::TEENSY_RF_OP_LINK_STATS;
    writeLe16(&m_rfResponse[5], static_cast<uint16_t>(stats.last_rssi_dbm));
    writeLe16(&m_rfResponse[7], stats.rx_good);
    writeLe16(&m_rfResponse[9], stats.rx_bad);
    writeLe16(&m_rfResponse[11], stats.tx_good);
    writeLe32(&m_rfResponse[13], m_counters.rfRxPackets);
    writeLe32(&m_rfResponse[17], m_counters.rfTxPackets);
    writeLe32(&m_rfResponse[21], m_counters.rfTxDrops);
  }

  m_rfResponseLen = static_cast<uint16_t>(LOCAL_HEADER_LEN + m_rfResponse[3]);
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
