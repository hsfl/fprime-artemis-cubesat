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

bool Rf23Driver::begin() {
  if (!artemis::rf23bp::initRadio(m_radio, m_radioPins, m_radioProfile, &SerialUSB1)) {
    return false;
  }
  m_radio.setPromiscuous(true);
  m_radio.setHeaderTo(link_protocol::RF_REMOTE_ADDRESS);
  m_radio.setHeaderFrom(link_protocol::RF_LOCAL_ADDRESS);
  m_radio.setHeaderId(link_protocol::RF_NETWORK_ID);
  m_radio.setHeaderFlags(link_protocol::RF_PROTOCOL_VERSION, 0xFF);
  return true;
}

bool Rf23Driver::available() {
  return m_radio.available();
}

Rf23ReceiveResult Rf23Driver::recv(uint8_t* buf, uint8_t* len) {
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
  return artemis::rf23bp::sendPacket(m_radio,
                                     m_radioPins,
                                     m_radioProfile,
                                     data,
                                     len,
                                     link_protocol::RF_TX_COMPLETE_TIMEOUT_MS,
                                     &SerialUSB1);
}
