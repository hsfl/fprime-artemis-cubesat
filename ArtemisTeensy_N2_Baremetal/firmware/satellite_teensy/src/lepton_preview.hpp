#ifndef ARTEMIS_TEENSY_LEPTON_PREVIEW_HPP
#define ARTEMIS_TEENSY_LEPTON_PREVIEW_HPP

#include <Arduino.h>

#include "relay_uart_rf.hpp"

struct LinkCounters;

// Stages one 80x60 U8 Lepton preview independently of PayloadCache. The
// preview is intentionally lossy: every committed fragment is attempted once
// over channel 1 and can be rendered partially by the ground receiver.
class LeptonPreview : public PreviewChannelHandler {
 public:
  explicit LeptonPreview(LinkCounters& counters);

  bool beginLocalFrame(const uint8_t* payload, uint16_t length);
  bool pollLocalResponse(uint8_t* payload, uint16_t& length);
  bool isTransferActive() const;
  void rejectBusy(uint8_t requestId, uint8_t operation);

  bool nextPreviewPacket(uint8_t* payload, uint16_t& length) override;
  void previewPacketAttempted(bool sent) override;

 private:
  static constexpr uint8_t LOCAL_HEADER_LEN = 4U;
  static constexpr uint8_t RESPONSE_BODY_LEN = 8U;
  static constexpr uint8_t PREVIEW_WIRE_HEADER_LEN = 20U;
  static constexpr uint8_t PREVIEW_FRAGMENT_DATA_BYTES =
      link_protocol::RF_SEGMENT_MAX_DATA - PREVIEW_WIRE_HEADER_LEN;

  void handleBegin(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void handleChunk(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void handleCommit(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void handleAbort(uint8_t requestId, const uint8_t* body, uint8_t bodyLen);
  void prepareResponse(uint8_t requestId, uint8_t status, uint8_t operation);
  void invalidate(uint8_t state);
  bool identityMatches(uint16_t session, uint32_t frameSequence, uint16_t totalBytes, uint16_t crc) const;
  uint16_t crc16Ccitt(const uint8_t* data, uint16_t length) const;
  static uint16_t readLe16(const uint8_t* data);
  static uint32_t readLe32(const uint8_t* data);
  static void writeLe16(uint8_t* data, uint16_t value);
  static void writeLe32(uint8_t* data, uint32_t value);

  uint8_t m_frame[link_protocol::LEPTON_PREVIEW_MAX_FRAME_BYTES];
  uint8_t m_state;
  uint16_t m_session;
  uint32_t m_frameSequence;
  uint16_t m_totalBytes;
  uint16_t m_receivedBytes;
  uint16_t m_expectedCrc;
  uint8_t m_width;
  uint8_t m_height;
  uint8_t m_pixelFormat;
  uint8_t m_fragmentCount;
  uint8_t m_nextFragment;
  bool m_pending;
  bool m_anySendFailure;
  uint8_t m_commitRequestId;

  uint8_t m_response[LOCAL_HEADER_LEN + RESPONSE_BODY_LEN];
  uint16_t m_responseLen;
  LinkCounters& m_counters;
};

#endif
