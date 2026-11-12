#include "ProductCache.hpp"

#include <cstring>

namespace Artemis {
namespace Science {
namespace {
std::uint16_t read16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[0]) | static_cast<unsigned>(bytes[1]) << 8U);
}
void write16(std::uint8_t* bytes, std::uint16_t value) {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
}
void write32(std::uint8_t* bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<std::uint8_t>(value >> (i * 8U));
}
}  // namespace

bool operator==(const ProductKey& left, const ProductKey& right) {
    return left.session == right.session && left.generation == right.generation &&
           left.productId == right.productId && left.totalBytes == right.totalBytes && left.crc16 == right.crc16;
}
bool operator!=(const ProductKey& left, const ProductKey& right) { return !(left == right); }

std::uint16_t crc16Ccitt(const std::uint8_t* bytes, std::size_t length) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(bytes[i]) << 8U;
        for (unsigned bit = 0; bit < 8; ++bit) {
            const std::uint32_t shifted = static_cast<std::uint32_t>(crc) << 1U;
            crc = static_cast<std::uint16_t>((crc & 0x8000U) ? shifted ^ 0x1021U : shifted);
        }
    }
    return crc;
}

ProductCache::ProductCache(std::uint8_t* storage, std::size_t capacity)
    : m_storage(storage), m_capacity(capacity < CACHE_BYTES ? capacity : CACHE_BYTES) {}

void ProductCache::setSession(std::uint32_t session) {
    if (session != m_session && m_state == CacheState::RECEIVING) invalidate(CacheState::EMPTY);
    m_session = session;
}

bool ProductCache::matches(const ProductKey& key) const { return key == m_key; }

Status ProductCache::begin(const ProductKey& key) {
    if (m_storage == nullptr || key.session == 0 || key.generation == 0 || key.totalBytes == 0 ||
        key.totalBytes > m_capacity) return Status::BAD_ARGUMENT;
    if (m_session == 0) m_session = key.session;
    if (key.session != m_session) return Status::STALE;
    if (matches(key) && (m_state == CacheState::RECEIVING || m_valid)) return Status::OK;
    if (m_state == CacheState::SENDING || m_state == CacheState::RECEIVING) return Status::BUSY;
    if (m_key.session == key.session && key.generation <= m_key.generation) return Status::STALE;
    invalidate(CacheState::RECEIVING);
    m_key = key;
    return Status::OK;
}

Status ProductCache::append(const ProductKey& key, std::uint32_t offset, const std::uint8_t* bytes,
                            std::size_t length) {
    if (key.session != m_session || !matches(key)) return Status::STALE;
    if (bytes == nullptr || length == 0 || length > STAGING_CHUNK_BYTES || offset > m_key.totalBytes ||
        length > m_key.totalBytes - offset) return Status::BAD_ARGUMENT;
    if (m_state != CacheState::RECEIVING) return Status::BUSY;
    if (offset < m_received) {
        return length <= m_received - offset && std::memcmp(m_storage + offset, bytes, length) == 0
                   ? Status::OK : Status::BAD_ARGUMENT;
    }
    if (offset != m_received) return Status::BAD_ARGUMENT;
    std::memcpy(m_storage + offset, bytes, length);
    m_received += static_cast<std::uint32_t>(length);
    return Status::OK;
}

Status ProductCache::commit(const ProductKey& key) {
    if (key.session != m_session || !matches(key)) return Status::STALE;
    if (m_valid) return Status::OK;
    if (m_state != CacheState::RECEIVING) return Status::NO_PRODUCT;
    if (m_received != m_key.totalBytes) return Status::INCOMPLETE;
    if (crc16Ccitt(m_storage, m_received) != m_key.crc16) {
        invalidate(CacheState::ERROR);
        return Status::BAD_CRC;
    }
    m_valid = true;
    m_state = CacheState::READY;
    return Status::OK;
}

Status ProductCache::abort(const ProductKey& key) {
    if (!matches(key) || key.session != m_session) return Status::STALE;
    if (m_state == CacheState::SENDING) return Status::BUSY;
    invalidate(CacheState::EMPTY);
    return Status::OK;
}

void ProductCache::invalidate(CacheState state) {
    m_valid = false;
    m_received = 0;
    m_state = state;
    ++m_revision;
}

void ProductCache::reset() {
    invalidate(CacheState::EMPTY);
    m_session = 0;
    m_key = ProductKey{};
}

void ProductCache::sending(bool active) {
    if (m_valid) m_state = active ? CacheState::SENDING : CacheState::READY;
}

bool ProductCache::read(std::uint32_t offset, std::uint8_t* bytes, std::size_t length) const {
    if (!m_valid || bytes == nullptr || offset > m_key.totalBytes || length > m_key.totalBytes - offset) return false;
    std::memcpy(bytes, m_storage + offset, length);
    return true;
}

Downlink::Downlink(ProductCache& cache) : m_cache(cache) {}

bool Downlink::current() const { return m_cache.valid() && m_revision == m_cache.m_revision; }
bool Downlink::active() const { return current() && m_phase != Phase::IDLE; }

Status Downlink::start() {
    if (m_cache.state() == CacheState::SENDING) return Status::BUSY;
    if (!m_cache.valid()) return Status::NO_PRODUCT;
    if (++m_transferId == 0) ++m_transferId;
    m_revision = m_cache.m_revision;
    m_totalPackets = static_cast<std::uint16_t>((m_cache.key().totalBytes + DATA_BYTES - 1U) / DATA_BYTES);
    m_headersRemaining = 3;
    m_nextIndex = 0;
    m_phase = Phase::HEADER;
    m_pending = false;
    m_repairs.fill(0);
    m_repairCount = 0;
    m_repairCursor = 0;
    m_acceptRepairs = true;
    m_cache.sending(true);
    return Status::OK;
}

bool Downlink::repairBit(std::uint16_t index) const {
    return (m_repairs[index / 8U] & static_cast<std::uint8_t>(1U << (index % 8U))) != 0;
}
void Downlink::setRepairBit(std::uint16_t index) {
    if (!repairBit(index)) { m_repairs[index / 8U] |= static_cast<std::uint8_t>(1U << (index % 8U)); ++m_repairCount; }
}
void Downlink::clearRepairBit(std::uint16_t index) {
    if (repairBit(index)) { m_repairs[index / 8U] &= static_cast<std::uint8_t>(~(1U << (index % 8U))); --m_repairCount; }
}
std::uint16_t Downlink::nextRepair() const {
    for (std::uint16_t delta = 0; delta < m_totalPackets; ++delta) {
        const std::uint16_t index = static_cast<std::uint16_t>((m_repairCursor + delta) % m_totalPackets);
        if (repairBit(index)) return index;
    }
    return 0;  // Only called with a nonempty bitmap.
}

bool Downlink::acceptRetry(const std::uint8_t* bytes, std::size_t length) {
    if (!m_acceptRepairs || !current() || m_transferId == 0 || bytes == nullptr || length < 8 ||
        bytes[0] != link_protocol::PAYLOAD_MAGIC_0 || bytes[1] != link_protocol::PAYLOAD_MAGIC_1 ||
        bytes[2] != 4 || bytes[3] != m_transferId || bytes[6] == 0 || bytes[6] > 36 ||
        length != static_cast<std::size_t>(7U + bytes[6])) return false;
    const std::uint16_t start = read16(bytes + 4);
    // Validate before mutating, so one malformed bitmap cannot enqueue a prefix.
    for (unsigned i = 0; i < static_cast<unsigned>(bytes[6]) * 8U; ++i) {
        if ((bytes[7U + i / 8U] & (1U << (i % 8U))) != 0 &&
            static_cast<std::uint32_t>(start) + i >= m_totalPackets) return false;
    }
    for (unsigned i = 0; i < static_cast<unsigned>(bytes[6]) * 8U; ++i) {
        if ((bytes[7U + i / 8U] & (1U << (i % 8U))) != 0) setRepairBit(static_cast<std::uint16_t>(start + i));
    }
    if (m_repairCount != 0 && m_phase == Phase::IDLE) {
        m_phase = Phase::REPAIR;
        m_cache.sending(true);
    }
    return true;
}

bool Downlink::nextPacket(Packet& packet) {
    packet.length = 0;
    if (!current() || m_phase == Phase::IDLE) return false;
    if (!m_pending) {
        m_pendingPhase = m_phase;
        m_pendingIndex = m_phase == Phase::REPAIR ? nextRepair() : m_nextIndex;
    }
    packet.bytes.fill(0);
    auto* out = packet.bytes.data();
    out[0] = link_protocol::PAYLOAD_MAGIC_0;
    out[1] = link_protocol::PAYLOAD_MAGIC_1;
    out[3] = m_transferId;
    if (m_pendingPhase == Phase::HEADER) {
        out[2] = 1;
        write32(out + 4, m_cache.key().productId);
        write32(out + 8, m_cache.key().totalBytes);
        write16(out + 12, m_totalPackets);
        out[14] = DATA_BYTES;
        write16(out + 15, m_cache.key().crc16);
        packet.length = 17;
    } else if (m_pendingPhase == Phase::DATA || m_pendingPhase == Phase::REPAIR) {
        const std::uint32_t offset = static_cast<std::uint32_t>(m_pendingIndex) * DATA_BYTES;
        const std::uint32_t remaining = m_cache.key().totalBytes - offset;
        const std::uint8_t count = static_cast<std::uint8_t>(remaining < DATA_BYTES ? remaining : DATA_BYTES);
        out[2] = 2;
        write16(out + 4, m_pendingIndex);
        out[6] = count;
        if (!m_cache.read(offset, out + 7, count)) return false;
        write16(out + 7 + count, crc16Ccitt(out, 7U + count));
        packet.length = static_cast<std::uint16_t>(9U + count);
    } else if (m_pendingPhase == Phase::END) {
        out[2] = 3;
        write16(out + 4, m_totalPackets);
        write16(out + 6, m_cache.key().crc16);
        packet.length = 8;
    }
    m_pending = packet.length != 0;
    return m_pending;
}

void Downlink::packetSent(bool success) {
    if (!m_pending || !current() || !success) return;
    m_pending = false;
    if (m_pendingPhase == Phase::HEADER) {
        if (--m_headersRemaining == 0) m_phase = Phase::DATA;
    } else if (m_pendingPhase == Phase::DATA) {
        if (++m_nextIndex == m_totalPackets) m_phase = m_repairCount != 0 ? Phase::REPAIR : Phase::END;
    } else if (m_pendingPhase == Phase::REPAIR) {
        clearRepairBit(m_pendingIndex);
        m_repairCursor = static_cast<std::uint16_t>((m_pendingIndex + 1U) % m_totalPackets);
        if (m_repairCount == 0) m_phase = Phase::END;
    } else if (m_pendingPhase == Phase::END) {
        m_phase = m_repairCount != 0 ? Phase::REPAIR : Phase::IDLE;
        m_cache.sending(m_phase != Phase::IDLE);
    }
}

void Downlink::stop() {
    m_phase = Phase::IDLE;
    m_pending = false;
    m_repairs.fill(0);
    m_repairCount = 0;
    m_acceptRepairs = false;
    if (current()) m_cache.sending(false);
}

}  // namespace Science
}  // namespace Artemis
