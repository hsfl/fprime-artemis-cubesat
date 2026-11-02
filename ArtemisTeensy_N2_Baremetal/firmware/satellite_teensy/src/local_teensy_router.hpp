#ifndef ARTEMIS_TEENSY_LOCAL_TEENSY_ROUTER_HPP
#define ARTEMIS_TEENSY_LOCAL_TEENSY_ROUTER_HPP

#include <Arduino.h>

#include "pdu_proxy.hpp"
#include "relay_uart_rf.hpp"
#if !defined(ARTEMIS_CDR_NO_RADIO)
#include "rf23_driver.hpp"
#endif

class LocalTeensyRouter : public LocalChannelHandler {
 public:
#if defined(ARTEMIS_CDR_NO_RADIO)
  LocalTeensyRouter(PduProxy& pduProxy, LinkCounters& counters);
#else
  LocalTeensyRouter(PduProxy& pduProxy, Rf23Driver& rfDriver, LinkCounters& counters);
#endif

  bool beginLocalFrame(const uint8_t* payload, uint16_t length) override;
  bool pollLocalResponse(uint8_t* payload, uint16_t& length) override;

 private:
  static constexpr uint8_t LOCAL_HEADER_LEN = 4;
  static constexpr uint8_t RF_STATS_PAYLOAD_LEN = 21;
  static constexpr uint16_t LOCAL_MAX_RESPONSE_LEN = LOCAL_HEADER_LEN + RF_STATS_PAYLOAD_LEN;

  void prepareRfStatusResponse(uint8_t requestId, uint8_t status);
  static void writeLe16(uint8_t* out, uint16_t value);
  static void writeLe32(uint8_t* out, uint32_t value);

  PduProxy& m_pduProxy;
#if !defined(ARTEMIS_CDR_NO_RADIO)
  Rf23Driver& m_rfDriver;
#endif
  LinkCounters& m_counters;
  uint8_t m_rfResponse[LOCAL_MAX_RESPONSE_LEN];
  uint16_t m_rfResponseLen;
};

#endif
