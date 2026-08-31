#pragma once

#include <cstddef>
#include <cstdint>

#include "SatelliteController/GeneratedProtocol.hpp"

namespace SatelliteController {

namespace Protocol = Generated;

// Small fixed storage primitive used by the protocol core. Keeping this
// buffer type local avoids requiring the full C++ standard library in the
// Zephyr minimal-libc image while retaining data()/size()/iterator helpers for
// host-side tests and F' adapters.
template <typename T, std::size_t N>
struct StaticBuffer {
    T values[N]{};

    constexpr std::size_t size() const { return N; }
    T* data() { return values; }
    const T* data() const { return values; }
    T* begin() { return values; }
    const T* begin() const { return values; }
    T* end() { return values + N; }
    const T* end() const { return values + N; }
    T& operator[](std::size_t index) { return values[index]; }
    const T& operator[](std::size_t index) const { return values[index]; }
};

struct Frame {
    std::uint8_t channel = 0;
    std::uint16_t length = 0;
    StaticBuffer<std::uint8_t, Protocol::FRAME_MAX_PAYLOAD> payload{};
};

enum class ParseResult { NONE, FRAME, FRAMING_DROP, CRC_DROP, TIMEOUT };

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t length);

enum class RfHeaderStatus { ACCEPT, WRONG_NETWORK, WRONG_ADDRESS, WRONG_VERSION };
RfHeaderStatus classifyRfHeader(std::uint8_t to,
                               std::uint8_t from,
                               std::uint8_t network,
                               std::uint8_t version);

class FrameParser final {
  public:
    ParseResult consume(std::uint8_t byte, std::uint32_t nowMs, Frame& output);
    void reset();

  private:
    enum class State { MAGIC_0, MAGIC_1, CHANNEL, LEN_LO, LEN_HI, PAYLOAD, CRC_LO, CRC_HI };
    ParseResult resetWith(ParseResult result);

    State m_state = State::MAGIC_0;
    Frame m_frame{};
    std::uint16_t m_index = 0;
    std::uint16_t m_receivedCrc = 0;
    std::uint32_t m_lastByteMs = 0;
};

struct Segment {
    std::uint8_t channel = 0;
    std::uint8_t messageId = 0;
    std::uint8_t index = 0;
    std::uint8_t count = 0;
    std::uint8_t length = 0;
    StaticBuffer<std::uint8_t, Protocol::RF_SEGMENT_MAX_DATA> data{};
};

bool decodeSegment(const std::uint8_t* packet, std::size_t packetLength, Segment& output);
std::size_t encodeSegment(const Segment& segment,
                          std::uint8_t* output,
                          std::size_t outputCapacity);
std::size_t encodeAck(std::uint8_t channel,
                      std::uint8_t messageId,
                      std::uint8_t acknowledgedIndex,
                      std::uint8_t* output,
                      std::size_t outputCapacity);

template <typename Output>
std::size_t encodeSegment(const Segment& segment, Output& output) {
    return encodeSegment(segment, output.data(), output.size());
}

template <std::size_t N>
std::size_t encodeSegment(const Segment& segment, std::uint8_t (&output)[N]) {
    return encodeSegment(segment, output, N);
}

template <typename Output>
std::size_t encodeAck(std::uint8_t channel,
                      std::uint8_t messageId,
                      std::uint8_t acknowledgedIndex,
                      Output& output) {
    return encodeAck(channel, messageId, acknowledgedIndex, output.data(), output.size());
}

template <std::size_t N>
std::size_t encodeAck(std::uint8_t channel,
                      std::uint8_t messageId,
                      std::uint8_t acknowledgedIndex,
                      std::uint8_t (&output)[N]) {
    return encodeAck(channel, messageId, acknowledgedIndex, output, N);
}

class Reassembler final {
  public:
    enum class Result { NONE, COMPLETE, DROP, TIMEOUT };
    Result accept(const Segment& segment, std::uint32_t nowMs, Frame& output);
    Result expire(std::uint32_t nowMs);
    void reset();

  private:
    bool m_active = false;
    std::uint8_t m_channel = 0;
    std::uint8_t m_messageId = 0;
    std::uint8_t m_nextIndex = 0;
    std::uint8_t m_count = 0;
    std::uint16_t m_length = 0;
    std::uint32_t m_lastSegmentMs = 0;
    StaticBuffer<std::uint8_t, Protocol::FRAME_MAX_PAYLOAD> m_buffer{};
};

class ChannelReassemblers final {
  public:
    Reassembler::Result accept(const Segment& segment, std::uint32_t nowMs, Frame& output) {
        if (segment.channel >= m_channels.size()) return Reassembler::Result::DROP;
        return m_channels[segment.channel].accept(segment, nowMs, output);
    }
    std::size_t expire(std::uint32_t nowMs) {
        std::size_t count = 0;
        for (auto& channel : m_channels) {
            if (channel.expire(nowMs) == Reassembler::Result::TIMEOUT) ++count;
        }
        return count;
    }
    void reset() {
        for (auto& channel : m_channels) channel.reset();
    }

  private:
    StaticBuffer<Reassembler, Protocol::RF_CHANNEL_COUNT> m_channels{};
};

}  // namespace SatelliteController
