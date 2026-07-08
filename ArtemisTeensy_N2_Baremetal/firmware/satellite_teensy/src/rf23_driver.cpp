#include "rf23_driver.hpp"

Rf23Driver::Rf23Driver(int csPin, int irqPin, uint8_t rxOnPin, uint8_t txOnPin)
    : m_csPin(csPin),
      m_irqPin(irqPin),
      m_rxOnPin(rxOnPin),
      m_txOnPin(txOnPin),
      m_radio(csPin, irqPin, hardware_spi1),
      m_radioPins{},
      m_radioProfile{} {
  m_radioPins.cs_pin = static_cast<uint8_t>(csPin);
  m_radioPins.irq_pin = static_cast<uint8_t>(irqPin);
  m_radioPins.rx_on_pin = rxOnPin;
  m_radioPins.tx_on_pin = txOnPin;
}

bool Rf23Driver::begin() { return artemis::rf23bp::initRadio(m_radio, m_radioPins, m_radioProfile, &Serial); }

bool Rf23Driver::available() {
  return m_radio.available();
}

bool Rf23Driver::recv(uint8_t* buf, uint8_t* len) {
  return artemis::rf23bp::receivePacket(m_radio, m_radioPins, m_radioProfile, buf, len, nullptr);
}

bool Rf23Driver::send(const uint8_t* data, uint8_t len) {
  return artemis::rf23bp::sendPacket(m_radio, m_radioPins, m_radioProfile, data, len);
}

artemis::rf23bp::LinkStats Rf23Driver::linkStats() {
  return artemis::rf23bp::readLinkStats(m_radio);
}
