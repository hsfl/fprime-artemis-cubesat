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
      m_recoverySchedule{},
      m_faultSnapshot{},
      m_faultSnapshotPending(false) {
  m_radioPins.cs_pin = static_cast<uint8_t>(csPin);
  m_radioPins.irq_pin = static_cast<uint8_t>(irqPin);
  m_radioPins.rx_on_pin = rxOnPin;
  m_radioPins.tx_on_pin = txOnPin;
  m_radioPins.sdn_pin = sdnPin;
}

void Rf23Driver::beginSafeOff() {
  m_recoverySchedule.succeeded();
  m_recoveringLocalTx = false;
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
    if (m_fault == Rf23Fault::LOCAL_TX) {
      m_fault = Rf23Fault::NONE;
    }
  } else {
    m_fault = Rf23Fault::LOCAL_TX;
  }
  return result;
}

void Rf23Driver::enterOff(Rf23Fault fault) {
  m_state = Rf23State::OFF;
  artemis::rf23bp::shutdownRadio(m_radioPins);
  m_fault = fault;
}
