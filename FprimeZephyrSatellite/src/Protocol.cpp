#include "SatelliteController/Protocol.hpp"

namespace SatelliteController {

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0U ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                                        : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

RfHeaderStatus classifyRfHeader(std::uint8_t to,
                               std::uint8_t from,
                               std::uint8_t network,
                               std::uint8_t version) {
    if (network != Protocol::RF_NETWORK_ID) return RfHeaderStatus::WRONG_NETWORK;
    if (to != Protocol::RF_LOCAL_ADDRESS || from != Protocol::RF_REMOTE_ADDRESS) {
        return RfHeaderStatus::WRONG_ADDRESS;
    }
    if (version != Protocol::RF_PROTOCOL_VERSION) return RfHeaderStatus::WRONG_VERSION;
    return RfHeaderStatus::ACCEPT;
}

ParseResult FrameParser::resetWith(ParseResult result) {
    reset();
    return result;
}

void FrameParser::reset() {
    m_state = State::MAGIC_0;
    m_frame = {};
    m_index = 0;
    m_receivedCrc = 0;
}

ParseResult FrameParser::consume(std::uint8_t byte, std::uint32_t nowMs, Frame& output) {
    if (m_state != State::MAGIC_0 && (nowMs - m_lastByteMs) > Protocol::FRAME_TIMEOUT_MS) {
        reset();
        m_lastByteMs = nowMs;
        if (byte == Protocol::FRAME_MAGIC_0) {
            m_state = State::MAGIC_1;
        }
        return ParseResult::TIMEOUT;
    }
    m_lastByteMs = nowMs;
    switch (m_state) {
        case State::MAGIC_0:
            if (byte == Protocol::FRAME_MAGIC_0) m_state = State::MAGIC_1;
            break;
        case State::MAGIC_1:
            if (byte == Protocol::FRAME_MAGIC_1) {
                m_state = State::CHANNEL;
            } else {
                // The Arduino oracle silently resynchronizes on a bad second
                // magic byte; it does not increment framingDrops here.
                reset();
            }
            break;
        case State::CHANNEL:
            if (byte >= Protocol::CHANNEL_COUNT) return resetWith(ParseResult::FRAMING_DROP);
            m_frame.channel = byte;
            m_state = State::LEN_LO;
            break;
        case State::LEN_LO:
            m_frame.length = byte;
            m_state = State::LEN_HI;
            break;
        case State::LEN_HI:
            m_frame.length |= static_cast<std::uint16_t>(byte) << 8U;
            if (m_frame.length == 0 || m_frame.length > Protocol::FRAME_MAX_PAYLOAD) {
                return resetWith(ParseResult::FRAMING_DROP);
            }
            m_state = State::PAYLOAD;
            break;
        case State::PAYLOAD:
            m_frame.payload[m_index++] = byte;
            if (m_index == m_frame.length) m_state = State::CRC_LO;
            break;
        case State::CRC_LO:
            m_receivedCrc = byte;
            m_state = State::CRC_HI;
            break;
        case State::CRC_HI: {
            m_receivedCrc |= static_cast<std::uint16_t>(byte) << 8U;
            if (m_receivedCrc != crc16Ccitt(m_frame.payload.data(), m_frame.length)) {
                return resetWith(ParseResult::CRC_DROP);
            }
            output = m_frame;
            return resetWith(ParseResult::FRAME);
        }
    }
    return ParseResult::NONE;
}

static bool channelForMagic(std::uint8_t magic, std::uint8_t& channel) {
    if (magic == Protocol::RF_MAGIC_CCSDS) channel = Protocol::CHANNEL_CCSDS;
    else if (magic == Protocol::RF_MAGIC_PAYLOAD) channel = Protocol::CHANNEL_PAYLOAD;
    else return false;
    return true;
}

bool decodeSegment(const std::uint8_t* packet, std::size_t packetLength, Segment& output) {
    if (packetLength < Protocol::RF_SEGMENT_HEADER_LEN || packetLength > Protocol::RF_PACKET_MAX_LEN) return false;
    if (!channelForMagic(packet[0], output.channel)) return false;
    output.messageId = packet[1];
    output.index = packet[2];
    output.count = packet[3];
    output.length = packet[4];
    // ACK wire format reuses byte 3 for the acknowledged segment index:
    // [magic, message_id, 0xFF, acknowledged_index, 0].
    if (output.index == Protocol::RF_ACK_INDEX) {
        return output.count != Protocol::RF_ACK_INDEX && output.length == 0 && packetLength == 5;
    }
    if (output.count == 0 || output.index >= output.count || output.length == 0 ||
        output.length > Protocol::RF_SEGMENT_MAX_DATA || packetLength != output.length + 5U) return false;
    for (std::size_t i = 0; i < output.length; ++i) output.data[i] = packet[5 + i];
    return true;
}

std::size_t encodeAck(std::uint8_t channel,
                      std::uint8_t messageId,
                      std::uint8_t acknowledgedIndex,
                      std::uint8_t* output,
                      std::size_t outputCapacity) {
    if (output == nullptr || outputCapacity < Protocol::RF_SEGMENT_HEADER_LEN ||
        channel >= Protocol::RF_CHANNEL_COUNT || acknowledgedIndex == Protocol::RF_ACK_INDEX) return 0;
    output[0] = channel == Protocol::CHANNEL_PAYLOAD ? Protocol::RF_MAGIC_PAYLOAD : Protocol::RF_MAGIC_CCSDS;
    output[1] = messageId;
    output[2] = Protocol::RF_ACK_INDEX;
    output[3] = acknowledgedIndex;
    output[4] = 0;
    return 5;
}

std::size_t encodeSegment(const Segment& segment,
                          std::uint8_t* output,
                          std::size_t outputCapacity) {
    if (segment.channel >= Protocol::RF_CHANNEL_COUNT || segment.count == 0 || segment.index >= segment.count ||
        segment.length == 0 || segment.length > Protocol::RF_SEGMENT_MAX_DATA || output == nullptr ||
        outputCapacity < static_cast<std::size_t>(segment.length) + Protocol::RF_SEGMENT_HEADER_LEN) return 0;
    output[0] = segment.channel == Protocol::CHANNEL_PAYLOAD ? Protocol::RF_MAGIC_PAYLOAD : Protocol::RF_MAGIC_CCSDS;
    output[1] = segment.messageId;
    output[2] = segment.index;
    output[3] = segment.count;
    output[4] = segment.length;
    for (std::size_t i = 0; i < segment.length; ++i) output[5 + i] = segment.data[i];
    return segment.length + 5U;
}

void Reassembler::reset() {
    m_active = false;
    m_channel = m_messageId = m_nextIndex = m_count = 0;
    m_length = 0;
    m_lastSegmentMs = 0;
}

Reassembler::Result Reassembler::expire(std::uint32_t nowMs) {
    if (m_active && (nowMs - m_lastSegmentMs) > Protocol::RF_REASSEMBLY_TIMEOUT_MS) {
        reset();
        return Result::TIMEOUT;
    }
    return Result::NONE;
}

Reassembler::Result Reassembler::accept(const Segment& segment, std::uint32_t nowMs, Frame& output) {
    // Match the Arduino oracle: expire stale state, then evaluate the byte/segment
    // that triggered expiry as the possible start of a new message.
    const bool timedOut = expire(nowMs) == Result::TIMEOUT;
    if (segment.index == Protocol::RF_ACK_INDEX) return Result::NONE;
    if (segment.index == 0) {
        reset();
        m_active = true;
        m_channel = segment.channel;
        m_messageId = segment.messageId;
        m_count = segment.count;
    }
    if (!m_active || segment.channel != m_channel || segment.messageId != m_messageId ||
        segment.count != m_count || segment.index != m_nextIndex ||
        segment.length == 0 || segment.length > Protocol::RF_SEGMENT_MAX_DATA ||
        m_length + segment.length > Protocol::FRAME_MAX_PAYLOAD) {
        reset();
        return Result::DROP;
    }
    for (std::size_t i = 0; i < segment.length; ++i) m_buffer[m_length + i] = segment.data[i];
    m_length += segment.length;
    ++m_nextIndex;
    m_lastSegmentMs = nowMs;
    if (m_nextIndex != m_count) return timedOut ? Result::TIMEOUT : Result::NONE;
    output.channel = m_channel;
    output.length = m_length;
    for (std::size_t i = 0; i < m_length; ++i) output.payload[i] = m_buffer[i];
    reset();
    return Result::COMPLETE;
}

}  // namespace SatelliteController
