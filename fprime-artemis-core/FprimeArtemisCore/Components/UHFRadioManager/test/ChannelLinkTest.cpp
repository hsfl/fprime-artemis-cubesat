#include <cassert>
#include <deque>
#include <iostream>
#include <vector>

#include "../LinkCore.hpp"

using namespace RadioLink;
struct ChannelTransport : Transport {
    uint32_t time = 0;
    bool acknowledge = false;
    std::deque<Packet> incoming;
    std::vector<Packet> outgoing;
    std::vector<std::vector<uint8_t>> controlFrames, scienceFrames;
    uint32_t now() override { return time; }
    void pause(uint32_t ms) override { time += ms; }
    static Packet peer(Packet packet) {
        packet.to = link_protocol::RF_LOCAL_ADDRESS;
        packet.from = link_protocol::RF_REMOTE_ADDRESS;
        packet.network = link_protocol::RF_NETWORK_ID;
        packet.version = link_protocol::RF_PROTOCOL_VERSION;
        return packet;
    }
    bool send(const Packet& packet) override {
        outgoing.push_back(packet);
        if (acknowledge && packet.bytes[2] != 0xFF) {
            auto ack = peer(packet);
            ack.length = 5;
            ack.bytes[2] = 0xFF;
            ack.bytes[3] = packet.bytes[2];
            ack.bytes[4] = 0;
            incoming.push_back(ack);
        }
        return true;
    }
    bool receive(Packet& packet) override {
        if (incoming.empty()) return false;
        packet = incoming.front(); incoming.pop_front(); return true;
    }
    void frame(const uint8_t* bytes, uint16_t length) override {
        controlFrames.emplace_back(bytes, bytes + length);
    }
    void frame(uint8_t channel, const uint8_t* bytes, uint16_t length) override {
        if (channel == link_protocol::CHANNEL_CCSDS) frame(bytes, length);
        else if (channel == link_protocol::CHANNEL_PAYLOAD) scienceFrames.emplace_back(bytes, bytes + length);
        else assert(false);
    }
};

int main() {
    uint8_t control[128], science[128];
    for (unsigned i = 0; i < 128; ++i) { control[i] = i; science[i] = 255 - i; }
    ChannelTransport tx;
    tx.acknowledge = true;
    LinkCore sender(tx);
    assert(sender.transmit(control, sizeof(control), false));
    assert(sender.transmitChannel(link_protocol::CHANNEL_PAYLOAD, science, sizeof(science)));
    assert(tx.outgoing.size() == 6);
    assert(tx.outgoing[0].bytes[0] == 0xA5 && tx.outgoing[3].bytes[0] == 0xA6);
    assert(tx.outgoing[0].bytes[1] == 0 && tx.outgoing[3].bytes[1] == 0);
    assert(tx.outgoing[3].bytes[2] == 0 && tx.outgoing[3].bytes[3] == 3 && tx.outgoing[3].bytes[4] == 44);
    assert(std::memcmp(tx.outgoing[3].bytes + 5, science, 44) == 0);

    ChannelTransport rx;
    LinkCore receiver(rx);
    // Same message IDs and overlapping segment progress cannot cross channels.
    for (unsigned i = 0; i < 3; ++i) {
        rx.incoming.push_back(ChannelTransport::peer(tx.outgoing[i]));
        rx.incoming.push_back(ChannelTransport::peer(tx.outgoing[3 + i]));
    }
    receiver.poll();
    assert(rx.controlFrames.size() == 1 && rx.scienceFrames.size() == 1);
    assert(rx.controlFrames[0] == std::vector<uint8_t>(control, control + 128));
    assert(rx.scienceFrames[0] == std::vector<uint8_t>(science, science + 128));
    unsigned controlAcks = 0, scienceAcks = 0;
    for (const auto& packet : rx.outgoing) {
        assert(packet.length == 5 && packet.bytes[2] == 0xFF);
        if (packet.bytes[0] == 0xA5) ++controlAcks;
        if (packet.bytes[0] == 0xA6) ++scienceAcks;
    }
    assert(controlAcks == (link_protocol::RF_RX_ACK_REQUIRED_CCSDS ? 3U : 0U));
    assert(scienceAcks == (link_protocol::RF_RX_ACK_REQUIRED_PAYLOAD ? 3U : 0U));
    for (unsigned i = 0; i < 3; ++i) {
        rx.incoming.push_back(ChannelTransport::peer(tx.outgoing[i]));
        rx.incoming.push_back(ChannelTransport::peer(tx.outgoing[3 + i]));
    }
    receiver.poll();
    assert(rx.controlFrames.size() == 1 && rx.scienceFrames.size() == 1 && receiver.counters.duplicates == 6);

    // Completion or expiry of one channel does not clear the other channel.
    ChannelTransport timedRx;
    LinkCore timed(timedRx);
    timedRx.incoming.push_back(ChannelTransport::peer(tx.outgoing[0]));
    timed.poll();
    timedRx.time = 300;
    timedRx.incoming.push_back(ChannelTransport::peer(tx.outgoing[3]));
    timed.poll();
    timedRx.time = 501;
    timed.poll();
    assert(timed.counters.reassemblyTimeouts == 1);
    timedRx.incoming.push_back(ChannelTransport::peer(tx.outgoing[4]));
    timedRx.incoming.push_back(ChannelTransport::peer(tx.outgoing[5]));
    timedRx.incoming.push_back(ChannelTransport::peer(tx.outgoing[1]));
    timed.poll();
    assert(timedRx.scienceFrames.size() == 1 && timedRx.controlFrames.empty());

    // An A6 ACK with the same ID and segment cannot acknowledge channel0.
    ChannelTransport wrongAck;
    auto ack = ChannelTransport::peer(tx.outgoing[0]);
    ack.length = 5; ack.bytes[0] = 0xA6; ack.bytes[2] = 0xFF; ack.bytes[3] = 0; ack.bytes[4] = 0;
    wrongAck.incoming.push_back(ack);
    LinkCore strict(wrongAck);
    assert(!strict.transmit(control, 1, true));
    assert(strict.counters.timeouts == 5);

    ChannelTransport policy;
    LinkCore configured(policy);
    const bool ok = configured.transmitChannel(link_protocol::CHANNEL_PAYLOAD, science, 44);
    assert(ok == !link_protocol::RF_TX_ACK_REQUIRED_PAYLOAD);
    assert(policy.outgoing.size() == (link_protocol::RF_TX_ACK_REQUIRED_PAYLOAD ? 5U : 1U));
    assert(policy.outgoing[0].length == 49 && policy.outgoing[0].bytes[0] == 0xA6);
    assert(!configured.transmitChannel(link_protocol::CHANNEL_TEENSY_LOCAL, science, 44));
    assert(!configured.transmitChannel(255, science, 44));
    assert(!configured.transmitChannel(link_protocol::CHANNEL_PAYLOAD, science, 221));

    // Per-channel message IDs wrap independently without cross-channel mutation.
    ChannelTransport wrap;
    wrap.acknowledge = true;
    LinkCore ids(wrap);
    for (unsigned i = 0; i < 260; ++i) {
        assert(ids.transmitChannel(link_protocol::CHANNEL_PAYLOAD, science, 1));
        assert(wrap.outgoing.back().bytes[1] == static_cast<uint8_t>(i));
    }
    assert(ids.transmit(control, 1, false));
    assert(wrap.outgoing.back().bytes[0] == 0xA5 && wrap.outgoing.back().bytes[1] == 0);
    std::cout << "Channel link: A5/A6 bytes, interleave, independent IDs/expiry, generated ACK policy and bounds passed\n";
}
