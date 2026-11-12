#include "FprimeArtemisCore/Science/ProductCache.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace Artemis::Science;

#define REQUIRE(condition) do { if (!(condition)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; std::abort(); } } while (false)

namespace {
std::vector<std::uint8_t> product(std::size_t length) {
    std::vector<std::uint8_t> bytes(length);
    for (std::size_t i = 0; i < length; ++i) bytes[i] = static_cast<std::uint8_t>((i * 31U + i / 256U) & 255U);
    return bytes;
}
ProductKey keyFor(const std::vector<std::uint8_t>& bytes, std::uint32_t generation = 1, std::uint32_t session = 1) {
    return ProductKey{session, generation, 0x12345678U, static_cast<std::uint32_t>(bytes.size()),
                      crc16Ccitt(bytes.data(), bytes.size())};
}
void stage(ProductCache& cache, const ProductKey& key, const std::vector<std::uint8_t>& bytes) {
    REQUIRE(cache.begin(key) == Status::OK);
    for (std::size_t offset = 0; offset < bytes.size();) {
        const std::size_t count = std::min(STAGING_CHUNK_BYTES, bytes.size() - offset);
        REQUIRE(cache.append(key, static_cast<std::uint32_t>(offset), bytes.data() + offset, count) == Status::OK);
        offset += count;
    }
    REQUIRE(cache.commit(key) == Status::OK);
}
std::uint16_t read16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0]) | static_cast<std::uint16_t>(bytes[1]) << 8;
}
std::uint32_t read32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8 |
           static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
}
std::vector<Packet> drain(Downlink& link) {
    std::vector<Packet> packets;
    Packet packet;
    while (link.nextPacket(packet)) {
        packets.push_back(packet);
        REQUIRE(packets.size() <= MAX_PACKETS * 2 + 10);
        link.packetSent(true);
    }
    REQUIRE(!link.active());
    return packets;
}
std::array<std::uint8_t, 8> retry(std::uint8_t transfer, std::uint16_t index) {
    return {{'N', '2', 4, transfer, static_cast<std::uint8_t>(index), static_cast<std::uint8_t>(index >> 8), 1, 1}};
}

void testStaging() {
    std::array<std::uint8_t, CACHE_BYTES + 32> storage;
    storage.fill(0xA5);
    ProductCache cache(storage.data(), CACHE_BYTES);
    Downlink link(cache);
    REQUIRE(storage[0] == 0xA5);  // Constructor must not initialize a full product-sized region.
    REQUIRE(sizeof(ProductCache) < 128);
    REQUIRE(sizeof(Downlink) < 1024);
    REQUIRE(cache.state() == CacheState::EMPTY && !cache.valid());
    REQUIRE(link.start() == Status::NO_PRODUCT);
    auto bytes = product(401);
    auto key = keyFor(bytes);
    auto invalid = key;
    invalid.totalBytes = static_cast<std::uint32_t>(CACHE_BYTES + 1);
    REQUIRE(cache.begin(invalid) == Status::BAD_ARGUMENT);
    invalid = key; invalid.totalBytes = 0;
    REQUIRE(cache.begin(invalid) == Status::BAD_ARGUMENT);
    invalid = key; invalid.session = 0;
    REQUIRE(cache.begin(invalid) == Status::BAD_ARGUMENT);
    invalid = key; invalid.generation = 0;
    REQUIRE(cache.begin(invalid) == Status::BAD_ARGUMENT);
    ProductCache nullCache(nullptr, CACHE_BYTES);
    REQUIRE(nullCache.begin(key) == Status::BAD_ARGUMENT);
    ProductCache small(storage.data(), 400);
    REQUIRE(small.begin(key) == Status::BAD_ARGUMENT);

    REQUIRE(cache.begin(key) == Status::OK);
    REQUIRE(cache.append(key, 1, bytes.data(), 200) == Status::BAD_ARGUMENT);
    REQUIRE(cache.append(key, std::numeric_limits<std::uint32_t>::max(), bytes.data(), 200) == Status::BAD_ARGUMENT);
    REQUIRE(cache.append(key, 0, bytes.data(), 201) == Status::BAD_ARGUMENT);
    REQUIRE(cache.append(key, 0, nullptr, 1) == Status::BAD_ARGUMENT);
    REQUIRE(cache.append(key, 0, bytes.data(), 0) == Status::BAD_ARGUMENT);
    REQUIRE(cache.append(key, 0, bytes.data(), 200) == Status::OK);
    REQUIRE(cache.append(key, 0, bytes.data(), 200) == Status::OK);
    auto conflict = bytes;
    conflict[199] ^= 1;
    REQUIRE(cache.append(key, 0, conflict.data(), 200) == Status::BAD_ARGUMENT);
    REQUIRE(cache.receivedBytes() == 200);
    REQUIRE(cache.begin(key) == Status::OK && cache.receivedBytes() == 200);
    auto newer = key; ++newer.generation;
    REQUIRE(cache.begin(newer) == Status::BUSY);
    REQUIRE(cache.append(newer, 200, bytes.data() + 200, 200) == Status::STALE);
    REQUIRE(cache.commit(key) == Status::INCOMPLETE);
    REQUIRE(cache.append(key, 200, bytes.data() + 200, 200) == Status::OK);
    REQUIRE(cache.append(key, 400, bytes.data() + 400, 2) == Status::BAD_ARGUMENT);
    REQUIRE(cache.append(key, 400, bytes.data() + 400, 1) == Status::OK);
    REQUIRE(cache.commit(key) == Status::OK);
    REQUIRE(cache.commit(key) == Status::OK);
    REQUIRE(cache.valid() && cache.state() == CacheState::READY);
    std::vector<std::uint8_t> read(bytes.size());
    REQUIRE(cache.read(0, read.data(), read.size()) && read == bytes);
    REQUIRE(!cache.read(static_cast<std::uint32_t>(bytes.size()), read.data(), 1));
    REQUIRE(cache.append(key, 0, bytes.data(), 200) == Status::BUSY);
    REQUIRE(cache.begin(key) == Status::OK);
    REQUIRE(cache.begin(newer) == Status::OK && !cache.valid());
    REQUIRE(!cache.read(0, read.data(), 1));
    REQUIRE(cache.abort(key) == Status::STALE);
    REQUIRE(cache.abort(newer) == Status::OK);
    REQUIRE(cache.state() == CacheState::EMPTY);
    REQUIRE(cache.begin(newer) == Status::STALE);

    newer.generation++;
    REQUIRE(cache.begin(newer) == Status::OK);
    REQUIRE(cache.append(newer, 0, conflict.data(), 200) == Status::OK);
    REQUIRE(cache.append(newer, 200, bytes.data() + 200, 200) == Status::OK);
    REQUIRE(cache.append(newer, 400, bytes.data() + 400, 1) == Status::OK);
    REQUIRE(cache.commit(newer) == Status::BAD_CRC);
    REQUIRE(cache.state() == CacheState::ERROR && !cache.valid());
    for (std::size_t i = CACHE_BYTES; i < storage.size(); ++i) REQUIRE(storage[i] == 0xA5);
}

void testMaximumAndWire() {
    std::array<std::uint8_t, CACHE_BYTES + 16> storage;
    storage.fill(0x5A);
    ProductCache cache(storage.data(), CACHE_BYTES);
    auto bytes = product(CACHE_BYTES);
    const auto key = keyFor(bytes);
    stage(cache, key, bytes);
    Downlink link(cache);
    REQUIRE(link.start() == Status::OK);
    auto replacement = key; replacement.generation++;
    REQUIRE(cache.begin(replacement) == Status::BUSY);
    REQUIRE(cache.abort(key) == Status::BUSY);
    Packet first, repeated;
    REQUIRE(link.nextPacket(first));
    link.packetSent(false);
    REQUIRE(link.nextPacket(repeated));
    REQUIRE(first.length == repeated.length && first.bytes == repeated.bytes);
    const auto packets = drain(link);
    REQUIRE(packets.size() == 3 + (CACHE_BYTES + DATA_BYTES - 1) / DATA_BYTES + 1);
    REQUIRE(link.totalPackets() == 5618);
    std::vector<std::uint8_t> rebuilt;
    for (std::size_t i = 0; i < packets.size(); ++i) {
        const auto& packet = packets[i];
        REQUIRE(packet.bytes[0] == 'N' && packet.bytes[1] == '2' && packet.bytes[3] == link.transferId());
        if (i < 3) {
            REQUIRE(packet.length == 17 && packet.bytes[2] == 1);
            REQUIRE(read32(packet.bytes.data() + 4) == key.productId);
            REQUIRE(read32(packet.bytes.data() + 8) == CACHE_BYTES);
            REQUIRE(read16(packet.bytes.data() + 12) == 5618 && packet.bytes[14] == 35);
            REQUIRE(read16(packet.bytes.data() + 15) == key.crc16);
        } else if (packet.bytes[2] == 2) {
            REQUIRE(read16(packet.bytes.data() + 4) == i - 3);
            const std::size_t count = packet.bytes[6];
            REQUIRE(packet.length == count + 9);
            REQUIRE(read16(packet.bytes.data() + 7 + count) == crc16Ccitt(packet.bytes.data(), count + 7));
            rebuilt.insert(rebuilt.end(), packet.bytes.begin() + 7, packet.bytes.begin() + 7 + count);
        } else {
            REQUIRE(i + 1 == packets.size() && packet.length == 8 && packet.bytes[2] == 3);
            REQUIRE(read16(packet.bytes.data() + 4) == 5618);
            REQUIRE(read16(packet.bytes.data() + 6) == key.crc16);
        }
    }
    REQUIRE(rebuilt == bytes && cache.state() == CacheState::READY);
    for (std::size_t i = CACHE_BYTES; i < storage.size(); ++i) REQUIRE(storage[i] == 0x5A);

    // Separate repair bitmaps must merge, including indices beyond the old
    // 288-entry queue, without replacing the complete product or requiring Pi.
    cache.setSession(2);
    auto a = retry(link.transferId(), 7);
    auto b = retry(link.transferId(), 5617);
    REQUIRE(link.acceptRetry(a.data(), a.size()));
    REQUIRE(link.acceptRetry(a.data(), a.size()) && link.pendingRepairs() == 1);
    REQUIRE(link.acceptRetry(b.data(), b.size()) && link.pendingRepairs() == 2);
    REQUIRE(cache.state() == CacheState::SENDING);
    replacement.session = 2;
    REQUIRE(cache.begin(replacement) == Status::BUSY);
    auto invalid = retry(link.transferId(), 5618);
    REQUIRE(!link.acceptRetry(invalid.data(), invalid.size()) && link.pendingRepairs() == 2);
    invalid = b; invalid[7] = 3;  // A valid prefix followed by an out-of-range bit must be atomic.
    REQUIRE(!link.acceptRetry(invalid.data(), invalid.size()) && link.pendingRepairs() == 2);
    invalid = a; invalid[3]++;
    REQUIRE(!link.acceptRetry(invalid.data(), invalid.size()));
    const auto repairs = drain(link);
    REQUIRE(repairs.size() == 3 && repairs[0].bytes[2] == 2 && repairs[1].bytes[2] == 2 && repairs[2].bytes[2] == 3);
    REQUIRE(read16(repairs[0].bytes.data() + 4) == 7 && read16(repairs[1].bytes.data() + 4) == 5617);
    REQUIRE(cache.valid() && cache.key() == key && cache.session() == 2);
    const auto previousTransfer = link.transferId();
    REQUIRE(link.start() == Status::OK && link.transferId() != previousTransfer);
    link.stop();
    a = retry(link.transferId(), 7);
    REQUIRE(!link.acceptRetry(a.data(), a.size()));
    REQUIRE(cache.valid() && cache.state() == CacheState::READY);
    for (unsigned i = 0; i < 300; ++i) {
        const auto previous = link.transferId();
        REQUIRE(link.start() == Status::OK && link.transferId() != 0 && link.transferId() != previous);
        link.stop();
    }
    cache.reset();
    REQUIRE(!cache.valid() && cache.state() == CacheState::EMPTY && !link.active());
    REQUIRE(link.start() == Status::NO_PRODUCT);
}

void testSessionsAndPendingEnd() {
    std::array<std::uint8_t, 1000> storage;
    ProductCache cache(storage.data(), storage.size());
    auto bytes = product(71);
    auto key = keyFor(bytes);
    REQUIRE(cache.begin(key) == Status::OK);
    REQUIRE(cache.append(key, 0, bytes.data(), 50) == Status::OK);
    cache.setSession(2);
    REQUIRE(cache.state() == CacheState::EMPTY && !cache.valid());
    REQUIRE(cache.begin(key) == Status::STALE);
    key.session = 2;
    stage(cache, key, bytes);
    Downlink link(cache);
    REQUIRE(link.start() == Status::OK);
    Packet packet;
    unsigned initialPackets = 0;
    while (link.nextPacket(packet) && packet.bytes[2] != 3) { link.packetSent(true); ++initialPackets; }
    REQUIRE(initialPackets == 6 && packet.bytes[2] == 3);
    auto request = retry(link.transferId(), 0);
    REQUIRE(link.acceptRetry(request.data(), request.size()));
    // A retry arriving after END was prepared must not reinterpret the pending
    // END completion as DATA completion or lose the newly requested repair.
    Packet sameEnd;
    REQUIRE(link.nextPacket(sameEnd) && sameEnd.bytes == packet.bytes);
    link.packetSent(true);
    REQUIRE(link.nextPacket(packet) && packet.bytes[2] == 2 && read16(packet.bytes.data() + 4) == 0);
    link.packetSent(true);
    REQUIRE(link.nextPacket(packet) && packet.bytes[2] == 3);
    link.packetSent(true);
    REQUIRE(!link.active() && cache.state() == CacheState::READY);
    REQUIRE(link.start() == Status::OK);
    REQUIRE(link.nextPacket(packet));
    cache.reset();
    link.packetSent(true);
    REQUIRE(!link.nextPacket(packet) && !link.acceptRetry(request.data(), request.size()));
    stage(cache, key, bytes);
    REQUIRE(link.start() == Status::OK);
    request = retry(link.transferId(), 1);
    REQUIRE(link.acceptRetry(request.data(), request.size()));  // Arrives during initial headers.
    const auto queued = drain(link);
    REQUIRE(queued.size() == 8 && queued[6].bytes[2] == 2 && read16(queued[6].bytes.data() + 4) == 1);
}

void emitVectors(const std::string& directory) {
    std::array<std::uint8_t, CACHE_BYTES> storage;
    ProductCache cache(storage.data(), storage.size());
    auto bytes = product(2051);
    auto key = keyFor(bytes);
    stage(cache, key, bytes);
    Downlink link(cache);
    REQUIRE(link.start() == Status::OK);
    const auto initial = drain(link);
    auto first = retry(link.transferId(), 7);
    auto last = retry(link.transferId(), static_cast<std::uint16_t>(link.totalPackets() - 1));
    REQUIRE(link.acceptRetry(first.data(), first.size()));
    REQUIRE(link.acceptRetry(last.data(), last.size()));
    const auto repairs = drain(link);
    std::ofstream source(directory + "/source.bin", std::ios::binary);
    source.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    for (const auto& item : {std::make_pair("packets.hex", initial), std::make_pair("repairs.hex", repairs)}) {
        std::ofstream stream(directory + "/" + item.first);
        static const char hex[] = "0123456789abcdef";
        for (const auto& packet : item.second) {
            for (unsigned i = 0; i < packet.length; ++i) stream << hex[packet.bytes[i] >> 4] << hex[packet.bytes[i] & 15];
            stream << '\n';
        }
    }
}
}  // namespace

int main(int argc, char** argv) {
    const std::uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    REQUIRE(crc16Ccitt(check, sizeof(check)) == 0x29B1);
    testStaging();
    testMaximumAndWire();
    testSessionsAndPendingEnd();
    if (argc == 3 && std::string(argv[1]) == "--emit") emitVectors(argv[2]);
    std::cout << "Science cache: PASS (bounds, sessions, CRC, busy, max product, wire, retained local repair)\n";
}
