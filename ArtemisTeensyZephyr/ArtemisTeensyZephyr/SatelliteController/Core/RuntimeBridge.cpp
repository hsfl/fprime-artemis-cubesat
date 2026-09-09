#include "SatelliteController/RuntimeBridge.hpp"

namespace SatelliteController {

RuntimeBridge::RuntimeBridge() : m_callbacks{nullptr, nullptr, nullptr, nullptr} {}

RuntimeBridge::RuntimeBridge(Callbacks callbacks) : m_callbacks(callbacks) {}

bool RuntimeBridge::txAckRequired(std::uint8_t channel) {
    if (channel == Protocol::CHANNEL_CCSDS) return Protocol::TX_ACK_REQUIRED_CCSDS;
    if (channel == Protocol::CHANNEL_PAYLOAD) return Protocol::TX_ACK_REQUIRED_PAYLOAD;
    return false;
}

bool RuntimeBridge::rxAckRequired(std::uint8_t channel) {
    if (channel == Protocol::CHANNEL_CCSDS) return Protocol::RX_ACK_REQUIRED_CCSDS;
    if (channel == Protocol::CHANNEL_PAYLOAD) return Protocol::RX_ACK_REQUIRED_PAYLOAD;
    return false;
}

bool RuntimeBridge::elapsed(std::uint32_t nowMs,
                            std::uint32_t thenMs,
                            std::uint32_t durationMs) {
    return static_cast<std::uint32_t>(nowMs - thenMs) > durationMs;
}

bool RuntimeBridge::reached(std::uint32_t nowMs,
                            std::uint32_t thenMs,
                            std::uint32_t durationMs) {
    return static_cast<std::uint32_t>(nowMs - thenMs) >= durationMs;
}

ParseResult RuntimeBridge::ingestUartByte(std::uint8_t byte, std::uint32_t nowMs) {
    Frame frame{};
    const ParseResult result = m_uartParser.consume(byte, nowMs, frame);
    if (result == ParseResult::FRAME) {
        routeFrame(frame);
    } else if (result == ParseResult::FRAMING_DROP) {
        ++m_framingDrops;
    } else if (result == ParseResult::CRC_DROP) {
        ++m_crcDrops;
    } else if (result == ParseResult::TIMEOUT) {
        ++m_timeoutEvents;
    }
    return result;
}

void RuntimeBridge::routeFrame(const Frame& frame) {
    if (frame.channel == Protocol::CHANNEL_CCSDS || frame.channel == Protocol::CHANNEL_PAYLOAD) {
        (void)enqueueRf(frame);
        return;
    }
    if (frame.channel != Protocol::CHANNEL_LOCAL || m_callbacks.localHandler == nullptr) return;

    Frame response{};
    if (!m_callbacks.localHandler(m_callbacks.context, frame, response)) return;
    if (response.length == 0 || response.length > Protocol::FRAME_MAX_PAYLOAD) {
        ++m_framingDrops;
        return;
    }
    response.channel = Protocol::CHANNEL_LOCAL;
    (void)enqueueUart(response);
}

bool RuntimeBridge::enqueueRf(const Frame& frame, TxTag tag) {
    if (!m_rfEnabled) {
        ++m_queueDrops;
        return false;
    }
    if (m_rfCount == m_rfQueue.size()) {
        ++m_queueDrops;
        return false;
    }
    const std::size_t tail = (m_rfHead + m_rfCount) % m_rfQueue.size();
    TxMessage& message = m_rfQueue[tail];
    message = {};
    message.frame = frame;
    message.messageId = m_nextMessageId[frame.channel]++;
    message.tag = tag;
    message.segmentCount = static_cast<std::uint8_t>(
        (static_cast<std::size_t>(frame.length) + Protocol::RF_SEGMENT_MAX_DATA - 1U) /
        Protocol::RF_SEGMENT_MAX_DATA);
    message.active = true;
    ++m_rfCount;
    return true;
}

bool RuntimeBridge::enqueueUart(const Frame& frame) {
    if (m_uartCount == m_uartQueue.size()) {
        ++m_queueDrops;
        return false;
    }
    const std::size_t tail = (m_uartHead + m_uartCount) % m_uartQueue.size();
    m_uartQueue[tail] = frame;
    ++m_uartCount;
    return true;
}

bool RuntimeBridge::queueLocalResponse(const std::uint8_t* payload, std::size_t length) {
    if (payload == nullptr || length == 0 || length > Protocol::FRAME_MAX_PAYLOAD) return false;
    Frame response{};
    response.channel = Protocol::CHANNEL_LOCAL;
    response.length = static_cast<std::uint16_t>(length);
    for (std::size_t i = 0; i < length; ++i) response.payload[i] = payload[i];
    return enqueueUart(response);
}

bool RuntimeBridge::queueRfMessage(std::uint8_t channel,
                                   const std::uint8_t* payload,
                                   std::size_t length,
                                   TxTag tag) {
    if (channel >= Protocol::RF_CHANNEL_COUNT || payload == nullptr || length == 0 ||
        length > Protocol::FRAME_MAX_PAYLOAD) {
        return false;
    }
    Frame message{};
    message.channel = channel;
    message.length = static_cast<std::uint16_t>(length);
    for (std::size_t i = 0; i < length; ++i) message.payload[i] = payload[i];
    return enqueueRf(message, tag);
}

bool RuntimeBridge::enqueueAck(std::uint8_t channel,
                               std::uint8_t messageId,
                               std::uint8_t index) {
    if (channel >= Protocol::RF_CHANNEL_COUNT || index == Protocol::RF_ACK_INDEX) return false;
    if (m_ackCount == m_ackQueue.size()) {
        ++m_queueDrops;
        return false;
    }
    const std::size_t tail = (m_ackHead + m_ackCount) % m_ackQueue.size();
    m_ackQueue[tail] = Ack{channel, messageId, index};
    ++m_ackCount;
    return true;
}

RuntimeBridge::RfResult RuntimeBridge::ingestRfPacket(const std::uint8_t* packet,
                                                      std::size_t packetLength,
                                                      std::uint32_t nowMs) {
    if (packet == nullptr || packetLength < Protocol::RF_SEGMENT_HEADER_LEN ||
        packetLength > Protocol::RF_PACKET_MAX_LEN) {
        ++m_framingDrops;
        return RfResult::DROP;
    }

    // ACKs are five-byte control packets. Their fourth byte is the segment
    // being acknowledged, not a segment count.
    if (packet[2] == Protocol::RF_ACK_INDEX) {
        std::uint8_t channel = 0;
        if (packet[0] == Protocol::RF_MAGIC_CCSDS) {
            channel = Protocol::CHANNEL_CCSDS;
        } else if (packet[0] == Protocol::RF_MAGIC_PAYLOAD) {
            channel = Protocol::CHANNEL_PAYLOAD;
        } else {
            ++m_framingDrops;
            return RfResult::DROP;
        }
        // The Arduino relay ignores ACK-shaped packets with extra bytes
        // after validating the RF magic. Preserve that resynchronization
        // behavior; only the canonical five-byte form is an ACK here.
        if (packetLength != Protocol::RF_SEGMENT_HEADER_LEN) return RfResult::NONE;
        if (packet[3] == Protocol::RF_ACK_INDEX || packet[4] != 0) {
            // The Arduino relay returns immediately for all ACK-index
            // packets, including malformed control bytes.
            return RfResult::NONE;
        }
        ++m_ackRx;
        if (m_rfCount != 0) {
            TxMessage& message = m_rfQueue[m_rfHead];
            if (message.active && message.awaitingAck && message.frame.channel == channel &&
                message.messageId == packet[1] && message.nextIndex == packet[3]) {
                message.awaitingAck = false;
                message.retries = 0;
                ++message.nextIndex;
                if (message.nextIndex == message.segmentCount) {
                    notifyCompletion(message, true);
                    removeRfHead();
                }
            }
        }
        return RfResult::ACK;
    }

    Segment segment{};
    if (!decodeSegment(packet, packetLength, segment) || segment.channel >= Protocol::RF_CHANNEL_COUNT) {
        ++m_framingDrops;
        return RfResult::DROP;
    }
    return acceptSegment(segment, nowMs);
}

RuntimeBridge::RfResult RuntimeBridge::acceptSegment(const Segment& segment,
                                                     std::uint32_t nowMs) {
    RxAssembly& assembly = m_rx[segment.channel];
    if (assembly.active && elapsed(nowMs, assembly.lastSegmentMs, Protocol::RF_REASSEMBLY_TIMEOUT_MS)) {
        resetAssembly(assembly);
        ++m_timeoutEvents;
        ++m_reassemblyDrops;
    }

    // A completed message remains remembered so retransmitted packets do not
    // get delivered to the Pi twice. Re-ACK each duplicate exactly as the
    // Arduino satellite does.
    if (!assembly.active && assembly.seenCompleted && assembly.messageId == segment.messageId) {
        ++m_duplicateDrops;
        if (rxAckRequired(segment.channel)) {
            (void)sendAck(Ack{segment.channel, segment.messageId, segment.index});
        }
        return RfResult::DROP;
    }

    if (!assembly.active) {
        if (segment.index != 0) {
            ++m_reassemblyDrops;
            return RfResult::DROP;
        }
        if (assembly.seenCompleted &&
            static_cast<std::uint8_t>(assembly.messageId + 1U) != segment.messageId) {
            ++m_messageIdGaps;
        }
        assembly.active = true;
        assembly.messageId = segment.messageId;
        assembly.nextIndex = 0;
        assembly.segmentCount = segment.count;
        assembly.length = 0;
    } else if (segment.messageId == assembly.messageId && segment.count == assembly.segmentCount &&
               segment.index < assembly.nextIndex) {
        // A retransmitted segment that was already accepted is harmless. It
        // must still be acknowledged on the reliable CCSDS direction.
        ++m_duplicateDrops;
        if (rxAckRequired(segment.channel)) {
            (void)sendAck(Ack{segment.channel, segment.messageId, segment.index});
        }
        return RfResult::DROP;
    } else if (segment.messageId != assembly.messageId || segment.count != assembly.segmentCount ||
               segment.index != assembly.nextIndex) {
        // An index-zero packet is an explicit fresh-message boundary. Other
        // mismatches discard the partial message and wait for a new index zero.
        resetAssembly(assembly);
        ++m_reassemblyDrops;
        if (segment.index != 0) return RfResult::DROP;
        assembly.active = true;
        assembly.messageId = segment.messageId;
        assembly.nextIndex = 0;
        assembly.segmentCount = segment.count;
        assembly.length = 0;
    }

    if (segment.length == 0 || segment.length > Protocol::RF_SEGMENT_MAX_DATA ||
        static_cast<std::size_t>(assembly.length) + segment.length > Protocol::FRAME_MAX_PAYLOAD) {
        resetAssembly(assembly);
        ++m_reassemblyDrops;
        return RfResult::DROP;
    }

    for (std::size_t i = 0; i < segment.length; ++i) {
        assembly.buffer[assembly.length + i] = segment.data[i];
    }
    assembly.length = static_cast<std::uint16_t>(assembly.length + segment.length);
    ++assembly.nextIndex;
    assembly.lastSegmentMs = nowMs;
    if (rxAckRequired(segment.channel)) {
        (void)sendAck(Ack{segment.channel, segment.messageId, segment.index});
    }

    if (assembly.nextIndex != assembly.segmentCount) return RfResult::SEGMENT;

    Frame output{};
    output.channel = segment.channel;
    output.length = assembly.length;
    for (std::size_t i = 0; i < assembly.length; ++i) output.payload[i] = assembly.buffer[i];
    const bool consumed = m_callbacks.rfMessageHandler != nullptr &&
                          m_callbacks.rfMessageHandler(m_callbacks.context, output);
    if (!consumed) (void)enqueueUart(output);
    assembly.seenCompleted = true;
    assembly.messageId = segment.messageId;
    resetAssembly(assembly);
    return RfResult::COMPLETE;
}

void RuntimeBridge::resetAssembly(RxAssembly& assembly) {
    assembly.active = false;
    assembly.nextIndex = 0;
    assembly.segmentCount = 0;
    assembly.length = 0;
    assembly.lastSegmentMs = 0;
}

void RuntimeBridge::poll(std::uint32_t nowMs, bool allowRfTx) {
    for (auto& assembly : m_rx) {
        if (assembly.active && elapsed(nowMs, assembly.lastSegmentMs, Protocol::RF_REASSEMBLY_TIMEOUT_MS)) {
            resetAssembly(assembly);
            ++m_timeoutEvents;
            ++m_reassemblyDrops;
        }
    }
    serviceAcks();
    if (allowRfTx) serviceRf(nowMs);
    serviceUart();
}

void RuntimeBridge::serviceAcks() {
    if (m_ackCount == 0 || m_callbacks.rfSend == nullptr) return;
    const Ack& ack = m_ackQueue[m_ackHead];
    if (sendAck(ack)) removeAckHead();
}

bool RuntimeBridge::sendAck(const Ack& ack) {
    std::uint8_t packet[Protocol::RF_PACKET_MAX_LEN]{};
    const std::size_t length = encodeAck(ack.channel, ack.messageId, ack.index, packet);
    if (length == 0 || m_callbacks.rfSend == nullptr ||
        !m_callbacks.rfSend(m_callbacks.context, packet, length)) {
        return false;
    }
    ++m_ackTx;
    return true;
}

void RuntimeBridge::serviceRf(std::uint32_t nowMs) {
    if (m_rfCount == 0 || m_callbacks.rfSend == nullptr) return;
    TxMessage& message = m_rfQueue[m_rfHead];
    if (!message.active) {
        removeRfHead();
        return;
    }
    if (message.awaitingAck) {
        if (!reached(nowMs, message.ackDeadlineMs, Protocol::RF_ACK_TIMEOUT_MS)) return;
        ++m_ackTimeouts;
        message.awaitingAck = false;
        if (message.retries < Protocol::RF_ACK_RETRIES) {
            ++message.retries;
            ++m_ackRetries;
        } else {
            notifyCompletion(message, false);
            removeRfHead();
            return;
        }
    }
    const std::uint32_t gapMs = message.frame.channel == Protocol::CHANNEL_PAYLOAD
                                    ? Protocol::RF_PAYLOAD_INTER_PACKET_GAP_MS
                                    : Protocol::RF_INTER_SEGMENT_GAP_MS;
    if (m_haveRfTxTime && !reached(nowMs, m_lastRfTxMs, gapMs)) {
        return;
    }
    (void)sendSegment(message, nowMs);
}

bool RuntimeBridge::sendSegment(TxMessage& message, std::uint32_t nowMs) {
    const std::size_t offset = static_cast<std::size_t>(message.nextIndex) *
                               Protocol::RF_SEGMENT_MAX_DATA;
    if (offset >= message.frame.length || message.nextIndex >= message.segmentCount) return false;
    const std::size_t remaining = message.frame.length - offset;
    const std::size_t chunkLength = remaining < Protocol::RF_SEGMENT_MAX_DATA
                                        ? remaining
                                        : Protocol::RF_SEGMENT_MAX_DATA;
    Segment segment{};
    segment.channel = message.frame.channel;
    segment.messageId = message.messageId;
    segment.index = message.nextIndex;
    segment.count = message.segmentCount;
    segment.length = static_cast<std::uint8_t>(chunkLength);
    for (std::size_t i = 0; i < chunkLength; ++i) {
        segment.data[i] = message.frame.payload[offset + i];
    }
    std::uint8_t packet[Protocol::RF_PACKET_MAX_LEN]{};
    const std::size_t packetLength = encodeSegment(segment, packet);
    if (packetLength == 0 || m_callbacks.rfSend == nullptr ||
        !m_callbacks.rfSend(m_callbacks.context, packet, packetLength)) {
        notifyCompletion(message, false);
        removeRfHead();
        return false;
    }
    m_haveRfTxTime = true;
    m_lastRfTxMs = m_callbacks.clockNow != nullptr ? m_callbacks.clockNow(m_callbacks.context) : nowMs;
    if (txAckRequired(message.frame.channel)) {
        message.awaitingAck = true;
        message.ackDeadlineMs = m_lastRfTxMs;
    } else {
        ++message.nextIndex;
        if (message.nextIndex == message.segmentCount) {
            notifyCompletion(message, true);
            removeRfHead();
        }
    }
    return true;
}

bool RuntimeBridge::sendUartFrame(const Frame& frame) {
    if (frame.channel >= Protocol::CHANNEL_COUNT || frame.length == 0 ||
        frame.length > Protocol::FRAME_MAX_PAYLOAD || m_callbacks.uartWrite == nullptr) {
        return false;
    }
    std::uint8_t encoded[Protocol::FRAME_MAX_PAYLOAD + 7U]{};
    std::size_t index = 0;
    encoded[index++] = Protocol::FRAME_MAGIC_0;
    encoded[index++] = Protocol::FRAME_MAGIC_1;
    encoded[index++] = frame.channel;
    encoded[index++] = static_cast<std::uint8_t>(frame.length);
    encoded[index++] = static_cast<std::uint8_t>(frame.length >> 8U);
    for (std::size_t i = 0; i < frame.length; ++i) encoded[index + i] = frame.payload[i];
    index += frame.length;
    const std::uint16_t crc = crc16Ccitt(frame.payload.data(), frame.length);
    encoded[index++] = static_cast<std::uint8_t>(crc);
    encoded[index++] = static_cast<std::uint8_t>(crc >> 8U);
    return m_callbacks.uartWrite(m_callbacks.context, encoded, index);
}

void RuntimeBridge::serviceUart() {
    if (m_uartCount == 0 || m_callbacks.uartWrite == nullptr) return;
    if (sendUartFrame(m_uartQueue[m_uartHead])) removeUartHead();
}

void RuntimeBridge::removeRfHead() {
    if (m_rfCount == 0) return;
    m_rfQueue[m_rfHead] = {};
    m_rfHead = (m_rfHead + 1U) % m_rfQueue.size();
    --m_rfCount;
}

void RuntimeBridge::removeUartHead() {
    if (m_uartCount == 0) return;
    m_uartQueue[m_uartHead] = {};
    m_uartHead = (m_uartHead + 1U) % m_uartQueue.size();
    --m_uartCount;
}

void RuntimeBridge::removeAckHead() {
    if (m_ackCount == 0) return;
    m_ackQueue[m_ackHead] = {};
    m_ackHead = (m_ackHead + 1U) % m_ackQueue.size();
    --m_ackCount;
}

void RuntimeBridge::notifyCompletion(const TxMessage& message, bool sent) {
    if (message.tag != TxTag::NONE && m_callbacks.rfCompletionHandler != nullptr) {
        m_callbacks.rfCompletionHandler(m_callbacks.context, message.tag, sent);
    }
}

void RuntimeBridge::discardRadioWork() {
    while (m_rfCount != 0) {
        notifyCompletion(m_rfQueue[m_rfHead], false);
        removeRfHead();
    }
    for (auto& ack : m_ackQueue) ack = {};
    for (auto& assembly : m_rx) assembly = {};
    m_ackHead = m_ackCount = 0;
    m_haveRfTxTime = false;
    m_lastRfTxMs = 0;
}

void RuntimeBridge::reset() {
    m_uartParser.reset();
    discardRadioWork();
    for (auto& frame : m_uartQueue) frame = {};
    m_nextMessageId = {};
    m_uartHead = m_uartCount = 0;
}

}  // namespace SatelliteController
