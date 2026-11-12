#ifndef ARTEMIS_SCIENCE_PRODUCT_CACHE_HPP
#define ARTEMIS_SCIENCE_PRODUCT_CACHE_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "../Components/UHFRadioManager/LinkConstants.hpp"

namespace Artemis {
namespace Science {

constexpr std::size_t CACHE_BYTES = link_protocol::PAYLOAD_CACHE_MAX_BYTES;
constexpr std::size_t STAGING_CHUNK_BYTES = link_protocol::PAYLOAD_CACHE_CHUNK_BYTES;
constexpr std::size_t DATA_BYTES = link_protocol::PAYLOAD_PACKET_DATA_BYTES;
constexpr std::size_t PACKET_BYTES = link_protocol::RF_SEGMENT_MAX_DATA;
constexpr std::size_t MAX_PACKETS = (CACHE_BYTES + DATA_BYTES - 1U) / DATA_BYTES;
static_assert(DATA_BYTES > 0 && DATA_BYTES <= 255, "C3M data length is a nonzero wire U8");
static_assert(MAX_PACKETS <= 65535, "C3M packet indexes/counts are wire U16");
static_assert(PACKET_BYTES >= DATA_BYTES + 9 && PACKET_BYTES >= 17, "C3M packets must fit the radio payload");

enum class Status : std::uint8_t { OK, BAD_ARGUMENT, STALE, BUSY, INCOMPLETE, BAD_CRC, NO_PRODUCT };
enum class CacheState : std::uint8_t { EMPTY, RECEIVING, READY, SENDING, ERROR };

struct ProductKey {
    std::uint32_t session = 0;
    std::uint32_t generation = 0;
    std::uint32_t productId = 0;
    std::uint32_t totalBytes = 0;
    std::uint16_t crc16 = 0;
};

bool operator==(const ProductKey& left, const ProductKey& right);
bool operator!=(const ProductKey& left, const ProductKey& right);
std::uint16_t crc16Ccitt(const std::uint8_t* bytes, std::size_t length);

class Downlink;

// Single-owner, allocation-free engine. Storage is supplied by the deployment,
// allowing the target to place the only complete product copy in internal OCRAM.
class ProductCache {
  public:
    ProductCache(std::uint8_t* storage, std::size_t capacity);

    // Explicitly select an admitted Pi link session. A new session aborts an
    // incomplete stage, but committed data survives for Pi-independent downlink.
    void setSession(std::uint32_t session);
    Status begin(const ProductKey& key);
    Status append(const ProductKey& key, std::uint32_t offset, const std::uint8_t* bytes, std::size_t length);
    Status commit(const ProductKey& key);
    Status abort(const ProductKey& key);
    void reset();

    CacheState state() const { return m_state; }
    bool valid() const { return m_valid; }
    const ProductKey& key() const { return m_key; }
    std::uint32_t receivedBytes() const { return m_received; }
    std::uint32_t session() const { return m_session; }
    std::size_t capacity() const { return m_capacity; }
    bool read(std::uint32_t offset, std::uint8_t* bytes, std::size_t length) const;

  private:
    friend class Downlink;
    bool matches(const ProductKey& key) const;
    void invalidate(CacheState state);
    void sending(bool active);

    std::uint8_t* const m_storage;
    const std::size_t m_capacity;
    std::uint32_t m_session = 0;
    ProductKey m_key;
    std::uint32_t m_received = 0;
    CacheState m_state = CacheState::EMPTY;
    bool m_valid = false;
    std::uint32_t m_revision = 0;
};

struct Packet {
    std::array<std::uint8_t, PACKET_BYTES> bytes{};
    std::uint16_t length = 0;
};

// Emits the existing C3M N2 payload packets. This is above RF fragmentation;
// the production radio manager supplies channel-1 segmentation and transport.
class Downlink {
  public:
    explicit Downlink(ProductCache& cache);
    Status start();
    bool nextPacket(Packet& packet);
    // Call exactly once for each nextPacket result after local TX completion.
    // Failure retains the same packet for a later attempt.
    void packetSent(bool success);
    bool acceptRetry(const std::uint8_t* bytes, std::size_t length);
    // Stops transmission and repairs while retaining committed cached bytes.
    void stop();
    bool active() const;
    std::uint8_t transferId() const { return m_transferId; }
    std::uint16_t totalPackets() const { return m_totalPackets; }
    std::uint16_t pendingRepairs() const { return m_repairCount; }

  private:
    enum class Phase : std::uint8_t { IDLE, HEADER, DATA, REPAIR, END };
    bool current() const;
    bool repairBit(std::uint16_t index) const;
    void setRepairBit(std::uint16_t index);
    void clearRepairBit(std::uint16_t index);
    std::uint16_t nextRepair() const;

    ProductCache& m_cache;
    std::uint32_t m_revision = 0;
    std::uint8_t m_transferId = 0;
    std::uint16_t m_totalPackets = 0;
    std::uint16_t m_nextIndex = 0;
    std::uint8_t m_headersRemaining = 0;
    Phase m_phase = Phase::IDLE;
    bool m_pending = false;
    Phase m_pendingPhase = Phase::IDLE;
    std::uint16_t m_pendingIndex = 0;
    std::array<std::uint8_t, (MAX_PACKETS + 7U) / 8U> m_repairs{};
    std::uint16_t m_repairCount = 0;
    std::uint16_t m_repairCursor = 0;
    bool m_acceptRepairs = false;
};

}  // namespace Science
}  // namespace Artemis

#endif
