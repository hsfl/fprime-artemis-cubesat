#pragma once

#include "SatelliteController/Protocol.hpp"

#include <cstddef>
#include <cstdint>

namespace SatelliteController {

/**
 * Allocation-free transport boundary for the satellite controller.
 *
 * The bridge deliberately knows only about byte-oriented UART and RF
 * callbacks.  Zephyr drivers (and, later, F' ports) can provide those
 * callbacks without pulling hardware or RTOS types into the protocol core.
 */
class RuntimeBridge final {
  public:
    static constexpr std::size_t QUEUE_DEPTH = 8;
    enum class TxTag : std::uint8_t { NONE, PAYLOAD_CACHE, PREVIEW };

    using UartWrite = bool (*)(void* context, const std::uint8_t* bytes, std::size_t length);
    using RfSend = bool (*)(void* context, const std::uint8_t* bytes, std::size_t length);
    using LocalHandler = bool (*)(void* context, const Frame& request, Frame& response);
    using RfMessageHandler = bool (*)(void* context, const Frame& message);
    using RfCompletionHandler = void (*)(void* context, TxTag tag, bool sent);
    using ClockNow = std::uint32_t (*)(void* context);

    struct Callbacks {
        void* context;
        UartWrite uartWrite;
        RfSend rfSend;
        LocalHandler localHandler;
        RfMessageHandler rfMessageHandler = nullptr;
        RfCompletionHandler rfCompletionHandler = nullptr;
        ClockNow clockNow = nullptr;
    };

    enum class RfResult : std::uint8_t { NONE, SEGMENT, COMPLETE, ACK, DROP, TIMEOUT };

    RuntimeBridge();
    explicit RuntimeBridge(Callbacks callbacks);

    // Feed one Pi UART byte. A completed frame is routed to RF (ch0/ch1) or
    // the local callback (ch2). The parser result is returned to aid counters.
    ParseResult ingestUartByte(std::uint8_t byte, std::uint32_t nowMs);

    // Feed one received RFM23 packet after its hardware header has been
    // classified. The bridge validates segment framing and reassembles each
    // RF channel independently.
    RfResult ingestRfPacket(const std::uint8_t* packet,
                            std::size_t packetLength,
                            std::uint32_t nowMs);

    // Queue an asynchronous channel-2 response (for example, a PDU reply
    // which completes after its UART transaction deadline). The response is
    // framed and written by poll(), never from an interrupt callback.
    bool queueLocalResponse(const std::uint8_t* payload, std::size_t length);

    // Queue a locally generated RF message (for example, a payload-cache
    // packet) without synthesizing a UART frame first.
    bool queueRfMessage(std::uint8_t channel,
                        const std::uint8_t* payload,
                        std::size_t length,
                        TxTag tag = TxTag::NONE);

    // Make bounded progress on queued UART/RF work. No callback is invoked
    // from an interrupt by this class; call poll from a Zephyr work item or
    // the controller thread.
    void poll(std::uint32_t nowMs, bool allowRfTx = true);

    void setRfEnabled(bool enabled) { m_rfEnabled = enabled; }
    void discardRadioWork();

    void reset();

    std::size_t pendingRfMessages() const { return m_rfCount; }
    std::size_t pendingUartFrames() const { return m_uartCount; }
    std::size_t pendingAcks() const { return m_ackCount; }

    std::uint32_t framingDrops() const { return m_framingDrops; }
    std::uint32_t crcDrops() const { return m_crcDrops; }
    std::uint32_t timeoutEvents() const { return m_timeoutEvents; }
    std::uint32_t reassemblyDrops() const { return m_reassemblyDrops; }
    std::uint32_t duplicateDrops() const { return m_duplicateDrops; }
    std::uint32_t messageIdGaps() const { return m_messageIdGaps; }
    std::uint32_t queueDrops() const { return m_queueDrops; }
    std::uint32_t ackTx() const { return m_ackTx; }
    std::uint32_t ackRx() const { return m_ackRx; }
    std::uint32_t ackTimeouts() const { return m_ackTimeouts; }
    std::uint32_t ackRetries() const { return m_ackRetries; }

  private:
    struct TxMessage {
        Frame frame{};
        std::uint8_t messageId = 0;
        std::uint8_t segmentCount = 0;
        std::uint8_t nextIndex = 0;
        bool active = false;
        bool awaitingAck = false;
        std::uint8_t retries = 0;
        std::uint32_t ackDeadlineMs = 0;
        TxTag tag = TxTag::NONE;
    };

    struct RxAssembly {
        bool active = false;
        bool seenCompleted = false;
        std::uint8_t messageId = 0;
        std::uint8_t nextIndex = 0;
        std::uint8_t segmentCount = 0;
        std::uint16_t length = 0;
        std::uint32_t lastSegmentMs = 0;
        StaticBuffer<std::uint8_t, Protocol::FRAME_MAX_PAYLOAD> buffer{};
    };

    struct Ack {
        std::uint8_t channel = 0;
        std::uint8_t messageId = 0;
        std::uint8_t index = 0;
    };

    static bool txAckRequired(std::uint8_t channel);
    static bool rxAckRequired(std::uint8_t channel);
    static bool elapsed(std::uint32_t nowMs, std::uint32_t thenMs, std::uint32_t durationMs);
    static bool reached(std::uint32_t nowMs, std::uint32_t thenMs, std::uint32_t durationMs);

    void routeFrame(const Frame& frame);
    bool enqueueRf(const Frame& frame, TxTag tag = TxTag::NONE);
    bool enqueueUart(const Frame& frame);
    bool enqueueAck(std::uint8_t channel, std::uint8_t messageId, std::uint8_t index);
    void serviceRf(std::uint32_t nowMs);
    void serviceUart();
    void serviceAcks();
    RfResult acceptSegment(const Segment& segment, std::uint32_t nowMs);
    void resetAssembly(RxAssembly& assembly);
    bool sendSegment(TxMessage& message, std::uint32_t nowMs);
    bool sendAck(const Ack& ack);
    bool sendUartFrame(const Frame& frame);
    void removeRfHead();
    void removeUartHead();
    void removeAckHead();
    void notifyCompletion(const TxMessage& message, bool sent);

    Callbacks m_callbacks;
    FrameParser m_uartParser{};
    StaticBuffer<TxMessage, QUEUE_DEPTH> m_rfQueue{};
    StaticBuffer<Frame, QUEUE_DEPTH> m_uartQueue{};
    StaticBuffer<Ack, QUEUE_DEPTH> m_ackQueue{};
    StaticBuffer<RxAssembly, Protocol::RF_CHANNEL_COUNT> m_rx{};
    StaticBuffer<std::uint8_t, Protocol::RF_CHANNEL_COUNT> m_nextMessageId{};
    std::size_t m_rfHead = 0;
    std::size_t m_rfCount = 0;
    std::size_t m_uartHead = 0;
    std::size_t m_uartCount = 0;
    std::size_t m_ackHead = 0;
    std::size_t m_ackCount = 0;
    bool m_haveRfTxTime = false;
    bool m_rfEnabled = true;
    std::uint32_t m_lastRfTxMs = 0;
    std::uint32_t m_framingDrops = 0;
    std::uint32_t m_crcDrops = 0;
    std::uint32_t m_timeoutEvents = 0;
    std::uint32_t m_reassemblyDrops = 0;
    std::uint32_t m_duplicateDrops = 0;
    std::uint32_t m_messageIdGaps = 0;
    std::uint32_t m_queueDrops = 0;
    std::uint32_t m_ackTx = 0;
    std::uint32_t m_ackRx = 0;
    std::uint32_t m_ackTimeouts = 0;
    std::uint32_t m_ackRetries = 0;
};

}  // namespace SatelliteController
