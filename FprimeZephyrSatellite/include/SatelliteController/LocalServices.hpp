#pragma once

#include <cstddef>
#include <cstdint>

#include "SatelliteController/GeneratedProtocol.hpp"
#include "SatelliteController/Protocol.hpp"
#include "SatelliteController/RadioStateMachine.hpp"

namespace SatelliteController {

// Channel-2 endpoint IDs and payload operations are deliberately kept here,
// next to the Arduino-free service implementation.  Their values mirror the
// generated satellite link protocol; they are not an alternate wire format.
namespace LocalProtocol {
inline constexpr std::uint8_t HEADER_LEN = 4;
inline constexpr std::uint8_t TARGET_PDU = Generated::TEENSY_TARGET_PDU;
inline constexpr std::uint8_t TARGET_RF_STATUS = Generated::TEENSY_TARGET_RF_STATUS;
inline constexpr std::uint8_t TARGET_PAYLOAD_CACHE = Generated::TEENSY_TARGET_PAYLOAD_CACHE;
inline constexpr std::uint8_t TARGET_LEPTON_PREVIEW = Generated::TEENSY_TARGET_LEPTON_PREVIEW;

inline constexpr std::uint8_t STATUS_OK = Generated::TEENSY_STATUS_OK;
inline constexpr std::uint8_t STATUS_BAD_REQUEST = Generated::TEENSY_STATUS_BAD_REQUEST;
inline constexpr std::uint8_t STATUS_BUSY = Generated::TEENSY_STATUS_BUSY;
inline constexpr std::uint8_t STATUS_TIMEOUT = Generated::TEENSY_STATUS_TIMEOUT;
inline constexpr std::uint8_t STATUS_TARGET_ERROR = Generated::TEENSY_STATUS_TARGET_ERROR;

inline constexpr std::uint8_t RF_OP_STATUS = Generated::TEENSY_RF_OP_STATUS;
inline constexpr std::uint8_t RF_OP_SET_ENABLED = Generated::TEENSY_RF_OP_SET_ENABLED;

inline constexpr std::uint8_t CACHE_OP_BEGIN = Generated::PAYLOAD_CACHE_OP_BEGIN;
inline constexpr std::uint8_t CACHE_OP_CHUNK = Generated::PAYLOAD_CACHE_OP_CHUNK;
inline constexpr std::uint8_t CACHE_OP_COMMIT_AND_SEND = Generated::PAYLOAD_CACHE_OP_COMMIT_AND_SEND;
inline constexpr std::uint8_t CACHE_OP_ABORT = Generated::PAYLOAD_CACHE_OP_ABORT;
inline constexpr std::uint8_t CACHE_STATE_EMPTY = Generated::PAYLOAD_CACHE_STATE_EMPTY;
inline constexpr std::uint8_t CACHE_STATE_RECEIVING = Generated::PAYLOAD_CACHE_STATE_RECEIVING;
inline constexpr std::uint8_t CACHE_STATE_READY = Generated::PAYLOAD_CACHE_STATE_READY;
inline constexpr std::uint8_t CACHE_STATE_SENDING = Generated::PAYLOAD_CACHE_STATE_SENDING;
inline constexpr std::uint8_t CACHE_STATE_ERROR = Generated::PAYLOAD_CACHE_STATE_ERROR;

inline constexpr std::uint8_t PREVIEW_OP_BEGIN = Generated::PREVIEW_OP_BEGIN;
inline constexpr std::uint8_t PREVIEW_OP_CHUNK = Generated::PREVIEW_OP_CHUNK;
inline constexpr std::uint8_t PREVIEW_OP_COMMIT_AND_SEND = Generated::PREVIEW_OP_COMMIT_AND_SEND;
inline constexpr std::uint8_t PREVIEW_OP_ABORT = Generated::PREVIEW_OP_ABORT;
inline constexpr std::uint8_t PREVIEW_STATE_EMPTY = Generated::PREVIEW_STATE_EMPTY;
inline constexpr std::uint8_t PREVIEW_STATE_RECEIVING = Generated::PREVIEW_STATE_RECEIVING;
inline constexpr std::uint8_t PREVIEW_STATE_READY = Generated::PREVIEW_STATE_READY;
inline constexpr std::uint8_t PREVIEW_STATE_SENDING = Generated::PREVIEW_STATE_SENDING;
inline constexpr std::uint8_t PREVIEW_STATE_ERROR = Generated::PREVIEW_STATE_ERROR;
inline constexpr std::uint8_t PREVIEW_WIRE_MAGIC_0 = Generated::PREVIEW_WIRE_MAGIC_0;
inline constexpr std::uint8_t PREVIEW_WIRE_MAGIC_1 = Generated::PREVIEW_WIRE_MAGIC_1;
inline constexpr std::uint8_t PREVIEW_WIRE_VERSION = Generated::PREVIEW_WIRE_VERSION;
inline constexpr std::uint8_t PREVIEW_WIRE_TYPE_FRAGMENT = Generated::PREVIEW_WIRE_TYPE_FRAGMENT;
inline constexpr std::uint8_t PREVIEW_WIDTH = Generated::PREVIEW_WIDTH;
inline constexpr std::uint8_t PREVIEW_HEIGHT = Generated::PREVIEW_HEIGHT;
inline constexpr std::uint8_t PREVIEW_PIXEL_FORMAT_U8 = Generated::PREVIEW_PIXEL_FORMAT_U8;

inline constexpr std::uint8_t PAYLOAD_MAGIC_0 = Generated::PAYLOAD_MAGIC_0;
inline constexpr std::uint8_t PAYLOAD_MAGIC_1 = Generated::PAYLOAD_MAGIC_1;
inline constexpr std::uint8_t PAYLOAD_PACKET_HEADER = 1;
inline constexpr std::uint8_t PAYLOAD_PACKET_DATA = 2;
inline constexpr std::uint8_t PAYLOAD_PACKET_END = 3;
inline constexpr std::uint8_t PAYLOAD_PACKET_RETRY_REQUEST = 4;
inline constexpr std::size_t PAYLOAD_CACHE_MAX_BYTES = Generated::PAYLOAD_CACHE_MAX_BYTES;
inline constexpr std::uint16_t PAYLOAD_CACHE_CHUNK_BYTES = Generated::PAYLOAD_CACHE_CHUNK_BYTES;
inline constexpr std::uint8_t PAYLOAD_PACKET_DATA_BYTES = Generated::PAYLOAD_PACKET_DATA_BYTES;
inline constexpr std::uint16_t PAYLOAD_MAX_RETRY_PACKETS = 8U * 36U;
inline constexpr std::size_t PREVIEW_MAX_FRAME_BYTES = Generated::PREVIEW_MAX_FRAME_BYTES;
inline constexpr std::uint16_t PREVIEW_CHUNK_BYTES = Generated::PREVIEW_CHUNK_BYTES;
inline constexpr std::uint8_t RF_STATUS_PAYLOAD_LEN = 33;
inline constexpr std::uint8_t RF_SET_ENABLED_PAYLOAD_LEN = 4;
inline constexpr std::uint32_t PDU_RESPONSE_TIMEOUT_MS = 350;
inline constexpr std::size_t PDU_V2_HEADER_LEN = 7;
inline constexpr std::size_t PDU_V2_CRC_LEN = 2;
inline constexpr std::size_t PDU_V2_MAX_PAYLOAD_LEN = 96;
inline constexpr std::size_t PDU_V2_MAX_FRAME_LEN = PDU_V2_HEADER_LEN + PDU_V2_MAX_PAYLOAD_LEN + PDU_V2_CRC_LEN;
}  // namespace LocalProtocol

struct RfStatusSnapshot {
    std::int16_t lastRssiDbm = -127;
    std::uint16_t rxGood = 0;
    std::uint16_t rxBad = 0;
    std::uint16_t txGood = 0;
    std::uint32_t rfRxPackets = 0;
    std::uint32_t rfTxPackets = 0;
    std::uint32_t rfTxDrops = 0;
    RadioState state = RadioState::OFF;
    RadioFault fault = RadioFault::NONE;
    std::uint8_t bootFlags = 0;
    bool rssiValid = false;
    std::uint32_t lastAcceptedRssiAgeMs = 0xFFFFFFFFU;
    std::uint32_t initAttempts = 0;
};

// Hardware adapters implement this small interface around the Zephyr radio
// driver.  The local services never allocate and never own the adapter.
class RfStatusProvider {
  public:
    virtual ~RfStatusProvider() = default;
    virtual bool setEnabled(bool enabled) = 0;
    virtual RfStatusSnapshot status() const = 0;
};

// The PDU UART adapter is intentionally byte-oriented so ISR code can remain
// a signal-only path and the PDU parser can run in a worker thread.
class PduTransport {
  public:
    virtual ~PduTransport() = default;
    virtual void discardRx() = 0;
    virtual bool write(const std::uint8_t* data, std::size_t length) = 0;
    virtual void flush() = 0;
    virtual bool readByte(std::uint8_t& value) = 0;
};

class PduProxy final {
  public:
    explicit PduProxy(PduTransport& transport);

    bool beginLocalFrame(const std::uint8_t* payload, std::size_t length);
    bool beginLocalFrame(const std::uint8_t* payload, std::size_t length, std::uint32_t nowMs);
    bool pollLocalResponse(std::uint32_t nowMs,
                           std::uint8_t* payload,
                           std::size_t capacity,
                           std::size_t& length);
    bool hasPending() const;

  private:
    enum class State : std::uint8_t { IDLE, WAITING_FOR_PDU, RESPONSE_READY };
    void resetRx();
    void prepareResponse(std::uint8_t requestId,
                         std::uint8_t status,
                         const std::uint8_t* pduFrame = nullptr,
                         std::size_t pduFrameLen = 0);
    bool copyResponse(std::uint8_t* payload, std::size_t capacity, std::size_t& length);
    void pollPduUart();

    PduTransport& m_transport;
    State m_state = State::IDLE;
    std::uint8_t m_requestId = 0;
    std::uint8_t m_pduRx[LocalProtocol::PDU_V2_MAX_FRAME_LEN]{};
    std::size_t m_pduRxLen = 0;
    std::size_t m_expectedPduLen = 0;
    std::uint32_t m_deadlineMs = 0;
    bool m_deadlineStarted = false;
    std::uint8_t m_response[LocalProtocol::HEADER_LEN + LocalProtocol::PDU_V2_MAX_FRAME_LEN]{};
    std::size_t m_responseLen = 0;
    // A BUSY response must not destroy the in-flight PDU request. One queued
    // BUSY slot is enough for the serialized Pi client contract.
    std::uint8_t m_busyResponse[LocalProtocol::HEADER_LEN + LocalProtocol::PDU_V2_MAX_FRAME_LEN]{};
    std::size_t m_busyResponseLen = 0;
};

class PayloadCacheService final {
  public:
    PayloadCacheService();

    bool beginLocalFrame(const std::uint8_t* payload, std::size_t length);
    bool pollLocalResponse(std::uint8_t* payload, std::size_t capacity, std::size_t& length);
    bool isTransferActive() const;
    void rejectBusy(std::uint8_t requestId, std::uint8_t operation);

    bool handlePayloadControl(const std::uint8_t* payload, std::size_t length);
    bool nextPayloadPacket(std::uint8_t* payload, std::size_t capacity, std::size_t& length);
    void payloadPacketSent(bool sent);
    std::uint8_t state() const { return m_state; }
    bool valid() const { return m_valid; }

  private:
    enum class TxPhase : std::uint8_t { IDLE, HEADERS, DATA, REPAIR, END };
    void handleBegin(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void handleChunk(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void handleCommit(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void handleAbort(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void prepareResponse(std::uint8_t requestId, std::uint8_t status, std::uint8_t operation);
    void startTransmit(std::uint8_t requestId);
    void invalidate(std::uint8_t state);
    bool identityMatches(std::uint8_t transferId,
                         std::uint32_t productId,
                         std::uint32_t totalBytes,
                         std::uint16_t crc) const;
    std::uint16_t cacheCrc() const;
    static std::uint16_t readLe16(const std::uint8_t* data);
    static std::uint32_t readLe32(const std::uint8_t* data);
    static void writeLe16(std::uint8_t* data, std::uint16_t value);
    static void writeLe32(std::uint8_t* data, std::uint32_t value);

    // Keep this as one fixed object. Platform linker placement may move the
    // array to OCRAM, but nominal operation must not use heap allocation.
    std::uint8_t m_cache[LocalProtocol::PAYLOAD_CACHE_MAX_BYTES]{};
    std::uint8_t m_state = LocalProtocol::CACHE_STATE_EMPTY;
    std::uint8_t m_transferId = 0;
    std::uint32_t m_productId = 0;
    std::uint32_t m_totalBytes = 0;
    std::uint32_t m_receivedBytes = 0;
    std::uint16_t m_expectedCrc = 0;
    bool m_valid = false;
    TxPhase m_txPhase = TxPhase::IDLE;
    std::uint8_t m_headersRemaining = 0;
    std::uint16_t m_nextPacketIndex = 0;
    std::uint16_t m_totalPackets = 0;
    std::uint16_t m_retryPackets[LocalProtocol::PAYLOAD_MAX_RETRY_PACKETS]{};
    std::uint16_t m_retryCount = 0;
    std::uint16_t m_retryCursor = 0;
    bool m_packetPending = false;
    std::uint8_t m_commitRequestId = 0;
    std::uint8_t m_response[LocalProtocol::HEADER_LEN + 8]{};
    std::size_t m_responseLen = 0;
};

class PreviewService final {
  public:
    PreviewService();

    bool beginLocalFrame(const std::uint8_t* payload, std::size_t length);
    bool pollLocalResponse(std::uint8_t* payload, std::size_t capacity, std::size_t& length);
    bool isTransferActive() const;
    void rejectBusy(std::uint8_t requestId, std::uint8_t operation);
    bool nextPreviewPacket(std::uint8_t* payload, std::size_t capacity, std::size_t& length);
    void previewPacketAttempted(bool sent);

  private:
    void handleBegin(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void handleChunk(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void handleCommit(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void handleAbort(std::uint8_t requestId, const std::uint8_t* body, std::size_t bodyLen);
    void prepareResponse(std::uint8_t requestId, std::uint8_t status, std::uint8_t operation);
    void invalidate(std::uint8_t state);
    bool identityMatches(std::uint16_t session,
                         std::uint32_t frameSequence,
                         std::uint16_t totalBytes,
                         std::uint16_t crc) const;
    std::uint16_t frameCrc() const;
    static std::uint16_t readLe16(const std::uint8_t* data);
    static std::uint32_t readLe32(const std::uint8_t* data);
    static void writeLe16(std::uint8_t* data, std::uint16_t value);
    static void writeLe32(std::uint8_t* data, std::uint32_t value);

    std::uint8_t m_frame[LocalProtocol::PREVIEW_MAX_FRAME_BYTES]{};
    std::uint8_t m_state = LocalProtocol::PREVIEW_STATE_EMPTY;
    std::uint16_t m_session = 0;
    std::uint32_t m_frameSequence = 0;
    std::uint16_t m_totalBytes = 0;
    std::uint16_t m_receivedBytes = 0;
    std::uint16_t m_expectedCrc = 0;
    std::uint8_t m_width = 0;
    std::uint8_t m_height = 0;
    std::uint8_t m_pixelFormat = 0;
    std::uint16_t m_fragmentCount = 0;
    std::uint16_t m_nextFragment = 0;
    bool m_pending = false;
    bool m_anySendFailure = false;
    std::uint8_t m_commitRequestId = 0;
    std::uint8_t m_response[LocalProtocol::HEADER_LEN + 8]{};
    std::size_t m_responseLen = 0;
};

class LocalServicesRouter final {
  public:
    LocalServicesRouter(RfStatusProvider& radio,
                        PduProxy& pdu,
                        PayloadCacheService& cache,
                        PreviewService& preview);

    bool beginLocalFrame(const std::uint8_t* payload, std::size_t length);
    bool beginLocalFrame(const std::uint8_t* payload, std::size_t length, std::uint32_t nowMs);
    bool pollLocalResponse(std::uint32_t nowMs,
                           std::uint8_t* payload,
                           std::size_t capacity,
                           std::size_t& length);

  private:
    void prepareErrorResponse(std::uint8_t requestId,
                              std::uint8_t status,
                              std::uint8_t target = LocalProtocol::TARGET_RF_STATUS);
    void prepareRfStatusResponse(std::uint8_t requestId);
    void prepareRfSetEnabledResponse(std::uint8_t requestId, bool enabled);
    static void writeLe16(std::uint8_t* out, std::uint16_t value);
    static void writeLe32(std::uint8_t* out, std::uint32_t value);

    RfStatusProvider& m_radio;
    PduProxy& m_pdu;
    PayloadCacheService& m_cache;
    PreviewService& m_preview;
    std::uint8_t m_rfResponse[LocalProtocol::HEADER_LEN + LocalProtocol::RF_STATUS_PAYLOAD_LEN]{};
    std::size_t m_rfResponseLen = 0;
};

// Software model for the 12-second watchdog contract. Hardware integration
// can call boot/feed/expired from the Zephyr watchdog adapter without making
// the channel-2 service depend on a particular watchdog driver.
class WatchdogModel final {
  public:
    static constexpr std::uint32_t TIMEOUT_MS = 12000;
    void boot(bool watchdogReset, std::uint32_t nowMs = 0);
    void feed(std::uint32_t nowMs);
    bool expired(std::uint32_t nowMs) const;
    bool watchdogReset() const { return m_watchdogReset; }

  private:
    std::uint32_t m_lastFeedMs = 0;
    bool m_watchdogReset = false;
};

}  // namespace SatelliteController
