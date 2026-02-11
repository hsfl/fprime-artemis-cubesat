#include "rf23_driver.hpp"

Rf23Driver::Rf23Driver(int csPin, int irqPin, uint8_t rxOnPin, uint8_t txOnPin)
    : m_csPin(csPin),
      m_irqPin(irqPin),
      m_rxOnPin(rxOnPin),
      m_txOnPin(txOnPin),
      m_radio(csPin, irqPin, hardware_spi1) {}

bool Rf23Driver::begin() {
  pinMode(m_rxOnPin, OUTPUT);
  pinMode(m_txOnPin, OUTPUT);
  setAmpReceive();

  if (!m_radio.init()) {
    return false;
  }
  if (!m_radio.setFrequency(433.0)) {
    return false;
  }

  m_radio.setModemConfig(RH_RF22::GFSK_Rb125Fd125);
  m_radio.setTxPower(RH_RF22_RF23BP_TXPOW_30DBM);
  return true;
}

bool Rf23Driver::available() {
  return m_radio.available();
}

bool Rf23Driver::recv(uint8_t* buf, uint8_t* len) {
  if (!m_radio.available()) {
    return false;
  }
  setAmpReceive();
  return m_radio.recv(buf, len);
}

bool Rf23Driver::send(const uint8_t* data, uint8_t len) {
  setAmpTransmit();
  const bool ok = m_radio.send(data, len);
  m_radio.waitPacketSent();
  setAmpReceive();
  return ok;
}

void Rf23Driver::setAmpReceive() {
  digitalWrite(m_rxOnPin, HIGH);
  digitalWrite(m_txOnPin, LOW);
}

void Rf23Driver::setAmpTransmit() {
  digitalWrite(m_rxOnPin, LOW);
  digitalWrite(m_txOnPin, HIGH);
}
