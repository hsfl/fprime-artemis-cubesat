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
      m_state(link_protocol::TEENSY_RF_STATE_OFF),
      m_fault(link_protocol::TEENSY_RF_FAULT_NONE),
      m_bootFlags(0),
      m_initAttempts(0),
      m_rssiValid(false),
      m_lastAcceptedRssiDbm(0),
      m_lastAcceptedRssiMs(0),
      m_cachedStats{} {
  m_radioPins.cs_pin = static_cast<uint8_t>(csPin);
  m_radioPins.irq_pin = static_cast<uint8_t>(irqPin);
  m_radioPins.rx_on_pin = rxOnPin;
  m_radioPins.tx_on_pin = txOnPin;
  m_radioPins.sdn_pin = sdnPin;
}

void Rf23Driver::beginSafeOff(bool watchdogReset) {
  m_bootFlags = watchdogReset ? link_protocol::TEENSY_RF_BOOT_FLAG_WATCHDOG : 0U;
  enterOff(watchdogReset ? link_protocol::TEENSY_RF_FAULT_WATCHDOG_RESET
                         : link_protocol::TEENSY_RF_FAULT_NONE);
}

bool Rf23Driver::begin() {
  return setEnabled(true);
}

bool Rf23Driver::setEnabled(bool enabled) {
  if (!enabled) {
    enterOff(link_protocol::TEENSY_RF_FAULT_NONE);
    return true;
  }

  if (isReady() && m_fault == link_protocol::TEENSY_RF_FAULT_NONE) {
    return true;
  }

  // READY with a factual local fault is not healthy readiness. Re-enter the
  // same SDN POR path instead of allowing an idempotent enable to preserve a
  // wedged hardware context.
  if (isReady()) {
    enterOff(m_fault);
  }

  m_initAttempts += 1U;
  if (!artemis::rf23bp::initRadio(m_radio, m_radioPins, m_radioProfile, &Serial)) {
    enterOff(link_protocol::TEENSY_RF_FAULT_INIT_FAILED);
    return false;
  }
  m_radio.setPromiscuous(true);
  m_radio.setHeaderTo(link_protocol::RF_REMOTE_ADDRESS);
  m_radio.setHeaderFrom(link_protocol::RF_LOCAL_ADDRESS);
  m_radio.setHeaderId(link_protocol::RF_NETWORK_ID);
  m_radio.setHeaderFlags(link_protocol::RF_PROTOCOL_VERSION, 0xFF);
  m_state = link_protocol::TEENSY_RF_STATE_READY;
  m_fault = link_protocol::TEENSY_RF_FAULT_NONE;
  return true;
}

void Rf23Driver::failSafeOffLocalTx() {
  enterOff(link_protocol::TEENSY_RF_FAULT_LOCAL_TX);
}

bool Rf23Driver::isReady() const {
  return m_state == link_protocol::TEENSY_RF_STATE_READY;
}

uint8_t Rf23Driver::state() const {
  return m_state;
}

uint8_t Rf23Driver::fault() const {
  return m_fault;
}

uint8_t Rf23Driver::bootFlags() const {
  return m_bootFlags;
}

uint32_t Rf23Driver::initAttempts() const {
  return m_initAttempts;
}

bool Rf23Driver::rssiValid() const {
  return m_rssiValid;
}

int16_t Rf23Driver::lastAcceptedRssiDbm() const {
  return m_lastAcceptedRssiDbm;
}

uint32_t Rf23Driver::lastAcceptedRssiAgeMs() const {
  if (!m_rssiValid) {
    return link_protocol::TEENSY_RF_RSSI_AGE_UNKNOWN_MS;
  }
  return millis() - m_lastAcceptedRssiMs;
}

bool Rf23Driver::available() {
  return isReady() && m_radio.available();
}

bool Rf23Driver::receiveInProgress() {
  return isReady() && m_radio.receiveInProgress(millis());
}

Rf23ReceiveResult Rf23Driver::recv(uint8_t* buf, uint8_t* len) {
  if (!isReady()) {
    return Rf23ReceiveResult::NO_PACKET;
  }

  int16_t receivedRssiDbm = 0;
  if (!artemis::rf23bp::receivePacket(
          m_radio, m_radioPins, m_radioProfile, buf, len, &receivedRssiDbm)) {
    return Rf23ReceiveResult::NO_PACKET;
  }
  const link_protocol::RfHeaderStatus status = link_protocol::classifyRfHeader(
      m_radio.headerTo(), m_radio.headerFrom(), m_radio.headerId(), m_radio.headerFlags());
  switch (status) {
    case link_protocol::RfHeaderStatus::ACCEPT:
      m_rssiValid = true;
      m_lastAcceptedRssiDbm = receivedRssiDbm;
      m_lastAcceptedRssiMs = millis();
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
  const Rf23SendResult result = artemis::rf23bp::sendPacket(m_radio,
                                                            m_radioPins,
                                                            m_radioProfile,
                                                            data,
                                                            len,
                                                            link_protocol::RF_TX_COMPLETE_TIMEOUT_MS,
                                                            &Serial);
  if (result == Rf23SendResult::TX_TIMEOUT) {
    m_fault = link_protocol::TEENSY_RF_FAULT_LOCAL_TX;
  } else if (result == Rf23SendResult::SENT &&
             m_fault == link_protocol::TEENSY_RF_FAULT_LOCAL_TX) {
    m_fault = link_protocol::TEENSY_RF_FAULT_NONE;
  }
  return result;
}

artemis::rf23bp::LinkStats Rf23Driver::linkStats() {
  if (isReady()) {
    m_cachedStats = artemis::rf23bp::readLinkStats(m_radio);
  }
  m_cachedStats.last_rssi_dbm = m_rssiValid ? m_lastAcceptedRssiDbm : 0;
  return m_cachedStats;
}

void Rf23Driver::enterOff(uint8_t fault) {
  m_state = link_protocol::TEENSY_RF_STATE_OFF;
  artemis::rf23bp::shutdownRadio(m_radioPins);
  m_fault = fault;
  // SDN removes the hardware context that produced the sample. Never carry a
  // pre-shutdown RSSI forward as if it described the next initialization.
  m_rssiValid = false;
  m_lastAcceptedRssiDbm = 0;
  m_lastAcceptedRssiMs = 0;
  m_cachedStats.last_rssi_dbm = 0;
}
