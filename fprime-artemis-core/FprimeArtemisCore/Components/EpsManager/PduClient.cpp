#include "PduClient.hpp"

#include <cstring>

namespace Artemis {
namespace Eps {
std::uint16_t PduClient::crc16(const std::uint8_t* bytes, std::size_t length) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(static_cast<unsigned>(bytes[i]) << 8U);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const std::uint32_t shifted = static_cast<std::uint32_t>(crc) << 1U;
            crc = static_cast<std::uint16_t>((crc & 0x8000U) ? shifted ^ 0x1021U : shifted);
        }
    }
    return crc;
}

bool PduClient::normalRail(std::uint8_t output) {
    switch (output) {
        case PDU_OUTPUT_3V3_1: case PDU_OUTPUT_3V3_2:
        case PDU_OUTPUT_5V_1: case PDU_OUTPUT_5V_2: case PDU_OUTPUT_5V_3:
        case PDU_OUTPUT_12V: case PDU_OUTPUT_VBATT: return true;
        default: return false;
    }
}

int PduClient::outputBit(std::uint8_t output) {
    static const std::uint8_t order[PDU_V2_OUTPUT_COUNT] = {
        PDU_OUTPUT_3V3_1, PDU_OUTPUT_3V3_2, PDU_OUTPUT_5V_1, PDU_OUTPUT_5V_2,
        PDU_OUTPUT_5V_3, PDU_OUTPUT_12V, PDU_OUTPUT_VBATT, PDU_OUTPUT_BURN1, PDU_OUTPUT_BURN2};
    for (unsigned i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) if (order[i] == output) return static_cast<int>(i);
    return -1;
}

bool PduClient::request(std::uint8_t opcode, const std::uint8_t* payload, std::size_t length,
                        std::uint32_t nowMs, std::uint32_t timeoutMs) {
    if (m_pending || m_resultReady || length > PDU_V2_MAX_PAYLOAD_LEN || (length != 0 && payload == nullptr) ||
        timeoutMs == 0 || timeoutMs > 60000) return false;
    if (++m_sequence == 0) ++m_sequence;
    m_opcode = opcode;
    m_started = nowMs;
    m_timeout = timeoutMs;
    m_request[0] = PDU_V2_SOF;
    m_request[1] = PDU_V2_VERSION;
    m_request[2] = PDU_V2_MSG_REQUEST;
    m_request[3] = opcode;
    m_request[4] = m_sequence;
    m_request[5] = 0;
    m_request[6] = static_cast<std::uint8_t>(length);
    if (length) std::memcpy(m_request.data() + PDU_V2_HEADER_LEN, payload, length);
    const auto crc = crc16(m_request.data() + 1, PDU_V2_HEADER_LEN - 1U + length);
    m_request[PDU_V2_HEADER_LEN + length] = static_cast<std::uint8_t>(crc);
    m_request[PDU_V2_HEADER_LEN + length + 1] = static_cast<std::uint8_t>(crc >> 8U);
    m_requestLength = PDU_V2_HEADER_LEN + length + PDU_V2_CRC_LEN;
    m_pending = true;
    resetParser();
    return true;
}

void PduClient::complete(Completion completion) {
    m_result = Result{};
    m_result.completion = completion;
    m_result.opcode = m_opcode;
    m_result.sequence = m_sequence;
    m_pending = false;
    m_resultReady = true;
}
void PduClient::transportResult(bool success) {
    if (!success && m_pending) complete(Completion::TRANSPORT_ERROR);
}
void PduClient::resetParser() { m_count = 0; m_expected = 0; }

void PduClient::tick(std::uint32_t nowMs) {
    if (m_count && static_cast<std::uint32_t>(nowMs - m_lastByte) > 100) {
        resetParser(); ++m_framingErrors;
    }
    if (m_pending && static_cast<std::uint32_t>(nowMs - m_started) >= m_timeout) {
        ++m_timeouts;
        complete(Completion::TIMEOUT);
    }
}

void PduClient::receive(const std::uint8_t* bytes, std::size_t length, std::uint32_t nowMs) {
    if (bytes == nullptr) return;
    tick(nowMs);
    for (std::size_t i = 0; i < length; ++i) accept(bytes[i], nowMs);
}

void PduClient::accept(std::uint8_t byte, std::uint32_t nowMs) {
    if (m_count == 0 && byte != PDU_V2_SOF) return;
    m_frame[m_count++] = byte;
    m_lastByte = nowMs;
    if (m_count == PDU_V2_HEADER_LEN) {
        if (m_frame[6] > PDU_V2_MAX_PAYLOAD_LEN) { resetParser(); ++m_framingErrors; return; }
        m_expected = PDU_V2_HEADER_LEN + m_frame[6] + PDU_V2_CRC_LEN;
    }
    if (m_expected == 0 || m_count != m_expected) return;
    const auto crc = crc16(m_frame.data() + 1, m_expected - 1U - PDU_V2_CRC_LEN);
    const auto received = static_cast<std::uint16_t>(static_cast<unsigned>(m_frame[m_expected - 2]) |
                                                    static_cast<unsigned>(m_frame[m_expected - 1]) << 8U);
    if (crc != received) ++m_crcErrors;
    else if (!m_pending || m_frame[1] != PDU_V2_VERSION || m_frame[2] != PDU_V2_MSG_RESPONSE ||
             m_frame[3] != m_opcode || m_frame[4] != m_sequence) ++m_ignored;
    else {
        complete(Completion::RESPONSE);
        m_result.status = m_frame[5];
        m_result.length = m_frame[6];
        if (m_result.length) std::memcpy(m_result.payload.data(), m_frame.data() + PDU_V2_HEADER_LEN, m_result.length);
    }
    resetParser();
}

bool PduClient::takeResult(Result& result) {
    if (!m_resultReady) return false;
    result = m_result;
    m_resultReady = false;
    return true;
}
}  // namespace Eps
}  // namespace Artemis
