#include "SatelliteController/RuntimeBridge.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

using namespace SatelliteController;

struct IoCapture {
    std::vector<std::vector<std::uint8_t>> rf;
    std::vector<std::vector<std::uint8_t>> uart;
    std::uint32_t completions = 0;
    bool lastCompletionSent = false;
};

static bool consumePayloadControl(void* context, const Frame& message) {
    auto& capture = *static_cast<IoCapture*>(context);
    if (message.channel != Protocol::CHANNEL_PAYLOAD || message.length != 1 ||
        message.payload[0] != 0xA5) return false;
    capture.uart.emplace_back(1, 0xCC);
    return true;
}

static bool captureRf(void* context, const std::uint8_t* bytes, std::size_t length) {
    auto& capture = *static_cast<IoCapture*>(context);
    capture.rf.emplace_back(bytes, bytes + length);
    return true;
}

static bool failRf(void*, const std::uint8_t*, std::size_t) { return false; }

static void captureCompletion(void* context, RuntimeBridge::TxTag, bool sent) {
    auto& capture = *static_cast<IoCapture*>(context);
    ++capture.completions;
    capture.lastCompletionSent = sent;
}

static bool captureUart(void* context, const std::uint8_t* bytes, std::size_t length) {
    auto& capture = *static_cast<IoCapture*>(context);
    capture.uart.emplace_back(bytes, bytes + length);
    return true;
}

static bool localEcho(void*, const Frame& request, Frame& response) {
    response = request;
    response.payload[request.length] = 0xEE;
    response.length = static_cast<std::uint16_t>(request.length + 1U);
    return true;
}

static std::vector<std::uint8_t> makeUartFrame(std::uint8_t channel,
                                                const std::vector<std::uint8_t>& payload) {
    const std::uint16_t crc = crc16Ccitt(payload.data(), payload.size());
    std::vector<std::uint8_t> frame{Protocol::FRAME_MAGIC_0, Protocol::FRAME_MAGIC_1, channel,
                                    static_cast<std::uint8_t>(payload.size()),
                                    static_cast<std::uint8_t>(payload.size() >> 8U)};
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(static_cast<std::uint8_t>(crc));
    frame.push_back(static_cast<std::uint8_t>(crc >> 8U));
    return frame;
}

static void feedUart(RuntimeBridge& bridge,
                     std::uint8_t channel,
                     const std::vector<std::uint8_t>& payload,
                     std::uint32_t startMs) {
    const auto frame = makeUartFrame(channel, payload);
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const ParseResult result = bridge.ingestUartByte(frame[i], startMs + static_cast<std::uint32_t>(i));
        assert(result != ParseResult::FRAMING_DROP && result != ParseResult::CRC_DROP &&
               result != ParseResult::TIMEOUT);
    }
}

static std::vector<std::uint8_t> makeSegment(const Segment& segment) {
    std::uint8_t packet[Protocol::RF_PACKET_MAX_LEN]{};
    const std::size_t length = encodeSegment(segment, packet);
    assert(length != 0);
    return std::vector<std::uint8_t>(packet, packet + length);
}

int main() {
    IoCapture uplinkIo;
    RuntimeBridge uplink({&uplinkIo, captureUart, captureRf, localEcho});
    feedUart(uplink, Protocol::CHANNEL_CCSDS, {1, 2, 3}, 0);
    assert(uplink.pendingRfMessages() == 1);
    uplink.poll(0);
    assert(uplinkIo.rf.size() == 1 && uplinkIo.rf[0][0] == Protocol::RF_MAGIC_CCSDS &&
           uplinkIo.rf[0][1] == 0 && uplinkIo.rf[0][2] == 0 && uplinkIo.rf[0][3] == 1 &&
           uplinkIo.rf[0][4] == 3);

    // IDs are per RF channel, and ch2 invokes only the local callback.
    feedUart(uplink, Protocol::CHANNEL_PAYLOAD, {4, 5}, 10);
    uplink.poll(15);
    assert(uplinkIo.rf.size() == 2 && uplinkIo.rf[1][0] == Protocol::RF_MAGIC_PAYLOAD &&
           uplinkIo.rf[1][1] == 0);
    const std::uint8_t locallyGeneratedPayload[] = {0xB0, 0xB1};
    assert(uplink.queueRfMessage(Protocol::CHANNEL_PAYLOAD,
                                 locallyGeneratedPayload,
                                 sizeof(locallyGeneratedPayload)));
    assert(!uplink.queueRfMessage(Protocol::CHANNEL_LOCAL, locallyGeneratedPayload,
                                  sizeof(locallyGeneratedPayload)));
    uplink.poll(30);
    assert(uplinkIo.rf.size() == 3 && uplinkIo.rf[2][0] == Protocol::RF_MAGIC_PAYLOAD &&
           uplinkIo.rf[2][1] == 1);
    feedUart(uplink, Protocol::CHANNEL_LOCAL, {6}, 40);
    assert(uplink.pendingRfMessages() == 0 && uplink.pendingUartFrames() == 1);
    uplink.poll(50);
    assert(uplinkIo.uart.size() == 1 && uplinkIo.uart[0][2] == Protocol::CHANNEL_LOCAL &&
           uplinkIo.uart[0][5] == 6 && uplinkIo.uart[0][6] == 0xEE);
    const std::uint8_t asynchronousResponse[] = {0xA0, 0xA1};
    assert(uplink.queueLocalResponse(asynchronousResponse, sizeof(asynchronousResponse)));
    uplink.poll(31);
    assert(uplinkIo.uart.size() == 2 && uplinkIo.uart[1][2] == Protocol::CHANNEL_LOCAL &&
           uplinkIo.uart[1][5] == 0xA0 && uplinkIo.uart[1][6] == 0xA1);

    // Satellite RX ch0 ACKs every accepted segment and re-ACKs duplicates,
    // while completed data is delivered to UART only once.
    IoCapture downlinkIo;
    RuntimeBridge downlink({&downlinkIo, captureUart, captureRf, nullptr});
    Segment first{};
    first.channel = Protocol::CHANNEL_CCSDS;
    first.messageId = 7;
    first.index = 0;
    first.count = 2;
    first.length = 1;
    first.data[0] = 0x10;
    Segment second = first;
    second.index = 1;
    second.data[0] = 0x11;
    const auto firstPacket = makeSegment(first);
    const auto secondPacket = makeSegment(second);
    assert(downlink.ingestRfPacket(firstPacket.data(), firstPacket.size(), 0) ==
           RuntimeBridge::RfResult::SEGMENT);
    assert(downlink.ingestRfPacket(firstPacket.data(), firstPacket.size(), 1) == RuntimeBridge::RfResult::DROP);
    assert(downlink.duplicateDrops() == 1 && downlink.pendingAcks() == 0);
    assert(downlinkIo.rf.size() == 2 &&
           downlinkIo.rf[0] == std::vector<std::uint8_t>({Protocol::RF_MAGIC_CCSDS, 7,
                                                           Protocol::RF_ACK_INDEX, 0, 0}));
    assert(downlink.ingestRfPacket(secondPacket.data(), secondPacket.size(), 1) ==
           RuntimeBridge::RfResult::COMPLETE);
    downlink.poll(1);
    assert(downlinkIo.uart.size() == 1);
    assert(downlink.ingestRfPacket(secondPacket.data(), secondPacket.size(), 2) ==
           RuntimeBridge::RfResult::DROP);
    downlink.poll(2);
    assert(downlinkIo.rf.size() == 4 &&
           downlinkIo.rf.back() == std::vector<std::uint8_t>({Protocol::RF_MAGIC_CCSDS, 7,
                                                                Protocol::RF_ACK_INDEX, 1, 0}));

    // Both RF channels retain independent assembly state.
    IoCapture interleaveIo;
    RuntimeBridge interleave({&interleaveIo, captureUart, captureRf, nullptr});
    Segment c0 = first;
    c0.messageId = 8;
    Segment c1 = second;
    c1.messageId = 8;
    Segment payload = first;
    payload.channel = Protocol::CHANNEL_PAYLOAD;
    payload.messageId = 9;
    payload.count = 1;
    const auto c0Packet = makeSegment(c0);
    const auto c1Packet = makeSegment(c1);
    const auto payloadPacket = makeSegment(payload);
    assert(interleave.ingestRfPacket(c0Packet.data(), c0Packet.size(), 0) ==
           RuntimeBridge::RfResult::SEGMENT);
    assert(interleave.ingestRfPacket(payloadPacket.data(), payloadPacket.size(), 1) ==
           RuntimeBridge::RfResult::COMPLETE);
    assert(interleave.ingestRfPacket(c1Packet.data(), c1Packet.size(), 2) ==
           RuntimeBridge::RfResult::COMPLETE);
    assert(interleave.pendingUartFrames() == 2 && interleave.pendingAcks() == 0 &&
           interleaveIo.rf.size() == 2);

    // Completed channel-1 control records can be consumed locally (payload
    // retry/repair) instead of being incorrectly forwarded to the Pi.
    IoCapture controlIo;
    RuntimeBridge control({&controlIo, captureUart, captureRf, nullptr, consumePayloadControl});
    Segment controlSegment{};
    controlSegment.channel = Protocol::CHANNEL_PAYLOAD;
    controlSegment.messageId = 10;
    controlSegment.index = 0;
    controlSegment.count = 1;
    controlSegment.length = 1;
    controlSegment.data[0] = 0xA5;
    const auto controlPacket = makeSegment(controlSegment);
    assert(control.ingestRfPacket(controlPacket.data(), controlPacket.size(), 3) ==
           RuntimeBridge::RfResult::COMPLETE);
    assert(control.pendingUartFrames() == 0 && controlIo.uart.size() == 1);

    IoCapture taggedIo;
    RuntimeBridge tagged({&taggedIo, captureUart, captureRf, nullptr, nullptr, captureCompletion});
    const std::uint8_t taggedPayload[] = {0x22};
    assert(tagged.queueRfMessage(Protocol::CHANNEL_PAYLOAD, taggedPayload, sizeof(taggedPayload),
                                 RuntimeBridge::TxTag::PAYLOAD_CACHE));
    tagged.poll(0);
    assert(tagged.pendingRfMessages() == 0 && taggedIo.completions == 1 &&
           taggedIo.lastCompletionSent);

    IoCapture failedIo;
    RuntimeBridge failed({&failedIo, captureUart, failRf, nullptr, nullptr, captureCompletion});
    assert(failed.queueRfMessage(Protocol::CHANNEL_PAYLOAD, taggedPayload, sizeof(taggedPayload),
                                 RuntimeBridge::TxTag::PAYLOAD_CACHE));
    failed.poll(0);
    assert(failed.pendingRfMessages() == 0 && failedIo.completions == 1 &&
           !failedIo.lastCompletionSent);

    // Expiry is wrap-safe and the packet which observes expiry can begin a
    // fresh message instead of being discarded.
    RuntimeBridge timeoutBridge({&interleaveIo, captureUart, captureRf, nullptr});
    Segment stale = c0;
    stale.messageId = 20;
    const auto stalePacket = makeSegment(stale);
    assert(timeoutBridge.ingestRfPacket(stalePacket.data(), stalePacket.size(), 0) ==
           RuntimeBridge::RfResult::SEGMENT);
    Segment fresh = payload;
    fresh.channel = Protocol::CHANNEL_CCSDS;
    fresh.messageId = 21;
    const auto freshPacket = makeSegment(fresh);
    assert(timeoutBridge.ingestRfPacket(freshPacket.data(), freshPacket.size(), 501) ==
           RuntimeBridge::RfResult::COMPLETE);
    assert(timeoutBridge.timeoutEvents() == 1 && timeoutBridge.pendingUartFrames() == 1);

    RuntimeBridge malformedRf;
    const std::uint8_t badMagic[] = {0x00, 1, 0, 1, 1};
    assert(malformedRf.ingestRfPacket(badMagic, sizeof(badMagic), 0) == RuntimeBridge::RfResult::DROP);
    const std::uint8_t badLength[] = {Protocol::RF_MAGIC_CCSDS, 1, 0, 1, 2, 0xAA};
    assert(malformedRf.ingestRfPacket(badLength, sizeof(badLength), 0) == RuntimeBridge::RfResult::DROP);
    const std::uint8_t ignoredAck[] = {Protocol::RF_MAGIC_CCSDS, 1, Protocol::RF_ACK_INDEX, 0, 0, 0xFF};
    assert(malformedRf.ingestRfPacket(ignoredAck, sizeof(ignoredAck), 0) == RuntimeBridge::RfResult::NONE);
    assert(malformedRf.framingDrops() == 2);

    RuntimeBridge gapBridge;
    Segment gapFirst = fresh;
    gapFirst.messageId = 30;
    const auto gapFirstPacket = makeSegment(gapFirst);
    assert(gapBridge.ingestRfPacket(gapFirstPacket.data(), gapFirstPacket.size(), 0) ==
           RuntimeBridge::RfResult::COMPLETE);
    Segment gapSecond = gapFirst;
    gapSecond.messageId = 32;
    const auto gapSecondPacket = makeSegment(gapSecond);
    assert(gapBridge.ingestRfPacket(gapSecondPacket.data(), gapSecondPacket.size(), 1) ==
           RuntimeBridge::RfResult::COMPLETE);
    assert(gapBridge.messageIdGaps() == 1);

    // The largest UART frame is segmented into five legal 44-byte packets.
    IoCapture maximumIo;
    RuntimeBridge maximum({&maximumIo, captureUart, captureRf, nullptr});
    std::vector<std::uint8_t> maximumPayload(Protocol::FRAME_MAX_PAYLOAD, 0x5A);
    feedUart(maximum, Protocol::CHANNEL_CCSDS, maximumPayload, 0);
    assert(maximum.pendingRfMessages() == 1);
    maximum.poll(0);
    maximum.poll(8);
    maximum.poll(16);
    maximum.poll(24);
    maximum.poll(32);
    assert(maximum.pendingRfMessages() == 0 && maximumIo.rf.size() == 5);
    for (std::size_t i = 0; i < maximumIo.rf.size(); ++i) {
        assert(maximumIo.rf[i].size() == 49U);
        assert(maximumIo.rf[i][2] == i);
        assert(maximumIo.rf[i][3] == 5);
    }

    RuntimeBridge wrapBridge;
    assert(wrapBridge.ingestUartByte(Protocol::FRAME_MAGIC_0, 0xFFFFFFF0U) == ParseResult::NONE);
    assert(wrapBridge.ingestUartByte(Protocol::FRAME_MAGIC_1, 0xFFFFFFF5U) == ParseResult::NONE);
    assert(wrapBridge.ingestUartByte(Protocol::CHANNEL_LOCAL, 0x00000005U) == ParseResult::NONE);

    // Malformed input is bounded and observable, never routed as RF data.
    RuntimeBridge malformed;
    const auto valid = makeUartFrame(Protocol::CHANNEL_CCSDS, {0x42});
    for (std::size_t i = 0; i < valid.size(); ++i) {
        std::uint8_t byte = valid[i];
        if (i == valid.size() - 1U) byte ^= 1U;
        malformed.ingestUartByte(byte, static_cast<std::uint32_t>(i));
    }
    assert(malformed.crcDrops() == 1);
    assert(malformed.ingestUartByte(Protocol::FRAME_MAGIC_0, 100) == ParseResult::NONE);
    assert(malformed.ingestUartByte(Protocol::FRAME_MAGIC_1, 351) == ParseResult::TIMEOUT);

    // A full bounded queue drops the ninth message without allocating.
    RuntimeBridge bounded;
    for (std::uint32_t i = 0; i < RuntimeBridge::QUEUE_DEPTH + 1U; ++i) {
        feedUart(bounded, Protocol::CHANNEL_CCSDS, {static_cast<std::uint8_t>(i)}, i * 10U);
    }
    assert(bounded.pendingRfMessages() == RuntimeBridge::QUEUE_DEPTH);
    assert(bounded.queueDrops() == 1);

    RuntimeBridge disabled;
    disabled.setRfEnabled(false);
    feedUart(disabled, Protocol::CHANNEL_CCSDS, {0x55}, 0);
    assert(disabled.pendingRfMessages() == 0 && disabled.queueDrops() == 1);
    return 0;
}
