#ifndef ARTEMIS_TEENSY_RF23_DRIVER_HPP
#define ARTEMIS_TEENSY_RF23_DRIVER_HPP

#include <Arduino.h>
#include <artemis_rf23bp.hpp>

class Rf23Driver {
 public:
  Rf23Driver(int csPin, int irqPin, uint8_t rxOnPin, uint8_t txOnPin);

  bool begin();
  bool available();
  bool recv(uint8_t* buf, uint8_t* len);
  bool send(const uint8_t* data, uint8_t len);

 private:
  int m_csPin;
  int m_irqPin;
  uint8_t m_rxOnPin;
  uint8_t m_txOnPin;
  RH_RF22 m_radio;
  artemis::rf23bp::RadioPins m_radioPins;
  artemis::rf23bp::RadioProfile m_radioProfile;
};

#endif
