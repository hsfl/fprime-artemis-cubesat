#include "SatelliteController/LocalServices.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace SatelliteController;

namespace {

struct FakePduTransport final : PduTransport {
    std::array<std::uint8_t, 256> rx{};
    std::array<std::uint8_t, 128> written{};
    std::size_t rxHead = 0;
    std::size_t rxTail = 0;
    std::size_t writtenLength = 0;
    void discardRx() override { rxHead = rxTail; }
    bool write(const std::uint8_t* data, std::size_t length) override {
        if (length > written.size()) return false;
        for (std::size_t i = 0; i < length; ++i) written[i] = data[i];
        writtenLength = length;
        return true;
    }
    void flush() override {}
    bool readByte(std::uint8_t& value) override {
        if (rxHead == rxTail) return false;
        value = rx[rxHead++];
        return true;
    }
    void push(const std::uint8_t* data, std::size_t length) {
        for (std::size_t i = 0; i < length; ++i) rx[rxTail++] = data[i];
    }
};

struct FakeRadio final : RfStatusProvider {
    RfStatusSnapshot snapshot{};
    bool enabledResult = true;
    bool requested = false;
    bool setEnabled(bool enabled) override {
        requested = enabled;
        snapshot.state = enabled ? RadioState::READY : RadioState::OFF;
        return enabledResult;
    }
    RfStatusSnapshot status() const override { return snapshot; }
};

std::size_t localFrame(std::uint8_t target,
                       std::uint8_t requestId,
                       const std::uint8_t* body,
                       std::size_t bodyLength,
                       std::array<std::uint8_t, Generated::FRAME_MAX_PAYLOAD>& frame) {
    frame[0] = target;
    frame[1] = requestId;
    frame[2] = static_cast<std::uint8_t>(bodyLength);
    frame[3] = 0;
    for (std::size_t i = 0; i < bodyLength; ++i) frame[4 + i] = body[i];
    return 4 + bodyLength;
}

std::size_t poll(LocalServicesRouter& router,
                 std::uint32_t nowMs,
                 std::array<std::uint8_t, Generated::FRAME_MAX_PAYLOAD>& response) {
    std::size_t length = 0;
    const bool ready = router.pollLocalResponse(nowMs, response.data(), response.size(), length);
    return ready ? length : 0;
}

void testRfAndPdu() {
    FakeRadio radio;
    radio.snapshot.lastRssiDbm = -70;
    radio.snapshot.rxGood = 4;
    radio.snapshot.rfRxPackets = 9;
    radio.snapshot.bootFlags = 1;
    FakePduTransport transport;
    PduProxy pdu(transport);
    static PayloadCacheService cache;
    static PreviewService preview;
    LocalServicesRouter router(radio, pdu, cache, preview);
    std::array<std::uint8_t, Generated::FRAME_MAX_PAYLOAD> request{};
    std::array<std::uint8_t, Generated::FRAME_MAX_PAYLOAD> response{};

    const std::uint8_t statusBody[] = {LocalProtocol::RF_OP_STATUS};
    const std::size_t statusLength = localFrame(LocalProtocol::TARGET_RF_STATUS, 3, statusBody, sizeof(statusBody), request);
    assert(router.beginLocalFrame(request.data(), statusLength));
    assert(poll(router, 0, response) == 37);
    assert(response[0] == LocalProtocol::TARGET_RF_STATUS && response[1] == 3 && response[3] == 33);
    assert(response[5] == static_cast<std::uint8_t>(-70));

    const std::uint8_t enableBody[] = {LocalProtocol::RF_OP_SET_ENABLED, 1};
    const std::size_t enableLength = localFrame(LocalProtocol::TARGET_RF_STATUS, 4, enableBody, sizeof(enableBody), request);
    assert(router.beginLocalFrame(request.data(), enableLength));
    assert(poll(router, 0, response) == 8);
    assert(radio.requested && response[2] == LocalProtocol::STATUS_OK && response[5] == 1);

    const std::uint8_t pduRequest[] = {0xA5, 1, 0, 2, 8, 0, 0, 0, 0};
    const std::size_t pduLength = localFrame(LocalProtocol::TARGET_PDU, 8, pduRequest, sizeof(pduRequest), request);
    assert(router.beginLocalFrame(request.data(), pduLength));
    assert(transport.writtenLength == sizeof(pduRequest));
    assert(poll(router, 100, response) == 0);
    const std::uint8_t pduResponse[] = {0xA5, 1, 1, 2, 8, 0, 0, 0, 0};
    transport.push(pduResponse, sizeof(pduResponse));
    assert(poll(router, 449, response) == 13);
    assert(response[0] == LocalProtocol::TARGET_PDU && response[1] == 8 && response[2] == LocalProtocol::STATUS_OK &&
           response[3] == sizeof(pduResponse));

    assert(router.beginLocalFrame(request.data(), pduLength, 1000));
    const std::uint8_t malformedConcurrent[] = {LocalProtocol::TARGET_PDU, 9};
    assert(pdu.beginLocalFrame(malformedConcurrent, sizeof(malformedConcurrent), 1001));
    assert(poll(router, 1001, response) == 4 && response[1] == 9 &&
           response[2] == LocalProtocol::STATUS_BUSY);
    assert(router.beginLocalFrame(request.data(), pduLength, 1000));
    assert(poll(router, 1000, response) == 4 && response[2] == LocalProtocol::STATUS_BUSY);
    assert(poll(router, 1350, response) == 4 && response[2] == LocalProtocol::STATUS_TIMEOUT);
}

void testCacheAndPreview() {
    FakeRadio radio;
    FakePduTransport transport;
    PduProxy pdu(transport);
    static PayloadCacheService cache;
    static PreviewService preview;
    LocalServicesRouter router(radio, pdu, cache, preview);
    std::array<std::uint8_t, Generated::FRAME_MAX_PAYLOAD> request{};
    std::array<std::uint8_t, Generated::FRAME_MAX_PAYLOAD> response{};
    std::array<std::uint8_t, Generated::RF_SEGMENT_MAX_DATA> packet{};
    const std::uint8_t data[] = {1, 2, 3, 4};
    const std::uint16_t dataCrc = crc16Ccitt(data, sizeof(data));
    std::uint8_t beginBody[12] = {LocalProtocol::CACHE_OP_BEGIN, 7, 0x44, 0x33, 0x22, 0x11,
                                  4, 0, 0, 0, static_cast<std::uint8_t>(dataCrc), static_cast<std::uint8_t>(dataCrc >> 8U)};
    std::size_t length = localFrame(LocalProtocol::TARGET_PAYLOAD_CACHE, 1, beginBody, sizeof(beginBody), request);
    assert(router.beginLocalFrame(request.data(), length));
    assert(poll(router, 0, response) == 12 && response[2] == LocalProtocol::STATUS_OK);

    std::uint8_t chunkBody[11] = {LocalProtocol::CACHE_OP_CHUNK, 7, 0, 0, 0, 0, 4, 1, 2, 3, 4};
    length = localFrame(LocalProtocol::TARGET_PAYLOAD_CACHE, 2, chunkBody, sizeof(chunkBody), request);
    assert(router.beginLocalFrame(request.data(), length));
    assert(poll(router, 0, response) == 12 && response[2] == LocalProtocol::STATUS_OK && response[8] == 4);
    assert(router.beginLocalFrame(request.data(), length));
    assert(poll(router, 0, response) == 12 && response[2] == LocalProtocol::STATUS_OK);

    std::uint8_t badChunk[8] = {LocalProtocol::CACHE_OP_CHUNK, 7, 0xFF, 0xFF, 0xFF, 0xFF, 1, 9};
    length = localFrame(LocalProtocol::TARGET_PAYLOAD_CACHE, 3, badChunk, sizeof(badChunk), request);
    assert(router.beginLocalFrame(request.data(), length));
    assert(poll(router, 0, response) == 12 && response[2] == LocalProtocol::STATUS_BAD_REQUEST);

    const std::uint8_t commitBody[] = {LocalProtocol::CACHE_OP_COMMIT_AND_SEND, 7};
    length = localFrame(LocalProtocol::TARGET_PAYLOAD_CACHE, 4, commitBody, sizeof(commitBody), request);
    assert(router.beginLocalFrame(request.data(), length));
    assert(poll(router, 0, response) == 12 && response[2] == LocalProtocol::STATUS_OK);
    for (int i = 0; i < 3; ++i) {
        std::size_t packetLength = 0;
        assert(cache.nextPayloadPacket(packet.data(), packet.size(), packetLength));
        assert(packet[2] == LocalProtocol::PAYLOAD_PACKET_HEADER && packetLength == 17);
        cache.payloadPacketSent(true);
    }
    std::size_t packetLength = 0;
    assert(cache.nextPayloadPacket(packet.data(), packet.size(), packetLength) && packet[2] == LocalProtocol::PAYLOAD_PACKET_DATA);
    cache.payloadPacketSent(true);
    assert(cache.nextPayloadPacket(packet.data(), packet.size(), packetLength) && packet[2] == LocalProtocol::PAYLOAD_PACKET_END);
    cache.payloadPacketSent(true);
    assert(poll(router, 0, response) == 12 && response[2] == LocalProtocol::STATUS_OK && response[6] == LocalProtocol::CACHE_STATE_READY);

    const std::uint8_t retry[] = {LocalProtocol::PAYLOAD_MAGIC_0, LocalProtocol::PAYLOAD_MAGIC_1,
                                  LocalProtocol::PAYLOAD_PACKET_RETRY_REQUEST, 7, 0, 0, 1, 1};
    assert(cache.handlePayloadControl(retry, sizeof(retry)));
    assert(cache.nextPayloadPacket(packet.data(), packet.size(), packetLength) && packet[2] == LocalProtocol::PAYLOAD_PACKET_DATA);
    cache.payloadPacketSent(true);
    assert(cache.nextPayloadPacket(packet.data(), packet.size(), packetLength) && packet[2] == LocalProtocol::PAYLOAD_PACKET_END);
    cache.payloadPacketSent(true);
    assert(poll(router, 0, response) == 12);

    std::array<std::uint8_t, LocalProtocol::PREVIEW_MAX_FRAME_BYTES> zeros{};
    const std::uint16_t zeroCrc = crc16Ccitt(zeros.data(), zeros.size());
    std::uint8_t previewBegin[14] = {LocalProtocol::PREVIEW_OP_BEGIN, 1, 0, 1, 0, 0, 0,
                                     LocalProtocol::PREVIEW_WIDTH, LocalProtocol::PREVIEW_HEIGHT,
                                     LocalProtocol::PREVIEW_PIXEL_FORMAT_U8, 0xC0, 0x12,
                                     static_cast<std::uint8_t>(zeroCrc), static_cast<std::uint8_t>(zeroCrc >> 8U)};
    length = localFrame(LocalProtocol::TARGET_LEPTON_PREVIEW, 9, previewBegin, sizeof(previewBegin), request);
    assert(router.beginLocalFrame(request.data(), length));
    assert(poll(router, 0, response) == 12 && response[2] == LocalProtocol::STATUS_OK);
    length = localFrame(LocalProtocol::TARGET_PAYLOAD_CACHE, 10, beginBody, sizeof(beginBody), request);
    assert(router.beginLocalFrame(request.data(), length));
    assert(poll(router, 0, response) == 12 && response[0] == LocalProtocol::TARGET_PAYLOAD_CACHE &&
           response[2] == LocalProtocol::STATUS_BUSY);
}

void testWatchdog() {
    WatchdogModel watchdog;
    watchdog.boot(true, 100);
    assert(watchdog.watchdogReset() && !watchdog.expired(12099) && watchdog.expired(12100));
    watchdog.feed(12100);
    assert(!watchdog.expired(24099) && watchdog.expired(24100));
}

}  // namespace

int main() {
    testRfAndPdu();
    testCacheAndPreview();
    testWatchdog();
    return 0;
}
