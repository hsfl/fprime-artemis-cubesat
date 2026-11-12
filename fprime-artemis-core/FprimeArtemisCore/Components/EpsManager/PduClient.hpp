#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "pdu_protocol_v2.h"

namespace Artemis {
namespace Eps {

enum class Completion : std::uint8_t { RESPONSE, TIMEOUT, TRANSPORT_ERROR };
struct Result {
    Completion completion = Completion::RESPONSE;
    std::uint8_t opcode = 0, sequence = 0, status = 0, length = 0;
    std::array<std::uint8_t, PDU_V2_MAX_PAYLOAD_LEN> payload{};
};

// Allocation-free, serialized UART v2 client. It never actuates a host pin.
class PduClient {
  public:
    bool request(std::uint8_t opcode, const std::uint8_t* payload, std::size_t length,
                 std::uint32_t nowMs, std::uint32_t timeoutMs = 1000);
    void transportResult(bool success);
    void receive(const std::uint8_t* bytes, std::size_t length, std::uint32_t nowMs);
    void tick(std::uint32_t nowMs);
    bool takeResult(Result& result);
    bool pending() const { return m_pending; }
    const std::uint8_t* requestBytes() const { return m_request.data(); }
    std::size_t requestLength() const { return m_requestLength; }
    std::uint32_t crcErrors() const { return m_crcErrors; }
    std::uint32_t framingErrors() const { return m_framingErrors; }
    std::uint32_t ignoredResponses() const { return m_ignored; }
    std::uint32_t timeouts() const { return m_timeouts; }
    static bool normalRail(std::uint8_t output);
    static int outputBit(std::uint8_t output);
    static std::uint16_t crc16(const std::uint8_t* bytes, std::size_t length);

  private:
    void accept(std::uint8_t byte, std::uint32_t nowMs);
    void resetParser();
    void complete(Completion completion);
    std::array<std::uint8_t, PDU_V2_MAX_FRAME_LEN> m_request{}, m_frame{};
    std::size_t m_requestLength = 0, m_count = 0, m_expected = 0;
    std::uint8_t m_sequence = 0, m_opcode = 0;
    std::uint32_t m_started = 0, m_timeout = 1000, m_lastByte = 0;
    bool m_pending = false, m_resultReady = false;
    Result m_result;
    std::uint32_t m_crcErrors = 0, m_framingErrors = 0, m_ignored = 0, m_timeouts = 0;
};
}  // namespace Eps
}  // namespace Artemis
