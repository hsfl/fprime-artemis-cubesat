#include "SatelliteController/Protocol.hpp"
#include "SatelliteController/RadioStateMachine.hpp"
#include "SatelliteController/LocalServices.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

using namespace SatelliteController;

static std::vector<std::uint8_t> uartFrame(std::uint8_t channel, const std::vector<std::uint8_t>& payload) {
    const auto crc = crc16Ccitt(payload.data(), payload.size());
    std::vector<std::uint8_t> bytes{Protocol::FRAME_MAGIC_0, Protocol::FRAME_MAGIC_1, channel,
                                    static_cast<std::uint8_t>(payload.size()),
                                    static_cast<std::uint8_t>(payload.size() >> 8U)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    bytes.push_back(static_cast<std::uint8_t>(crc));
    bytes.push_back(static_cast<std::uint8_t>(crc >> 8U));
    return bytes;
}

int main() {
    assert(classifyRfHeader(0xA2, 0xA1, 0xC3, 1) == RfHeaderStatus::ACCEPT);
    assert(classifyRfHeader(0xA2, 0xA1, 0xD2, 1) == RfHeaderStatus::WRONG_NETWORK);
    assert(classifyRfHeader(0xA1, 0xA2, 0xC3, 1) == RfHeaderStatus::WRONG_ADDRESS);
    assert(classifyRfHeader(0xA2, 0xA1, 0xC3, 2) == RfHeaderStatus::WRONG_VERSION);
    FrameParser parser;
    Frame frame;
    assert(parser.consume(Protocol::FRAME_MAGIC_0, 0, frame) == ParseResult::NONE);
    assert(parser.consume(0x00, 1, frame) == ParseResult::NONE);
    auto bytes = uartFrame(Protocol::CHANNEL_CCSDS, {1, 2, 3, 4});
    ParseResult result = ParseResult::NONE;
    for (std::size_t i = 0; i < bytes.size(); ++i) result = parser.consume(bytes[i], static_cast<std::uint32_t>(10 + i), frame);
    assert(result == ParseResult::FRAME && frame.length == 4 && frame.payload[3] == 4);

    bytes.back() ^= 1U;
    for (std::size_t i = 0; i < bytes.size(); ++i) result = parser.consume(bytes[i], static_cast<std::uint32_t>(20 + i), frame);
    assert(result == ParseResult::CRC_DROP);
    assert(parser.consume(Protocol::FRAME_MAGIC_0, 100, frame) == ParseResult::NONE);
    assert(parser.consume(Protocol::FRAME_MAGIC_1, 400, frame) == ParseResult::TIMEOUT);

    Segment a{};
    a.channel = Protocol::CHANNEL_PAYLOAD; a.messageId = 7; a.index = 0; a.count = 2; a.length = 3;
    a.data[0] = 10; a.data[1] = 11; a.data[2] = 12;
    Segment b = a; b.index = 1; b.length = 2; b.data[0] = 13; b.data[1] = 14;
    std::array<std::uint8_t, Protocol::RF_PACKET_MAX_LEN> packet{};
    const auto packetLength = encodeSegment(a, packet);
    Segment decoded{};
    assert(packetLength == 8 && decodeSegment(packet.data(), packetLength, decoded));
    Reassembler reassembler;
    assert(reassembler.accept(decoded, 0, frame) == Reassembler::Result::NONE);
    assert(encodeAck(Protocol::CHANNEL_CCSDS, 7, 1, packet) == 5);
    assert(decodeSegment(packet.data(), 5, decoded));
    assert(decoded.index == Protocol::RF_ACK_INDEX && decoded.count == 1);
    packet[3] = Protocol::RF_ACK_INDEX;
    assert(!decodeSegment(packet.data(), 5, decoded));
    assert(reassembler.accept(b, 1, frame) == Reassembler::Result::COMPLETE);
    assert(frame.channel == Protocol::CHANNEL_PAYLOAD && frame.length == 5 && frame.payload[4] == 14);
    assert(!decodeSegment(packet.data(), 4, decoded));
    assert(reassembler.accept(a, 10, frame) == Reassembler::Result::NONE);
    assert(reassembler.expire(511) == Reassembler::Result::TIMEOUT);
    assert(reassembler.accept(b, 512, frame) == Reassembler::Result::DROP);

    ChannelReassemblers channels;
    Segment c0 = a; c0.channel = Protocol::CHANNEL_CCSDS; c0.messageId = 8;
    Segment c1 = a; c1.channel = Protocol::CHANNEL_PAYLOAD; c1.messageId = 9; c1.count = 1;
    assert(channels.accept(c0, 600, frame) == Reassembler::Result::NONE);
    assert(channels.accept(c1, 601, frame) == Reassembler::Result::COMPLETE);
    assert(frame.channel == Protocol::CHANNEL_PAYLOAD);
    Segment c0last = b; c0last.channel = Protocol::CHANNEL_CCSDS; c0last.messageId = 8;
    assert(channels.accept(c0last, 602, frame) == Reassembler::Result::COMPLETE);
    assert(frame.channel == Protocol::CHANNEL_CCSDS);

    RadioStateMachine radio;
    assert(radio.boot(true) == RadioAction::SAFE_OFF && radio.fault() == RadioFault::WATCHDOG_RESET);
    assert(radio.setEnabled(true) == RadioAction::START_INIT);
    assert(radio.initComplete(true) == RadioAction::ENTER_RX && radio.state() == RadioState::READY);
    assert(radio.requestTx() == RadioAction::START_TX);
    assert(radio.txComplete(false) == RadioAction::RECOVER_FIFO && radio.fault() == RadioFault::LOCAL_TX);
    assert(radio.recoveryComplete(true) == RadioAction::ENTER_RX && radio.fault() == RadioFault::LOCAL_TX);
    assert(radio.requestTx() == RadioAction::START_TX);
    assert(radio.txComplete(true) == RadioAction::ENTER_RX && radio.fault() == RadioFault::NONE);
    assert(radio.requestTx() == RadioAction::START_TX);
    assert(radio.txComplete(false) == RadioAction::RECOVER_FIFO);
    assert(radio.recoveryComplete(false) == RadioAction::SAFE_OFF && radio.state() == RadioState::OFF);
    assert(radio.setEnabled(true) == RadioAction::START_INIT && radio.initAttempts() == 2);
    assert(radio.initComplete(false) == RadioAction::SAFE_OFF && radio.fault() == RadioFault::INIT_FAILED);
}
