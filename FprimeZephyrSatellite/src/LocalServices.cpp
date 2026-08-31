#include "SatelliteController/LocalServices.hpp"

#include <algorithm>
#include <cstring>

namespace SatelliteController {

namespace {
constexpr std::uint8_t PACKET_RETRY_REQUEST = LocalProtocol::PAYLOAD_PACKET_RETRY_REQUEST;
constexpr std::size_t PREVIEW_WIRE_HEADER_LEN = 20;
constexpr std::size_t PREVIEW_FRAGMENT_DATA_BYTES = Generated::RF_SEGMENT_MAX_DATA - PREVIEW_WIRE_HEADER_LEN;

bool elapsed(std::uint32_t now, std::uint32_t deadline) {
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

void write16(std::uint8_t* data, std::uint16_t value) {
    data[0] = static_cast<std::uint8_t>(value & 0xFFU);
    data[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value & 0xFFU);
    data[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    data[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    data[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::uint16_t read16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t read32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

}  // namespace

PduProxy::PduProxy(PduTransport& transport) : m_transport(transport) {}

void PduProxy::resetRx() {
    m_pduRxLen = 0;
    m_expectedPduLen = 0;
}

bool PduProxy::hasPending() const {
    return m_state != State::IDLE || m_responseLen != 0 || m_busyResponseLen != 0;
}

void PduProxy::prepareResponse(std::uint8_t requestId,
                               std::uint8_t status,
                               const std::uint8_t* pduFrame,
                               std::size_t pduFrameLen) {
    if (pduFrameLen > LocalProtocol::PDU_V2_MAX_FRAME_LEN) {
        pduFrameLen = 0;
        status = LocalProtocol::STATUS_TARGET_ERROR;
    }
    m_response[0] = LocalProtocol::TARGET_PDU;
    m_response[1] = requestId;
    m_response[2] = status;
    m_response[3] = static_cast<std::uint8_t>(pduFrameLen);
    if (pduFrame != nullptr && pduFrameLen != 0) {
        std::memcpy(&m_response[LocalProtocol::HEADER_LEN], pduFrame, pduFrameLen);
    }
    m_responseLen = LocalProtocol::HEADER_LEN + pduFrameLen;
    m_state = State::RESPONSE_READY;
}

bool PduProxy::beginLocalFrame(const std::uint8_t* payload, std::size_t length) {
    return beginLocalFrame(payload, length, 0);
}

bool PduProxy::beginLocalFrame(const std::uint8_t* payload, std::size_t length, std::uint32_t nowMs) {
    if (m_state == State::WAITING_FOR_PDU) {
        // Serialization wins over validation: no second request, malformed or
        // otherwise, may replace the transaction whose 350 ms deadline is
        // already running.
        const std::uint8_t requestId = payload != nullptr && length > 1 ? payload[1] : 0;
        m_busyResponse[0] = LocalProtocol::TARGET_PDU;
        m_busyResponse[1] = requestId;
        m_busyResponse[2] = LocalProtocol::STATUS_BUSY;
        m_busyResponse[3] = 0;
        m_busyResponseLen = LocalProtocol::HEADER_LEN;
        return true;
    }
    if (payload == nullptr || length < LocalProtocol::HEADER_LEN) {
        prepareResponse(0, LocalProtocol::STATUS_BAD_REQUEST);
        return true;
    }
    const std::uint8_t requestId = payload[1];
    const std::uint8_t payloadLen = payload[2];
    if (payload[0] != LocalProtocol::TARGET_PDU || payload[3] != 0 || payloadLen == 0 ||
        payloadLen > LocalProtocol::PDU_V2_MAX_FRAME_LEN ||
        length != static_cast<std::size_t>(LocalProtocol::HEADER_LEN) + payloadLen) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST);
        return true;
    }
    m_transport.discardRx();
    resetRx();
    m_requestId = requestId;
    m_deadlineStarted = false;
    if (!m_transport.write(&payload[LocalProtocol::HEADER_LEN], payloadLen)) {
        prepareResponse(requestId, LocalProtocol::STATUS_TARGET_ERROR);
        return true;
    }
    m_transport.flush();
    m_state = State::WAITING_FOR_PDU;
    m_deadlineMs = nowMs + LocalProtocol::PDU_RESPONSE_TIMEOUT_MS;
    m_deadlineStarted = true;
    return true;
}

void PduProxy::pollPduUart() {
    std::uint8_t value = 0;
    while (m_state == State::WAITING_FOR_PDU && m_transport.readByte(value)) {
        if (m_pduRxLen == 0 && value != 0xA5U) continue;
        if (m_pduRxLen >= LocalProtocol::PDU_V2_MAX_FRAME_LEN) {
            prepareResponse(m_requestId, LocalProtocol::STATUS_TARGET_ERROR);
            return;
        }
        m_pduRx[m_pduRxLen++] = value;
        if (m_pduRxLen == LocalProtocol::PDU_V2_HEADER_LEN) {
            const std::uint8_t pduPayloadLen = m_pduRx[6];
            if (pduPayloadLen > LocalProtocol::PDU_V2_MAX_PAYLOAD_LEN) {
                prepareResponse(m_requestId, LocalProtocol::STATUS_TARGET_ERROR);
                return;
            }
            m_expectedPduLen = LocalProtocol::PDU_V2_HEADER_LEN + pduPayloadLen + LocalProtocol::PDU_V2_CRC_LEN;
        }
        if (m_expectedPduLen != 0 && m_pduRxLen == m_expectedPduLen) {
            prepareResponse(m_requestId, LocalProtocol::STATUS_OK, m_pduRx, m_pduRxLen);
            return;
        }
    }
}

bool PduProxy::copyResponse(std::uint8_t* payload, std::size_t capacity, std::size_t& length) {
    const std::uint8_t* source = nullptr;
    std::size_t sourceLen = 0;
    bool busy = false;
    if (m_busyResponseLen != 0) {
        source = m_busyResponse;
        sourceLen = m_busyResponseLen;
        busy = true;
    } else if (m_state == State::RESPONSE_READY) {
        source = m_response;
        sourceLen = m_responseLen;
    }
    if (source == nullptr) return false;
    if (payload == nullptr || capacity < sourceLen) return false;
    std::memcpy(payload, source, sourceLen);
    length = sourceLen;
    if (busy) {
        m_busyResponseLen = 0;
    } else {
        m_responseLen = 0;
        m_state = State::IDLE;
    }
    return true;
}

bool PduProxy::pollLocalResponse(std::uint32_t nowMs,
                                 std::uint8_t* payload,
                                 std::size_t capacity,
                                 std::size_t& length) {
    length = 0;
    if (m_state == State::WAITING_FOR_PDU) {
        // The no-clock overload starts at zero. A real Zephyr caller should
        // use the timestamped overload so the timeout includes queue latency.
        if (!m_deadlineStarted) {
            m_deadlineMs = nowMs + LocalProtocol::PDU_RESPONSE_TIMEOUT_MS;
            m_deadlineStarted = true;
        }
        pollPduUart();
        if (m_state == State::WAITING_FOR_PDU && elapsed(nowMs, m_deadlineMs)) {
            prepareResponse(m_requestId, LocalProtocol::STATUS_TIMEOUT);
        }
    }
    return copyResponse(payload, capacity, length);
}

PayloadCacheService::PayloadCacheService() = default;

void PayloadCacheService::writeLe16(std::uint8_t* data, std::uint16_t value) { write16(data, value); }
void PayloadCacheService::writeLe32(std::uint8_t* data, std::uint32_t value) { write32(data, value); }
std::uint16_t PayloadCacheService::readLe16(const std::uint8_t* data) { return read16(data); }
std::uint32_t PayloadCacheService::readLe32(const std::uint8_t* data) { return read32(data); }

std::uint16_t PayloadCacheService::cacheCrc() const {
    return crc16Ccitt(m_cache, m_totalBytes);
}

bool PayloadCacheService::identityMatches(std::uint8_t transferId,
                                          std::uint32_t productId,
                                          std::uint32_t totalBytes,
                                          std::uint16_t crc) const {
    return m_transferId == transferId && m_productId == productId && m_totalBytes == totalBytes &&
           m_expectedCrc == crc;
}

void PayloadCacheService::prepareResponse(std::uint8_t requestId,
                                          std::uint8_t status,
                                          std::uint8_t operation) {
    m_response[0] = LocalProtocol::TARGET_PAYLOAD_CACHE;
    m_response[1] = requestId;
    m_response[2] = status;
    m_response[3] = 8;
    m_response[4] = operation;
    m_response[5] = m_transferId;
    m_response[6] = m_state;
    m_response[7] = 0;
    writeLe32(&m_response[8], m_receivedBytes);
    m_responseLen = sizeof(m_response);
}

bool PayloadCacheService::beginLocalFrame(const std::uint8_t* payload, std::size_t length) {
    if (payload == nullptr || length < LocalProtocol::HEADER_LEN) return false;
    const std::uint8_t requestId = payload[1];
    const std::size_t bodyLen = payload[2];
    if (payload[0] != LocalProtocol::TARGET_PAYLOAD_CACHE || payload[3] != 0 || bodyLen == 0 ||
        length != LocalProtocol::HEADER_LEN + bodyLen) {
        prepareResponse(requestId,
                        LocalProtocol::STATUS_BAD_REQUEST,
                        length > LocalProtocol::HEADER_LEN ? payload[4] : 0);
        return true;
    }
    const std::uint8_t* body = &payload[LocalProtocol::HEADER_LEN];
    switch (body[0]) {
        case LocalProtocol::CACHE_OP_BEGIN: handleBegin(requestId, body, bodyLen); break;
        case LocalProtocol::CACHE_OP_CHUNK: handleChunk(requestId, body, bodyLen); break;
        case LocalProtocol::CACHE_OP_COMMIT_AND_SEND: handleCommit(requestId, body, bodyLen); break;
        case LocalProtocol::CACHE_OP_ABORT: handleAbort(requestId, body, bodyLen); break;
        default: prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]); break;
    }
    return true;
}

bool PayloadCacheService::pollLocalResponse(std::uint8_t* payload, std::size_t capacity, std::size_t& length) {
    length = 0;
    if (m_responseLen == 0 || payload == nullptr || capacity < m_responseLen) return false;
    std::memcpy(payload, m_response, m_responseLen);
    length = m_responseLen;
    m_responseLen = 0;
    return true;
}

bool PayloadCacheService::isTransferActive() const {
    return m_state == LocalProtocol::CACHE_STATE_RECEIVING || m_state == LocalProtocol::CACHE_STATE_SENDING;
}

void PayloadCacheService::rejectBusy(std::uint8_t requestId, std::uint8_t operation) {
    prepareResponse(requestId, LocalProtocol::STATUS_BUSY, operation);
}

void PayloadCacheService::handleBegin(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen != 12) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    const std::uint8_t transferId = body[1];
    const std::uint32_t productId = readLe32(&body[2]);
    const std::uint32_t totalBytes = readLe32(&body[6]);
    const std::uint16_t crc = readLe16(&body[10]);
    if (transferId == 0 || totalBytes == 0 || totalBytes > LocalProtocol::PAYLOAD_CACHE_MAX_BYTES) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    if (m_state == LocalProtocol::CACHE_STATE_SENDING) {
        prepareResponse(requestId,
                        identityMatches(transferId, productId, totalBytes, crc) ? LocalProtocol::STATUS_OK
                                                                                 : LocalProtocol::STATUS_BUSY,
                        body[0]);
        return;
    }
    if (m_valid && identityMatches(transferId, productId, totalBytes, crc)) {
        m_state = LocalProtocol::CACHE_STATE_READY;
        m_receivedBytes = m_totalBytes;
        prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
        return;
    }
    invalidate(LocalProtocol::CACHE_STATE_RECEIVING);
    m_transferId = transferId;
    m_productId = productId;
    m_totalBytes = totalBytes;
    m_expectedCrc = crc;
    prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
}

void PayloadCacheService::handleChunk(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen < 8 || body[1] != m_transferId) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    const std::uint32_t offset = readLe32(&body[2]);
    const std::size_t chunkLen = body[6];
    // Subtraction form prevents uint32_t wraparound in offset + chunkLen.
    if (chunkLen == 0 || chunkLen > LocalProtocol::PAYLOAD_CACHE_CHUNK_BYTES ||
        bodyLen != 7 + chunkLen || offset > m_totalBytes || chunkLen > m_totalBytes - offset) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    if (m_state != LocalProtocol::CACHE_STATE_RECEIVING) {
        prepareResponse(requestId, LocalProtocol::STATUS_BUSY, body[0]);
        return;
    }
    if (offset < m_receivedBytes) {
        if (chunkLen <= m_receivedBytes - offset && std::memcmp(&m_cache[offset], &body[7], chunkLen) == 0) {
            prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
        } else {
            prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        }
        return;
    }
    if (offset != m_receivedBytes) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    std::memcpy(&m_cache[offset], &body[7], chunkLen);
    m_receivedBytes += static_cast<std::uint32_t>(chunkLen);
    prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
}

void PayloadCacheService::handleCommit(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen != 2 || body[1] != m_transferId) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    if (!m_valid) {
        if (m_state != LocalProtocol::CACHE_STATE_RECEIVING || m_receivedBytes != m_totalBytes ||
            cacheCrc() != m_expectedCrc) {
            invalidate(LocalProtocol::CACHE_STATE_ERROR);
            prepareResponse(requestId, LocalProtocol::STATUS_TARGET_ERROR, body[0]);
            return;
        }
        m_valid = true;
    }
    startTransmit(requestId);
    prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
}

void PayloadCacheService::handleAbort(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen != 2 || (m_transferId != 0 && body[1] != m_transferId)) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    invalidate(LocalProtocol::CACHE_STATE_EMPTY);
    prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
}

void PayloadCacheService::startTransmit(std::uint8_t requestId) {
    m_state = LocalProtocol::CACHE_STATE_SENDING;
    m_txPhase = TxPhase::HEADERS;
    m_headersRemaining = 3;
    m_nextPacketIndex = 0;
    m_totalPackets = static_cast<std::uint16_t>((m_totalBytes + LocalProtocol::PAYLOAD_PACKET_DATA_BYTES - 1U) /
                                                LocalProtocol::PAYLOAD_PACKET_DATA_BYTES);
    m_retryCount = 0;
    m_retryCursor = 0;
    m_packetPending = false;
    m_commitRequestId = requestId;
}

bool PayloadCacheService::handlePayloadControl(const std::uint8_t* payload, std::size_t length) {
    if (!m_valid || payload == nullptr || length < 7 || payload[0] != LocalProtocol::PAYLOAD_MAGIC_0 ||
        payload[1] != LocalProtocol::PAYLOAD_MAGIC_1 || payload[2] != PACKET_RETRY_REQUEST ||
        payload[3] != m_transferId) return false;
    const std::uint16_t startIndex = readLe16(&payload[4]);
    const std::size_t bitmapBytes = payload[6];
    if (bitmapBytes == 0 || bitmapBytes > 36 || length != 7 + bitmapBytes) return false;
    m_retryCount = 0;
    m_retryCursor = 0;
    for (std::size_t byteIndex = 0; byteIndex < bitmapBytes; ++byteIndex) {
        const std::uint8_t bits = payload[7 + byteIndex];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            if ((bits & static_cast<std::uint8_t>(1U << bit)) == 0) continue;
            const std::uint32_t packetIndex = static_cast<std::uint32_t>(startIndex) + byteIndex * 8U + bit;
            if (packetIndex < m_totalPackets && m_retryCount < LocalProtocol::PAYLOAD_MAX_RETRY_PACKETS) {
                m_retryPackets[m_retryCount++] = static_cast<std::uint16_t>(packetIndex);
            }
        }
    }
    if (m_retryCount != 0) {
        m_state = LocalProtocol::CACHE_STATE_SENDING;
        m_txPhase = TxPhase::REPAIR;
        m_packetPending = false;
    }
    return true;
}

bool PayloadCacheService::nextPayloadPacket(std::uint8_t* payload,
                                            std::size_t capacity,
                                            std::size_t& length) {
    length = 0;
    if (!m_valid || m_state != LocalProtocol::CACHE_STATE_SENDING || m_txPhase == TxPhase::IDLE ||
        payload == nullptr || capacity < Generated::RF_SEGMENT_MAX_DATA) return false;
    std::memset(payload, 0, Generated::RF_SEGMENT_MAX_DATA);
    payload[0] = LocalProtocol::PAYLOAD_MAGIC_0;
    payload[1] = LocalProtocol::PAYLOAD_MAGIC_1;
    payload[3] = m_transferId;
    if (m_txPhase == TxPhase::HEADERS) {
        payload[2] = LocalProtocol::PAYLOAD_PACKET_HEADER;
        writeLe32(&payload[4], m_productId);
        writeLe32(&payload[8], m_totalBytes);
        writeLe16(&payload[12], m_totalPackets);
        payload[14] = LocalProtocol::PAYLOAD_PACKET_DATA_BYTES;
        writeLe16(&payload[15], m_expectedCrc);
        length = 17;
    } else if (m_txPhase == TxPhase::DATA || m_txPhase == TxPhase::REPAIR) {
        const std::uint16_t packetIndex = m_txPhase == TxPhase::DATA ? m_nextPacketIndex : m_retryPackets[m_retryCursor];
        const std::uint32_t offset = static_cast<std::uint32_t>(packetIndex) * LocalProtocol::PAYLOAD_PACKET_DATA_BYTES;
        if (offset >= m_totalBytes) return false;
        const std::uint32_t remaining = m_totalBytes - offset;
        const std::uint8_t dataLength = static_cast<std::uint8_t>(
            remaining > LocalProtocol::PAYLOAD_PACKET_DATA_BYTES ? LocalProtocol::PAYLOAD_PACKET_DATA_BYTES : remaining);
        payload[2] = LocalProtocol::PAYLOAD_PACKET_DATA;
        writeLe16(&payload[4], packetIndex);
        payload[6] = dataLength;
        std::memcpy(&payload[7], &m_cache[offset], dataLength);
        const std::size_t crcOffset = 7 + dataLength;
        writeLe16(&payload[crcOffset], crc16Ccitt(payload, crcOffset));
        length = crcOffset + 2;
    } else if (m_txPhase == TxPhase::END) {
        payload[2] = LocalProtocol::PAYLOAD_PACKET_END;
        writeLe16(&payload[4], m_totalPackets);
        writeLe16(&payload[6], m_expectedCrc);
        length = 8;
    }
    m_packetPending = length != 0;
    return m_packetPending;
}

void PayloadCacheService::payloadPacketSent(bool sent) {
    if (!m_packetPending || !sent) return;
    m_packetPending = false;
    if (m_txPhase == TxPhase::HEADERS) {
        if (--m_headersRemaining == 0) m_txPhase = TxPhase::DATA;
    } else if (m_txPhase == TxPhase::DATA) {
        if (++m_nextPacketIndex >= m_totalPackets) m_txPhase = TxPhase::END;
    } else if (m_txPhase == TxPhase::REPAIR) {
        if (++m_retryCursor >= m_retryCount) m_txPhase = TxPhase::END;
    } else if (m_txPhase == TxPhase::END) {
        m_txPhase = TxPhase::IDLE;
        m_state = LocalProtocol::CACHE_STATE_READY;
        prepareResponse(m_commitRequestId, LocalProtocol::STATUS_OK, LocalProtocol::CACHE_OP_COMMIT_AND_SEND);
    }
}

void PayloadCacheService::invalidate(std::uint8_t state) {
    m_state = state;
    m_transferId = 0;
    m_productId = 0;
    m_totalBytes = 0;
    m_receivedBytes = 0;
    m_expectedCrc = 0;
    m_valid = false;
    m_txPhase = TxPhase::IDLE;
    m_headersRemaining = 0;
    m_nextPacketIndex = 0;
    m_totalPackets = 0;
    m_retryCount = 0;
    m_retryCursor = 0;
    m_packetPending = false;
    m_commitRequestId = 0;
}

PreviewService::PreviewService() = default;
void PreviewService::writeLe16(std::uint8_t* data, std::uint16_t value) { write16(data, value); }
void PreviewService::writeLe32(std::uint8_t* data, std::uint32_t value) { write32(data, value); }
std::uint16_t PreviewService::readLe16(const std::uint8_t* data) { return read16(data); }
std::uint32_t PreviewService::readLe32(const std::uint8_t* data) { return read32(data); }
std::uint16_t PreviewService::frameCrc() const { return crc16Ccitt(m_frame, m_totalBytes); }

bool PreviewService::identityMatches(std::uint16_t session,
                                     std::uint32_t frameSequence,
                                     std::uint16_t totalBytes,
                                     std::uint16_t crc) const {
    return m_session == session && m_frameSequence == frameSequence && m_totalBytes == totalBytes &&
           m_expectedCrc == crc;
}

void PreviewService::prepareResponse(std::uint8_t requestId, std::uint8_t status, std::uint8_t operation) {
    m_response[0] = LocalProtocol::TARGET_LEPTON_PREVIEW;
    m_response[1] = requestId;
    m_response[2] = status;
    m_response[3] = 8;
    m_response[4] = operation;
    writeLe16(&m_response[5], m_session);
    m_response[7] = m_state;
    writeLe32(&m_response[8], m_receivedBytes);
    m_responseLen = sizeof(m_response);
}

bool PreviewService::beginLocalFrame(const std::uint8_t* payload, std::size_t length) {
    if (payload == nullptr || length < LocalProtocol::HEADER_LEN) return false;
    const std::uint8_t requestId = payload[1];
    const std::size_t bodyLen = payload[2];
    if (payload[0] != LocalProtocol::TARGET_LEPTON_PREVIEW || payload[3] != 0 || bodyLen == 0 ||
        length != LocalProtocol::HEADER_LEN + bodyLen) {
        prepareResponse(requestId,
                        LocalProtocol::STATUS_BAD_REQUEST,
                        length > LocalProtocol::HEADER_LEN ? payload[4] : 0);
        return true;
    }
    const std::uint8_t* body = &payload[LocalProtocol::HEADER_LEN];
    switch (body[0]) {
        case LocalProtocol::PREVIEW_OP_BEGIN: handleBegin(requestId, body, bodyLen); break;
        case LocalProtocol::PREVIEW_OP_CHUNK: handleChunk(requestId, body, bodyLen); break;
        case LocalProtocol::PREVIEW_OP_COMMIT_AND_SEND: handleCommit(requestId, body, bodyLen); break;
        case LocalProtocol::PREVIEW_OP_ABORT: handleAbort(requestId, body, bodyLen); break;
        default: prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]); break;
    }
    return true;
}

bool PreviewService::pollLocalResponse(std::uint8_t* payload, std::size_t capacity, std::size_t& length) {
    length = 0;
    if (m_responseLen == 0 || payload == nullptr || capacity < m_responseLen) return false;
    std::memcpy(payload, m_response, m_responseLen);
    length = m_responseLen;
    m_responseLen = 0;
    return true;
}

bool PreviewService::isTransferActive() const {
    return m_state == LocalProtocol::PREVIEW_STATE_RECEIVING || m_state == LocalProtocol::PREVIEW_STATE_SENDING;
}

void PreviewService::rejectBusy(std::uint8_t requestId, std::uint8_t operation) {
    prepareResponse(requestId, LocalProtocol::STATUS_BUSY, operation);
}

void PreviewService::handleBegin(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen != 14) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    const std::uint16_t session = readLe16(&body[1]);
    const std::uint32_t sequence = readLe32(&body[3]);
    const std::uint8_t width = body[7];
    const std::uint8_t height = body[8];
    const std::uint8_t pixelFormat = body[9];
    const std::uint16_t totalBytes = readLe16(&body[10]);
    const std::uint16_t crc = readLe16(&body[12]);
    if (session == 0 || width != LocalProtocol::PREVIEW_WIDTH || height != LocalProtocol::PREVIEW_HEIGHT ||
        pixelFormat != LocalProtocol::PREVIEW_PIXEL_FORMAT_U8 || totalBytes == 0 ||
        totalBytes > LocalProtocol::PREVIEW_MAX_FRAME_BYTES || totalBytes != width * height) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    if (m_state == LocalProtocol::PREVIEW_STATE_SENDING) {
        prepareResponse(requestId, LocalProtocol::STATUS_BUSY, body[0]);
        return;
    }
    if (m_state == LocalProtocol::PREVIEW_STATE_READY && identityMatches(session, sequence, totalBytes, crc)) {
        prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
        return;
    }
    invalidate(LocalProtocol::PREVIEW_STATE_RECEIVING);
    m_session = session;
    m_frameSequence = sequence;
    m_totalBytes = totalBytes;
    m_expectedCrc = crc;
    m_width = width;
    m_height = height;
    m_pixelFormat = pixelFormat;
    prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
}

void PreviewService::handleChunk(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen < 7 || readLe16(&body[1]) != m_session) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    const std::uint16_t offset = readLe16(&body[3]);
    const std::size_t chunkLen = body[5];
    if (chunkLen == 0 || chunkLen > LocalProtocol::PREVIEW_CHUNK_BYTES || bodyLen != 6 + chunkLen ||
        offset > m_totalBytes || chunkLen > static_cast<std::size_t>(m_totalBytes - offset) ||
        m_state != LocalProtocol::PREVIEW_STATE_RECEIVING) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    if (offset < m_receivedBytes) {
        if (chunkLen <= static_cast<std::size_t>(m_receivedBytes - offset) &&
            std::memcmp(&m_frame[offset], &body[6], chunkLen) == 0) {
            prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
        } else {
            prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        }
        return;
    }
    if (offset != m_receivedBytes) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    std::memcpy(&m_frame[offset], &body[6], chunkLen);
    m_receivedBytes = static_cast<std::uint16_t>(m_receivedBytes + chunkLen);
    prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
}

void PreviewService::handleCommit(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen != 3 || readLe16(&body[1]) != m_session) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    if (m_state == LocalProtocol::PREVIEW_STATE_SENDING) {
        prepareResponse(requestId, LocalProtocol::STATUS_BUSY, body[0]);
        return;
    }
    if (m_state != LocalProtocol::PREVIEW_STATE_RECEIVING || m_receivedBytes != m_totalBytes || frameCrc() != m_expectedCrc) {
        invalidate(LocalProtocol::PREVIEW_STATE_ERROR);
        prepareResponse(requestId, LocalProtocol::STATUS_TARGET_ERROR, body[0]);
        return;
    }
    const std::size_t fragments = (m_totalBytes + PREVIEW_FRAGMENT_DATA_BYTES - 1U) / PREVIEW_FRAGMENT_DATA_BYTES;
    if (fragments == 0 || fragments > 200) {
        invalidate(LocalProtocol::PREVIEW_STATE_ERROR);
        prepareResponse(requestId, LocalProtocol::STATUS_TARGET_ERROR, body[0]);
        return;
    }
    m_fragmentCount = static_cast<std::uint16_t>(fragments);
    m_nextFragment = 0;
    m_pending = false;
    m_anySendFailure = false;
    m_commitRequestId = requestId;
    m_state = LocalProtocol::PREVIEW_STATE_SENDING;
}

void PreviewService::handleAbort(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen) {
    if (bodyLen != 3 || (m_session != 0 && readLe16(&body[1]) != m_session)) {
        prepareResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST, body[0]);
        return;
    }
    invalidate(LocalProtocol::PREVIEW_STATE_EMPTY);
    prepareResponse(requestId, LocalProtocol::STATUS_OK, body[0]);
}

bool PreviewService::nextPreviewPacket(std::uint8_t* payload, std::size_t capacity, std::size_t& length) {
    length = 0;
    if (payload == nullptr || capacity < Generated::RF_SEGMENT_MAX_DATA ||
        m_state != LocalProtocol::PREVIEW_STATE_SENDING || m_pending || m_nextFragment >= m_fragmentCount) return false;
    const std::uint16_t offset = static_cast<std::uint16_t>(m_nextFragment * PREVIEW_FRAGMENT_DATA_BYTES);
    const std::uint16_t remaining = static_cast<std::uint16_t>(m_totalBytes - offset);
    const std::uint8_t dataLength = static_cast<std::uint8_t>(
        remaining > PREVIEW_FRAGMENT_DATA_BYTES ? PREVIEW_FRAGMENT_DATA_BYTES : remaining);
    payload[0] = LocalProtocol::PREVIEW_WIRE_MAGIC_0;
    payload[1] = LocalProtocol::PREVIEW_WIRE_MAGIC_1;
    payload[2] = LocalProtocol::PREVIEW_WIRE_VERSION;
    payload[3] = LocalProtocol::PREVIEW_WIRE_TYPE_FRAGMENT;
    writeLe16(&payload[4], m_session);
    writeLe32(&payload[6], m_frameSequence);
    payload[10] = m_width;
    payload[11] = m_height;
    payload[12] = m_pixelFormat;
    payload[13] = static_cast<std::uint8_t>(m_nextFragment);
    payload[14] = static_cast<std::uint8_t>(m_fragmentCount);
    payload[15] = dataLength;
    writeLe16(&payload[16], m_totalBytes);
    writeLe16(&payload[18], m_expectedCrc);
    std::memcpy(&payload[PREVIEW_WIRE_HEADER_LEN], &m_frame[offset], dataLength);
    length = PREVIEW_WIRE_HEADER_LEN + dataLength;
    m_pending = true;
    return true;
}

void PreviewService::previewPacketAttempted(bool sent) {
    if (!m_pending) return;
    m_pending = false;
    if (!sent) m_anySendFailure = true;
    ++m_nextFragment;
    if (m_nextFragment >= m_fragmentCount) {
        m_state = LocalProtocol::PREVIEW_STATE_READY;
        prepareResponse(m_commitRequestId,
                        m_anySendFailure ? LocalProtocol::STATUS_TARGET_ERROR : LocalProtocol::STATUS_OK,
                        LocalProtocol::PREVIEW_OP_COMMIT_AND_SEND);
    }
}

void PreviewService::invalidate(std::uint8_t state) {
    m_state = state;
    m_session = 0;
    m_frameSequence = 0;
    m_totalBytes = 0;
    m_receivedBytes = 0;
    m_expectedCrc = 0;
    m_width = 0;
    m_height = 0;
    m_pixelFormat = 0;
    m_fragmentCount = 0;
    m_nextFragment = 0;
    m_pending = false;
    m_anySendFailure = false;
    m_commitRequestId = 0;
}

LocalServicesRouter::LocalServicesRouter(RfStatusProvider& radio,
                                         PduProxy& pdu,
                                         PayloadCacheService& cache,
                                         PreviewService& preview)
    : m_radio(radio), m_pdu(pdu), m_cache(cache), m_preview(preview) {}

void LocalServicesRouter::writeLe16(std::uint8_t* out, std::uint16_t value) { write16(out, value); }
void LocalServicesRouter::writeLe32(std::uint8_t* out, std::uint32_t value) { write32(out, value); }

void LocalServicesRouter::prepareErrorResponse(std::uint8_t requestId, std::uint8_t status, std::uint8_t target) {
    m_rfResponse[0] = target;
    m_rfResponse[1] = requestId;
    m_rfResponse[2] = status;
    m_rfResponse[3] = 0;
    m_rfResponseLen = LocalProtocol::HEADER_LEN;
}

void LocalServicesRouter::prepareRfStatusResponse(std::uint8_t requestId) {
    const RfStatusSnapshot stats = m_radio.status();
    m_rfResponse[0] = LocalProtocol::TARGET_RF_STATUS;
    m_rfResponse[1] = requestId;
    m_rfResponse[2] = LocalProtocol::STATUS_OK;
    m_rfResponse[3] = LocalProtocol::RF_STATUS_PAYLOAD_LEN;
    m_rfResponse[4] = LocalProtocol::RF_OP_STATUS;
    writeLe16(&m_rfResponse[5], static_cast<std::uint16_t>(stats.lastRssiDbm));
    writeLe16(&m_rfResponse[7], stats.rxGood);
    writeLe16(&m_rfResponse[9], stats.rxBad);
    writeLe16(&m_rfResponse[11], stats.txGood);
    writeLe32(&m_rfResponse[13], stats.rfRxPackets);
    writeLe32(&m_rfResponse[17], stats.rfTxPackets);
    writeLe32(&m_rfResponse[21], stats.rfTxDrops);
    m_rfResponse[25] = static_cast<std::uint8_t>(stats.state);
    m_rfResponse[26] = static_cast<std::uint8_t>(stats.fault);
    m_rfResponse[27] = stats.bootFlags;
    m_rfResponse[28] = stats.rssiValid ? 1U : 0U;
    writeLe32(&m_rfResponse[29], stats.lastAcceptedRssiAgeMs);
    writeLe32(&m_rfResponse[33], stats.initAttempts);
    m_rfResponseLen = sizeof(m_rfResponse);
}

void LocalServicesRouter::prepareRfSetEnabledResponse(std::uint8_t requestId, bool enabled) {
    const bool ok = m_radio.setEnabled(enabled);
    m_rfResponse[0] = LocalProtocol::TARGET_RF_STATUS;
    m_rfResponse[1] = requestId;
    m_rfResponse[2] = ok ? LocalProtocol::STATUS_OK : LocalProtocol::STATUS_TARGET_ERROR;
    m_rfResponse[3] = LocalProtocol::RF_SET_ENABLED_PAYLOAD_LEN;
    m_rfResponse[4] = LocalProtocol::RF_OP_SET_ENABLED;
    m_rfResponse[5] = enabled ? 1U : 0U;
    const RfStatusSnapshot stats = m_radio.status();
    m_rfResponse[6] = static_cast<std::uint8_t>(stats.state);
    m_rfResponse[7] = static_cast<std::uint8_t>(stats.fault);
    m_rfResponseLen = LocalProtocol::HEADER_LEN + LocalProtocol::RF_SET_ENABLED_PAYLOAD_LEN;
}

bool LocalServicesRouter::beginLocalFrame(const std::uint8_t* payload, std::size_t length) {
    return beginLocalFrame(payload, length, 0);
}

bool LocalServicesRouter::beginLocalFrame(const std::uint8_t* payload,
                                          std::size_t length,
                                          std::uint32_t nowMs) {
    if (payload == nullptr || length < LocalProtocol::HEADER_LEN) {
        prepareErrorResponse(0, LocalProtocol::STATUS_BAD_REQUEST);
        return true;
    }
    const std::uint8_t target = payload[0];
    const std::uint8_t requestId = payload[1];
    const std::size_t bodyLen = payload[2];
    const std::uint8_t operation = length > LocalProtocol::HEADER_LEN ? payload[4] : 0;
    if (target == LocalProtocol::TARGET_PDU) return m_pdu.beginLocalFrame(payload, length, nowMs);
    if (target == LocalProtocol::TARGET_PAYLOAD_CACHE) {
        if (m_preview.isTransferActive()) {
            m_cache.rejectBusy(requestId, operation);
            return true;
        }
        return m_cache.beginLocalFrame(payload, length);
    }
    if (target == LocalProtocol::TARGET_LEPTON_PREVIEW) {
        if (m_cache.isTransferActive()) {
            m_preview.rejectBusy(requestId, operation);
            return true;
        }
        return m_preview.beginLocalFrame(payload, length);
    }
    if (target != LocalProtocol::TARGET_RF_STATUS || payload[3] != 0 || bodyLen < 1 ||
        length != LocalProtocol::HEADER_LEN + bodyLen) {
        prepareErrorResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST);
        return true;
    }
    if (operation == LocalProtocol::RF_OP_STATUS && bodyLen == 1) {
        prepareRfStatusResponse(requestId);
        return true;
    }
    if (operation == LocalProtocol::RF_OP_SET_ENABLED && bodyLen == 2 && payload[5] <= 1) {
        prepareRfSetEnabledResponse(requestId, payload[5] != 0);
        return true;
    }
    prepareErrorResponse(requestId, LocalProtocol::STATUS_BAD_REQUEST);
    return true;
}

bool LocalServicesRouter::pollLocalResponse(std::uint32_t nowMs,
                                            std::uint8_t* payload,
                                            std::size_t capacity,
                                            std::size_t& length) {
    length = 0;
    if (m_rfResponseLen != 0) {
        if (payload == nullptr || capacity < m_rfResponseLen) return false;
        std::memcpy(payload, m_rfResponse, m_rfResponseLen);
        length = m_rfResponseLen;
        m_rfResponseLen = 0;
        return true;
    }
    if (m_cache.pollLocalResponse(payload, capacity, length)) return true;
    if (m_preview.pollLocalResponse(payload, capacity, length)) return true;
    return m_pdu.pollLocalResponse(nowMs, payload, capacity, length);
}

void WatchdogModel::boot(bool watchdogReset, std::uint32_t nowMs) {
    m_watchdogReset = watchdogReset;
    m_lastFeedMs = nowMs;
}

void WatchdogModel::feed(std::uint32_t nowMs) { m_lastFeedMs = nowMs; }

bool WatchdogModel::expired(std::uint32_t nowMs) const {
    return static_cast<std::uint32_t>(nowMs - m_lastFeedMs) >= TIMEOUT_MS;
}

}  // namespace SatelliteController
