#pragma once
#include <cstring>

#include "LinkConstants.hpp"
namespace RadioLink {
struct Packet {
    uint8_t bytes[49] = {};
    uint8_t length = 0, to = 0, from = 0, network = 0, version = 0;
};
class Transport {
   public:
    virtual ~Transport() = default;
    virtual uint32_t now() = 0;
    virtual void pause(uint32_t ms) = 0;
    virtual bool send(const Packet&) = 0;
    virtual bool receive(Packet&) = 0;
    virtual void frame(const uint8_t*, uint16_t) = 0;
    // Legacy users receive CCSDS only. Payload must have its own consumer.
    virtual void frame(uint8_t channel, const uint8_t* bytes, uint16_t length) {
        if (channel == link_protocol::CHANNEL_CCSDS) frame(bytes, length);
    }
};
struct Counters {
    uint32_t tx = 0, rx = 0, retries = 0, timeouts = 0, drops = 0, duplicates = 0, reassemblyTimeouts = 0;
};
class LinkCore {
   public:
    explicit LinkCore(Transport& transport) : io(transport) {}
    Counters counters;
    void reset() {
        for (auto& state : channels) state = {};
    }
    void poll() {
        expire();
        Packet p;
        // Bound a pass so RX cannot starve shutdown or pending TX.
        for (unsigned i = 0; i < 16 && io.receive(p); ++i)
            process(p);
    }
    bool transmit(const uint8_t* data, uint16_t size, bool ackRequired = link_protocol::RF_TX_ACK_REQUIRED_CCSDS) {
        return transmitOn(link_protocol::CHANNEL_CCSDS, data, size, ackRequired);
    }
    bool transmitChannel(uint8_t channel, const uint8_t* data, uint16_t size) {
        if (!link_protocol::isRfChannel(channel)) return false;
        return transmitOn(channel, data, size, link_protocol::txAckRequiredForChannel(channel));
    }

   private:
    bool transmitOn(uint8_t channel, const uint8_t* data, uint16_t size, bool ackRequired) {
        if (!data || !size || size > link_protocol::FRAME_MAX_PAYLOAD)
            return false;
        const uint8_t msg = nextMessage[channel]++, count = (size + 43) / 44;
        for (uint8_t segment = 0; segment < count; ++segment) {
            Packet p;
            address(p);
            const uint16_t offset = segment * 44;
            const uint8_t n = size - offset > 44 ? 44 : size - offset;
            p.length = n + 5;
            p.bytes[0] = link_protocol::magicForChannel(channel);
            p.bytes[1] = msg;
            p.bytes[2] = segment;
            p.bytes[3] = count;
            p.bytes[4] = n;
            std::memcpy(p.bytes + 5, data + offset, n);
            bool success = false;
            for (unsigned attempt = 0; attempt <= link_protocol::RF_ACK_RETRIES; ++attempt) {
                if (!io.send(p))
                    return false;
                ++counters.tx;
                if (!ackRequired) {
                    success = true;
                    break;
                }
                const uint32_t start = io.now();
                while (uint32_t(io.now() - start) < link_protocol::RF_ACK_TIMEOUT_MS) {
                    Packet rx;
                    if (io.receive(rx)) {
                        if (validHeader(rx) && rx.length == 5 && rx.bytes[0] == link_protocol::magicForChannel(channel) && rx.bytes[1] == msg &&
                            rx.bytes[2] == 0xFF && rx.bytes[3] == segment && rx.bytes[4] == 0) {
                            success = true;
                            break;
                        }
                        process(rx);
                    }
                    io.pause(1);
                }
                if (success)
                    break;
                ++counters.timeouts;
                if (attempt < link_protocol::RF_ACK_RETRIES)
                    ++counters.retries;
            }
            if (!success)
                return false;
            if (segment + 1 < count) {
                const uint32_t start = io.now();
                while (uint32_t(io.now() - start) < link_protocol::RF_INTER_SEGMENT_GAP_MS) {
                    poll();
                    io.pause(1);
                }
            }
        }
        return true;
    }

    Transport& io;
    struct Channel {
        uint8_t message = 0, expected = 0, total = 0, lastMessage = 0;
        uint16_t length = 0;
        uint32_t lastTime = 0;
        bool active = false, seen = false;
        uint8_t buffer[link_protocol::FRAME_MAX_PAYLOAD] = {};
    };
    uint8_t nextMessage[link_protocol::CHANNEL_RF_COUNT] = {};
    Channel channels[link_protocol::CHANNEL_RF_COUNT];
    static void address(Packet& p) {
        p.to = link_protocol::RF_REMOTE_ADDRESS;
        p.from = link_protocol::RF_LOCAL_ADDRESS;
        p.network = link_protocol::RF_NETWORK_ID;
        p.version = link_protocol::RF_PROTOCOL_VERSION;
    }
    static bool validHeader(const Packet& p) {
        return link_protocol::classifyRfHeader(p.to, p.from, p.network, p.version) ==
               link_protocol::RfHeaderStatus::ACCEPT;
    }
    void expire() {
        for (auto& state : channels) {
            if (state.active && uint32_t(io.now() - state.lastTime) > link_protocol::RF_REASSEMBLY_TIMEOUT_MS) {
                state.active = false;
                state.length = 0;
                ++counters.reassemblyTimeouts;
            }
        }
    }
    void ack(uint8_t channel, uint8_t msg, uint8_t segment) {
        if (!link_protocol::rxAckRequiredForChannel(channel))
            return;
        Packet p;
        address(p);
        p.length = 5;
        p.bytes[0] = link_protocol::magicForChannel(channel);
        p.bytes[1] = msg;
        p.bytes[2] = 0xFF;
        p.bytes[3] = segment;
        p.bytes[4] = 0;
        if (io.send(p))
            ++counters.tx;
    }
    void process(const Packet& p) {
        expire();
        uint8_t channel = 0;
        if (!validHeader(p) || p.length < 5 || p.length > 49 ||
            !link_protocol::channelForMagic(p.bytes[0], channel)) {
            ++counters.drops;
            return;
        }
        Channel& state = channels[channel];
        ++counters.rx;
        const uint8_t msg = p.bytes[1], seg = p.bytes[2], count = p.bytes[3], n = p.bytes[4];
        if (seg == 0xFF)
            return;
        if (!count || count > 5 || seg >= count || !n || n > 44 || p.length != 5 + n) {
            ++counters.drops;
            return;
        }
        if (!state.active && state.seen && msg == state.lastMessage) {
            ++counters.duplicates;
            ack(channel, msg, seg);
            return;
        }
        if (state.active && msg == state.message && count == state.total && seg < state.expected) {
            ++counters.duplicates;
            ack(channel, msg, seg);
            return;
        }
        if (!state.active || state.message != msg || state.total != count || state.expected != seg) {
            state.active = false;
            state.length = 0;
            if (seg != 0) {
                ++counters.drops;
                return;
            }
            state.active = true;
            state.message = msg;
            state.total = count;
            state.expected = 0;
        }
        if (state.length + n > sizeof(state.buffer)) {
            state.active = false;
            ++counters.drops;
            return;
        }
        std::memcpy(state.buffer + state.length, p.bytes + 5, n);
        state.length += n;
        ++state.expected;
        state.lastTime = io.now();
        ack(channel, msg, seg);
        if (state.expected == state.total) {
            state.active = false;
            state.seen = true;
            state.lastMessage = msg;
            io.frame(channel, state.buffer, state.length);
        }
    }
};
}  // namespace RadioLink
