#ifndef ARTEMIS_TEENSY_RF23_DRIVER_HPP
#define ARTEMIS_TEENSY_RF23_DRIVER_HPP

#include <Arduino.h>
#include "artemis_rf23bp.hpp"
#include "link_protocol.hpp"
#include "rf_recovery_policy.hpp"

enum class Rf23ReceiveResult : uint8_t {
  NO_PACKET = 0,
  ACCEPTED = 1,
  WRONG_NETWORK = 2,
  WRONG_ADDRESS = 3,
  WRONG_VERSION = 4,
};

using Rf23SendResult = artemis::rf23bp::SendResult;

enum class Rf23State : uint8_t {
  OFF = 0,
  READY = 1,
};

enum class Rf23Fault : uint8_t {
  NONE = 0,
  INIT_FAILED = 1,
  LOCAL_TX = 2,
};

#if defined(GDS_TX_LOAD_TEST)
struct Rf23RejectedPacketSnapshot {
  static constexpr uint8_t PAYLOAD_PREFIX_CAPACITY = 16;

  bool valid = false;
  Rf23ReceiveResult reason = Rf23ReceiveResult::NO_PACKET;
  uint32_t capturedMs = 0;
  uint8_t to = 0;
  uint8_t from = 0;
  uint8_t network = 0;
  uint8_t version = 0;
  uint8_t length = 0;
  int16_t rssiDbm = 0;
  uint8_t payloadPrefixLength = 0;
  uint8_t payloadPrefix[PAYLOAD_PREFIX_CAPACITY] = {0};
};

struct Rf23HealthSnapshot {
  bool identityStable = false;
  uint8_t nirqLevel = 0xFF;
  uint8_t radioheadMode = 0xFF;
  uint8_t csLevel = 0xFF;
  uint8_t rxOnLevel = 0xFF;
  uint8_t txOnLevel = 0xFF;
  uint8_t sdnLevel = 0xFF;
  uint8_t deviceType = 0;
  uint8_t versionCode = 0;
  uint8_t deviceStatus = 0;
  uint8_t interruptEnable1 = 0;
  uint8_t interruptEnable2 = 0;
  uint8_t operatingMode1 = 0;
  uint8_t operatingMode2 = 0;
  uint8_t dataAccessControl = 0;
  uint8_t chargePump = 0;
  uint8_t txPower = 0;
  uint8_t frequencyBand = 0;
  uint8_t frequency1 = 0;
  uint8_t frequency0 = 0;
  uint8_t txFifoThreshold = 0;
  uint8_t rxFifoThreshold = 0;
};
#endif

class Rf23Driver {
 public:
  Rf23Driver(int csPin,
             int irqPin,
             uint8_t rxOnPin,
             uint8_t txOnPin,
             uint8_t sdnPin);

  void beginSafeOff();
  bool begin();
  bool serviceRecovery();
  void failSafeOffLocalTx();
  bool isReady() const;
  Rf23State state() const;
  Rf23Fault fault() const;
  const char* stateName() const;
  const char* faultName() const;
  uint32_t initAttempts() const;
  uint32_t initFailures() const;
  uint32_t sdnRecoveries() const;
  bool recoveryPending() const;
  uint32_t recoveryBackoffMs() const;
  bool consumeFaultSnapshot(artemis::rf23bp::FaultSnapshot& snapshot);
  bool consumeTxTimeoutRecoveryRequest();
  bool available();
  Rf23ReceiveResult recv(uint8_t* buf, uint8_t* len);
  Rf23SendResult send(const uint8_t* data, uint8_t len);
#if defined(GDS_TX_LOAD_TEST)
  bool setTxPowerDbm(uint8_t dbm);
  uint8_t txPowerDbm() const;
  bool consumeRejectedPacketSnapshot(Rf23RejectedPacketSnapshot& snapshot);
  Rf23HealthSnapshot captureHealthSnapshot();
#endif

 private:
  void enterOff(Rf23Fault fault);

  int m_csPin;
  int m_irqPin;
  uint8_t m_rxOnPin;
  uint8_t m_txOnPin;
  uint8_t m_sdnPin;
  artemis::rf23bp::BoundedRf22 m_radio;
  artemis::rf23bp::RadioPins m_radioPins;
  artemis::rf23bp::RadioProfile m_radioProfile;
  Rf23State m_state;
  Rf23Fault m_fault;
  uint32_t m_initAttempts;
  uint32_t m_initFailures;
  uint32_t m_sdnRecoveries;
  bool m_recoveringLocalTx;
  uint8_t m_consecutiveTxTimeouts;
  bool m_txTimeoutRecoveryRequested;
  rf_recovery::Schedule m_recoverySchedule;
  artemis::rf23bp::FaultSnapshot m_faultSnapshot;
  bool m_faultSnapshotPending;
#if defined(GDS_TX_LOAD_TEST)
  Rf23RejectedPacketSnapshot m_rejectedPacketSnapshot;
  bool m_rejectedPacketSnapshotPending;
#endif
};

#endif
