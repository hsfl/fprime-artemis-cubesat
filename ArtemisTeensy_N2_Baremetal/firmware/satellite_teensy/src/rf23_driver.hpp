#ifndef ARTEMIS_TEENSY_RF23_DRIVER_HPP
#define ARTEMIS_TEENSY_RF23_DRIVER_HPP

#include <Arduino.h>
#include "artemis_rf23bp.hpp"
#include "link_protocol.hpp"

enum class Rf23ReceiveResult : uint8_t {
  NO_PACKET = 0,
  ACCEPTED = 1,
  WRONG_NETWORK = 2,
  WRONG_ADDRESS = 3,
  WRONG_VERSION = 4,
};

using Rf23SendResult = artemis::rf23bp::SendResult;

class Rf23Driver {
 public:
  Rf23Driver(int csPin, int irqPin, uint8_t rxOnPin, uint8_t txOnPin, uint8_t sdnPin);

  void beginSafeOff();
  bool begin();
  bool setEnabled(bool enabled);
  void failSafeOffLocalTx();
  bool isReady() const;
  uint8_t state() const;
  uint8_t fault() const;
  uint8_t bootFlags() const;
  uint32_t initAttempts() const;
  bool rssiValid() const;
  int16_t lastAcceptedRssiDbm() const;
  uint32_t lastAcceptedRssiAgeMs() const;
  bool consumeTxTimeoutRecoveryRequest();
  bool receiveInProgress();
  bool available();
  Rf23ReceiveResult recv(uint8_t* buf, uint8_t* len);
  Rf23SendResult send(const uint8_t* data, uint8_t len);
  artemis::rf23bp::LinkStats linkStats();

 private:
  void enterOff(uint8_t fault);

  int m_csPin;
  int m_irqPin;
  uint8_t m_rxOnPin;
  uint8_t m_txOnPin;
  uint8_t m_sdnPin;
  artemis::rf23bp::BoundedRf22 m_radio;
  artemis::rf23bp::RadioPins m_radioPins;
  artemis::rf23bp::RadioProfile m_radioProfile;
  uint8_t m_state;
  uint8_t m_fault;
  uint8_t m_bootFlags;
  uint32_t m_initAttempts;
  uint8_t m_consecutiveTxTimeouts;
  bool m_txTimeoutRecoveryRequested;
  bool m_rssiValid;
  int16_t m_lastAcceptedRssiDbm;
  uint32_t m_lastAcceptedRssiMs;
  artemis::rf23bp::LinkStats m_cachedStats;
};

#endif
