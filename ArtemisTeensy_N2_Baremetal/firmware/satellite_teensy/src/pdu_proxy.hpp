#ifndef ARTEMIS_TEENSY_PDU_PROXY_HPP
#define ARTEMIS_TEENSY_PDU_PROXY_HPP

#include <Arduino.h>

#include <pdu_protocol_v2.h>
#include "link_protocol.hpp"
#include "relay_uart_rf.hpp"

class PduProxy : public LocalChannelHandler {
 public:
  explicit PduProxy(HardwareSerial& pduUart);

  void begin(uint32_t baud);
  bool beginLocalFrame(const uint8_t* payload, uint16_t length) override;
  bool pollLocalResponse(uint8_t* payload, uint16_t& length) override;

 private:
  enum class State {
    IDLE,
    WAITING_FOR_PDU,
    RESPONSE_READY,
  };

  static constexpr uint8_t LOCAL_HEADER_LEN = 4;
  static constexpr uint32_t PDU_RESPONSE_TIMEOUT_MS = 350;
  static constexpr uint16_t LOCAL_MAX_RESPONSE_LEN = LOCAL_HEADER_LEN + PDU_V2_MAX_FRAME_LEN;

  void prepareLocalResponse(uint8_t requestId, uint8_t status, const uint8_t* pduFrame, uint16_t pduFrameLen);
  void pollPduUart();
  void resetPduRx();

  HardwareSerial& m_pduUart;
  State m_state;
  uint8_t m_requestId;
  uint8_t m_pduRx[PDU_V2_MAX_FRAME_LEN];
  uint16_t m_pduRxLen;
  uint16_t m_expectedPduLen;
  uint32_t m_deadlineMs;
  uint8_t m_localResponse[LOCAL_MAX_RESPONSE_LEN];
  uint16_t m_localResponseLen;
};

#endif
