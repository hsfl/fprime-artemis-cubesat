#include "rf23_driver.hpp"

Rf23Driver::Rf23Driver(int csPin,
                       int irqPin,
                       uint8_t rxOnPin,
                       uint8_t txOnPin,
                       uint8_t sdnPin)
    : m_csPin(csPin),
      m_irqPin(irqPin),
      m_rxOnPin(rxOnPin),
      m_txOnPin(txOnPin),
      m_sdnPin(sdnPin),
      m_radio(csPin, irqPin, hardware_spi1),
      m_radioPins{},
      m_radioProfile{},
      m_state(Rf23State::OFF),
      m_fault(Rf23Fault::NONE),
      m_initAttempts(0),
      m_initFailures(0),
      m_sdnRecoveries(0),
      m_recoveringLocalTx(false),
      m_consecutiveTxTimeouts(0),
      m_txTimeoutRecoveryRequested(false),
      m_recoverySchedule{},
      m_faultSnapshot{},
      m_faultSnapshotPending(false)
#if defined(GDS_TX_LOAD_TEST)
      ,
      m_rejectedPacketSnapshot{},
      m_rejectedPacketSnapshotPending(false)
#endif
      {
  m_radioPins.cs_pin = static_cast<uint8_t>(csPin);
  m_radioPins.irq_pin = static_cast<uint8_t>(irqPin);
  m_radioPins.rx_on_pin = rxOnPin;
  m_radioPins.tx_on_pin = txOnPin;
  m_radioPins.sdn_pin = sdnPin;
}

void Rf23Driver::beginSafeOff() {
  m_recoverySchedule.succeeded();
  m_recoveringLocalTx = false;
  m_consecutiveTxTimeouts = 0;
  m_txTimeoutRecoveryRequested = false;
  enterOff(Rf23Fault::NONE);
}

bool Rf23Driver::begin() {
  if (isReady()) {
    return true;
  }
  m_recoverySchedule.requestImmediate(millis());
  return serviceRecovery();
}

bool Rf23Driver::serviceRecovery() {
  if (isReady() || !m_recoverySchedule.due(millis())) {
    return false;
  }

  m_initAttempts += 1U;
  if (!artemis::rf23bp::initRadio(m_radio, m_radioPins, m_radioProfile, &SerialUSB1)) {
    m_initFailures += 1U;
    enterOff(Rf23Fault::INIT_FAILED);
    m_recoverySchedule.failed(millis());
    return false;
  }
  m_radio.setPromiscuous(true);
  m_radio.setHeaderTo(link_protocol::RF_REMOTE_ADDRESS);
  m_radio.setHeaderFrom(link_protocol::RF_LOCAL_ADDRESS);
  m_radio.setHeaderId(link_protocol::RF_NETWORK_ID);
  m_radio.setHeaderFlags(link_protocol::RF_PROTOCOL_VERSION, 0xFF);
  m_state = Rf23State::READY;
  m_fault = Rf23Fault::NONE;
  m_recoverySchedule.succeeded();
  if (m_recoveringLocalTx) {
    m_sdnRecoveries += 1U;
    m_recoveringLocalTx = false;
  }
  return true;
}

void Rf23Driver::failSafeOffLocalTx() {
  if (!isReady() && m_recoveringLocalTx) {
    return;
  }
  // Leave TX before SDN so the radio enters shutdown from a known idle state.
  if (isReady()) {
    m_radio.setModeIdle();
    delay(1);
  }
  m_recoveringLocalTx = true;
  enterOff(Rf23Fault::LOCAL_TX);
  m_recoverySchedule.requestImmediate(millis());
}

bool Rf23Driver::isReady() const {
  return m_state == Rf23State::READY;
}

Rf23State Rf23Driver::state() const {
  return m_state;
}

Rf23Fault Rf23Driver::fault() const {
  return m_fault;
}

const char* Rf23Driver::stateName() const {
  return isReady() ? "READY" : "OFF";
}

const char* Rf23Driver::faultName() const {
  switch (m_fault) {
    case Rf23Fault::NONE:
      return "NONE";
    case Rf23Fault::INIT_FAILED:
      return "INIT_FAILED";
    case Rf23Fault::LOCAL_TX:
      return "LOCAL_TX";
  }
  return "UNKNOWN";
}

uint32_t Rf23Driver::initAttempts() const {
  return m_initAttempts;
}

uint32_t Rf23Driver::initFailures() const {
  return m_initFailures;
}

uint32_t Rf23Driver::sdnRecoveries() const {
  return m_sdnRecoveries;
}

bool Rf23Driver::recoveryPending() const {
  return m_recoverySchedule.pending();
}

uint32_t Rf23Driver::recoveryBackoffMs() const {
  return m_recoverySchedule.currentBackoffMs();
}

bool Rf23Driver::consumeFaultSnapshot(artemis::rf23bp::FaultSnapshot& snapshot) {
  if (!m_faultSnapshotPending) {
    return false;
  }
  snapshot = m_faultSnapshot;
  m_faultSnapshotPending = false;
  return true;
}

bool Rf23Driver::consumeTxTimeoutRecoveryRequest() {
  const bool requested = m_txTimeoutRecoveryRequested;
  m_txTimeoutRecoveryRequested = false;
  return requested;
}

bool Rf23Driver::available() {
  return isReady() && m_radio.available();
}

Rf23ReceiveResult Rf23Driver::recv(uint8_t* buf, uint8_t* len) {
  if (!isReady()) {
    return Rf23ReceiveResult::NO_PACKET;
  }
  if (!artemis::rf23bp::receivePacket(m_radio, m_radioPins, m_radioProfile, buf, len, nullptr)) {
    return Rf23ReceiveResult::NO_PACKET;
  }
  const link_protocol::RfHeaderStatus status = link_protocol::classifyRfHeader(
      m_radio.headerTo(), m_radio.headerFrom(), m_radio.headerId(), m_radio.headerFlags());
#if defined(GDS_TX_LOAD_TEST)
  if (status != link_protocol::RfHeaderStatus::ACCEPT && !m_rejectedPacketSnapshotPending) {
    Rf23ReceiveResult reason = Rf23ReceiveResult::NO_PACKET;
    switch (status) {
      case link_protocol::RfHeaderStatus::WRONG_NETWORK:
        reason = Rf23ReceiveResult::WRONG_NETWORK;
        break;
      case link_protocol::RfHeaderStatus::WRONG_ADDRESS:
        reason = Rf23ReceiveResult::WRONG_ADDRESS;
        break;
      case link_protocol::RfHeaderStatus::WRONG_VERSION:
        reason = Rf23ReceiveResult::WRONG_VERSION;
        break;
      case link_protocol::RfHeaderStatus::ACCEPT:
        break;
    }
    m_rejectedPacketSnapshot.valid = true;
    m_rejectedPacketSnapshot.reason = reason;
    m_rejectedPacketSnapshot.capturedMs = millis();
    m_rejectedPacketSnapshot.to = m_radio.headerTo();
    m_rejectedPacketSnapshot.from = m_radio.headerFrom();
    m_rejectedPacketSnapshot.network = m_radio.headerId();
    m_rejectedPacketSnapshot.version = m_radio.headerFlags();
    m_rejectedPacketSnapshot.length = *len;
    m_rejectedPacketSnapshot.rssiDbm = m_radio.lastRssi();
    m_rejectedPacketSnapshot.payloadPrefixLength =
        *len < Rf23RejectedPacketSnapshot::PAYLOAD_PREFIX_CAPACITY
            ? *len
            : Rf23RejectedPacketSnapshot::PAYLOAD_PREFIX_CAPACITY;
    memcpy(m_rejectedPacketSnapshot.payloadPrefix,
           buf,
           m_rejectedPacketSnapshot.payloadPrefixLength);
    m_rejectedPacketSnapshotPending = true;
  }
#endif
  switch (status) {
    case link_protocol::RfHeaderStatus::ACCEPT:
      return Rf23ReceiveResult::ACCEPTED;
    case link_protocol::RfHeaderStatus::WRONG_NETWORK:
      return Rf23ReceiveResult::WRONG_NETWORK;
    case link_protocol::RfHeaderStatus::WRONG_ADDRESS:
      return Rf23ReceiveResult::WRONG_ADDRESS;
    case link_protocol::RfHeaderStatus::WRONG_VERSION:
      return Rf23ReceiveResult::WRONG_VERSION;
  }
  return Rf23ReceiveResult::NO_PACKET;
}

Rf23SendResult Rf23Driver::send(const uint8_t* data, uint8_t len) {
  if (!isReady()) {
    return Rf23SendResult::START_FAILED;
  }

  artemis::rf23bp::FaultSnapshot attemptSnapshot;
  const Rf23SendResult result = artemis::rf23bp::sendPacket(
      m_radio,
      m_radioPins,
      m_radioProfile,
      data,
      len,
      link_protocol::RF_TX_COMPLETE_TIMEOUT_MS,
      &SerialUSB1,
      &attemptSnapshot);
  if (attemptSnapshot.valid && !m_faultSnapshotPending) {
    m_faultSnapshot = attemptSnapshot;
    m_faultSnapshotPending = true;
  }
  if (result == Rf23SendResult::SENT) {
    m_consecutiveTxTimeouts = 0;
    if (m_fault == Rf23Fault::LOCAL_TX) {
      m_fault = Rf23Fault::NONE;
    }
  } else {
    m_fault = Rf23Fault::LOCAL_TX;
    if (result == Rf23SendResult::TX_TIMEOUT) {
      static constexpr uint8_t TX_TIMEOUTS_BEFORE_RECOVERY = 3;
      m_consecutiveTxTimeouts += 1U;
      if (m_consecutiveTxTimeouts >= TX_TIMEOUTS_BEFORE_RECOVERY) {
        m_consecutiveTxTimeouts = 0;
        m_txTimeoutRecoveryRequested = true;
        failSafeOffLocalTx();
      }
    }
  }
  return result;
}

#if defined(GDS_TX_LOAD_TEST)
bool Rf23Driver::setTxPowerDbm(uint8_t dbm) {
  uint8_t setting = 0;
  switch (dbm) {
    case 28:
      setting = RH_RF22_RF23BP_TXPOW_28DBM;
      break;
    case 29:
      setting = RH_RF22_RF23BP_TXPOW_29DBM;
      break;
    case 30:
      setting = RH_RF22_RF23BP_TXPOW_30DBM;
      break;
    default:
      return false;
  }

  m_radioProfile.tx_power = setting;
  if (isReady()) {
    m_radio.setTxPower(setting);
  }
  return true;
}

uint8_t Rf23Driver::txPowerDbm() const {
  switch (m_radioProfile.tx_power) {
    case RH_RF22_RF23BP_TXPOW_28DBM:
      return 28;
    case RH_RF22_RF23BP_TXPOW_29DBM:
      return 29;
    case RH_RF22_RF23BP_TXPOW_30DBM:
      return 30;
    default:
      return 0;
  }
}

bool Rf23Driver::consumeRejectedPacketSnapshot(Rf23RejectedPacketSnapshot& snapshot) {
  if (!m_rejectedPacketSnapshotPending) {
    return false;
  }
  snapshot = m_rejectedPacketSnapshot;
  m_rejectedPacketSnapshotPending = false;
  return true;
}

Rf23HealthSnapshot Rf23Driver::captureHealthSnapshot() {
  Rf23HealthSnapshot snapshot;
  ATOMIC_BLOCK_START;
  snapshot.nirqLevel = static_cast<uint8_t>(digitalRead(m_irqPin));
  snapshot.radioheadMode = static_cast<uint8_t>(m_radio.mode());
  snapshot.csLevel = static_cast<uint8_t>(digitalRead(m_csPin));
  snapshot.rxOnLevel = static_cast<uint8_t>(digitalRead(m_rxOnPin));
  snapshot.txOnLevel = static_cast<uint8_t>(digitalRead(m_txOnPin));
  snapshot.sdnLevel = static_cast<uint8_t>(digitalRead(m_sdnPin));
  const uint8_t typeFirst = m_radio.spiRead(RH_RF22_REG_00_DEVICE_TYPE);
  const uint8_t versionFirst = m_radio.spiRead(RH_RF22_REG_01_VERSION_CODE);
  snapshot.deviceType = m_radio.spiRead(RH_RF22_REG_00_DEVICE_TYPE);
  snapshot.versionCode = m_radio.spiRead(RH_RF22_REG_01_VERSION_CODE);
  snapshot.identityStable = typeFirst == snapshot.deviceType &&
                            versionFirst == snapshot.versionCode &&
                            snapshot.deviceType == RH_RF22_DEVICE_TYPE_RX_TRX;
  snapshot.deviceStatus = m_radio.spiRead(RH_RF22_REG_02_DEVICE_STATUS);
  snapshot.interruptEnable1 = m_radio.spiRead(RH_RF22_REG_05_INTERRUPT_ENABLE1);
  snapshot.interruptEnable2 = m_radio.spiRead(RH_RF22_REG_06_INTERRUPT_ENABLE2);
  snapshot.operatingMode1 = m_radio.spiRead(RH_RF22_REG_07_OPERATING_MODE1);
  snapshot.operatingMode2 = m_radio.spiRead(RH_RF22_REG_08_OPERATING_MODE2);
  snapshot.dataAccessControl = m_radio.spiRead(RH_RF22_REG_30_DATA_ACCESS_CONTROL);
  snapshot.chargePump = m_radio.spiRead(RH_RF22_REG_58_CHARGE_PUMP_CURRENT_TRIMMING);
  snapshot.txPower = m_radio.spiRead(RH_RF22_REG_6D_TX_POWER);
  snapshot.frequencyBand = m_radio.spiRead(RH_RF22_REG_75_FREQUENCY_BAND_SELECT);
  snapshot.frequency1 = m_radio.spiRead(RH_RF22_REG_76_NOMINAL_CARRIER_FREQUENCY1);
  snapshot.frequency0 = m_radio.spiRead(RH_RF22_REG_77_NOMINAL_CARRIER_FREQUENCY0);
  snapshot.txFifoThreshold = m_radio.spiRead(RH_RF22_REG_7D_TX_FIFO_CONTROL2);
  snapshot.rxFifoThreshold = m_radio.spiRead(RH_RF22_REG_7E_RX_FIFO_CONTROL);
  ATOMIC_BLOCK_END;
  return snapshot;
}
#endif

void Rf23Driver::enterOff(Rf23Fault fault) {
  m_state = Rf23State::OFF;
  artemis::rf23bp::shutdownRadio(m_radioPins);
  m_fault = fault;
}
