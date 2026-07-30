#ifndef ARTEMIS_TEENSY_PAYLOAD_CACHE_HPP
#define ARTEMIS_TEENSY_PAYLOAD_CACHE_HPP

#include <Arduino.h>

#include "relay_uart_rf.hpp"

class PayloadCache : public PayloadChannelHandler {
 public:
  PayloadCache();

  bool beginLocalFrame(const uint8_t* payload, uint16_t length);
  bool pollLocalResponse(uint8_t* payload, uint16_t& length);

  bool handlePayloadControl(const uint8_t* payload, uint16_t length) override;
  bool nextPayloadPacket(uint8_t* payload, uint16_t& length) override;
  void payloadPacketSent(bool sent) override;

 private:
  enum PacketType : uint8_t {
    PACKET_HEADER = 1,
    PACKET_DATA = 2,
    PACKET_END = 3,
    PACKET_RETRY_REQUEST = 4,
  };

  enum TxPhase : uint8_t {
    TX_IDLE = 0,
    TX_HEADERS = 1,
    TX_DATA = 2,
    TX_REPAIR = 3,
    TX_END = 4,
  };

  static constexpr uint8_t LOCAL_HEADER_LEN = 4;
  static constexpr uint8_t RESPONSE_BODY_LEN = 8;
  static constexpr uint16_t MAX_RETRY_PACKETS = 8U * 36U;

  void handleBegin(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void handleChunk(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void handleCommit(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void handleAbort(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void prepareResponse(uint8_t requestId, uint8_t status, uint8_t operation);
  void startTransmit(uint8_t requestId);
  void invalidate(uint8_t state);
  bool identityMatches(uint8_t transferId, uint32_t productId, uint32_t totalBytes, uint16_t crc) const;
  uint16_t crc16Ccitt(const uint8_t* data, uint32_t length) const;
  static uint16_t readLe16(const uint8_t* data);
  static uint32_t readLe32(const uint8_t* data);
  static void writeLe16(uint8_t* data, uint16_t value);
  static void writeLe32(uint8_t* data, uint32_t value);

  uint8_t m_cache[link_protocol::PAYLOAD_CACHE_MAX_BYTES];
  uint8_t m_state;
  uint8_t m_transferId;
  uint32_t m_productId;
  uint32_t m_totalBytes;
  uint32_t m_receivedBytes;
  uint16_t m_expectedCrc;
  bool m_valid;

  TxPhase m_txPhase;
  uint8_t m_headersRemaining;
  uint16_t m_nextPacketIndex;
  uint16_t m_totalPackets;
  uint16_t m_retryPackets[MAX_RETRY_PACKETS];
  uint16_t m_retryCount;
  uint16_t m_retryCursor;
  bool m_packetPending;
  uint8_t m_commitRequestId;

  uint8_t m_response[LOCAL_HEADER_LEN + RESPONSE_BODY_LEN];
  uint16_t m_responseLen;
};

#endif
