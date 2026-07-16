#ifndef ARTEMIS_RF23BP_ARTEMIS_RF23BP_HPP
#define ARTEMIS_RF23BP_ARTEMIS_RF23BP_HPP

#include <Arduino.h>
#include <RHHardwareSPI1.h>
#include <RH_RF22.h>
#include <SPI.h>

namespace artemis {
namespace rf23bp {

// Header-only RF23BP helpers for Artemis Teensy firmware.
// Design goal: keep radio bring-up and packet operations explicit and easy to debug.
//
// NOTE FOR STUDENTS:
// - This helper currently targets Teensy boards using SPI1 via RHHardwareSPI1.
// - It is not a drop-in helper for every Arduino board as-is.
// - Typical sketch include (from a firmware sketch folder in this repo):
//     #include "../libs/rf23bp/artemis_rf23bp.hpp"
// - You still create your RH_RF22 instance in the sketch:
//     RH_RF22 rf23(kPins.cs_pin, kPins.irq_pin, hardware_spi1);

// Artemis+EPSCOR reference wiring for Teensy 4.1 on SPI1.
struct Spi1Pins {
  uint8_t miso_pin = 39;
  uint8_t mosi_pin = 26;
  uint8_t sck_pin = 27;
};

// Complete pin mapping used by this helper library.
struct RadioPins {
  uint8_t cs_pin = 38;
  uint8_t irq_pin = 40;
  uint8_t rx_on_pin = 30;
  uint8_t tx_on_pin = 31;
  uint8_t sdn_pin = 37;
  Spi1Pins spi1 = {};
};

// Runtime radio settings students will most commonly tune.
struct RadioProfile {
  float frequency_mhz = 433.0f;
  RH_RF22::ModemConfigChoice modem = RH_RF22::GFSK_Rb125Fd125;
  uint8_t tx_power = RH_RF22_RF23BP_TXPOW_30DBM;
  bool start_in_receive = true;
  unsigned long settle_us = 300;
};

// Snapshot of interrupt status registers.
// Important: these flags are cleared when the interrupt status registers are read.
struct InterruptStatus {
  uint8_t status1 = 0;
  uint8_t status2 = 0;

  // True when packet CRC check failed.
  bool crcError() const { return (status1 & RH_RF22_ICRCERROR) != 0; }
  // True when FIFO over/underflow was detected by the radio.
  bool fifoError() const { return (status1 & RH_RF22_IFFERROR) != 0; }
  // True when transmit finished.
  bool packetSent() const { return (status1 & RH_RF22_IPKSENT) != 0; }
  // True when a valid packet was received.
  bool packetValid() const { return (status1 & RH_RF22_IPKVALID) != 0; }
  // True when valid preamble was detected.
  bool preambleValid() const { return (status2 & RH_RF22_IPREAVAL) != 0; }
  // True when chip-ready interrupt is set.
  bool chipReady() const { return (status2 & RH_RF22_ICHIPRDY) != 0; }
  // Convenience check for common receive/transmit failures.
  bool anyError() const { return crcError() || fifoError(); }
};

// Snapshot of device status register bits.
struct DeviceStatus {
  uint8_t raw = 0;

  // FIFO overflow bit.
  bool fifoOverflow() const { return (raw & RH_RF22_FFOVL) != 0; }
  // FIFO underflow bit.
  bool fifoUnderflow() const { return (raw & RH_RF22_FFUNFL) != 0; }
  // True when RX FIFO is empty.
  bool rxFifoEmpty() const { return (raw & RH_RF22_RXFFEM) != 0; }
  // Header check failed.
  bool headerError() const { return (raw & RH_RF22_HEADERR) != 0; }
  // Synthesizer frequency error bit.
  bool frequencyError() const { return (raw & RH_RF22_FREQERR) != 0; }
};

// Link-level counters exported by RadioHead.
struct LinkStats {
  uint16_t rx_good = 0;
  uint16_t rx_bad = 0;
  uint16_t tx_good = 0;
  int16_t last_rssi_dbm = 0;
};

enum class SendResult : uint8_t {
  SENT = 0,
  START_FAILED = 1,
  TX_TIMEOUT = 2,
};

// Configure Teensy SPI1 pin mux and start the bus.
inline void setupSpi1(const Spi1Pins& pins) {
  SPI1.setMISO(pins.miso_pin);
  SPI1.setMOSI(pins.mosi_pin);
  SPI1.setSCK(pins.sck_pin);
  SPI1.begin();
}

// Hold chip select high before the radio is probed or initialized.
inline void setupChipSelect(const RadioPins& pins) {
  pinMode(pins.cs_pin, OUTPUT);
  digitalWrite(pins.cs_pin, HIGH);
}

// Put the module into the RFM23BP datasheet shutdown state. This leaves VCC
// present but removes register state and makes SPI unavailable until SDN falls.
inline void shutdownRadio(const RadioPins& pins) {
  detachInterrupt(digitalPinToInterrupt(pins.irq_pin));
  setupChipSelect(pins);
  pinMode(pins.rx_on_pin, OUTPUT);
  pinMode(pins.tx_on_pin, OUTPUT);
  digitalWrite(pins.rx_on_pin, LOW);
  digitalWrite(pins.tx_on_pin, LOW);
  pinMode(pins.sdn_pin, OUTPUT);
  digitalWrite(pins.sdn_pin, HIGH);
}

// Put RF front-end control lines into low-power idle.
inline void setAmpIdle(const RadioPins& pins) {
  digitalWrite(pins.rx_on_pin, LOW);
  digitalWrite(pins.tx_on_pin, LOW);
}

// EPSCOR behavior for receive: RX_ON=0, TX_ON=1.
inline void setAmpReceive(const RadioPins& pins, const RadioProfile& profile) {
  digitalWrite(pins.rx_on_pin, LOW);
  digitalWrite(pins.tx_on_pin, HIGH);
  delayMicroseconds(profile.settle_us);
}

// EPSCOR behavior for transmit: RX_ON=1, TX_ON=0.
inline void setAmpTransmit(const RadioPins& pins, const RadioProfile& profile) {
  digitalWrite(pins.rx_on_pin, HIGH);
  digitalWrite(pins.tx_on_pin, LOW);
  delayMicroseconds(profile.settle_us);
}

// Configure amp control pins and select startup state.
inline void setupAmpPins(const RadioPins& pins, const RadioProfile& profile) {
  pinMode(pins.rx_on_pin, OUTPUT);
  pinMode(pins.tx_on_pin, OUTPUT);
  if (profile.start_in_receive) {
    setAmpReceive(pins, profile);
  } else {
    setAmpIdle(pins);
  }
}

// Verify stable device identity without changing a radio register. RadioHead
// owns the one software-reset/init sequence that follows this check.
inline bool probeDeviceIdentity(RH_RF22& radio, const RadioPins& pins, Print* log = nullptr) {
  setupChipSelect(pins);

  const uint8_t first = radio.spiRead(RH_RF22_REG_00_DEVICE_TYPE);
  delay(1);
  const uint8_t second = radio.spiRead(RH_RF22_REG_00_DEVICE_TYPE);
  const bool knownType =
      first == RH_RF22_DEVICE_TYPE_RX_TRX || first == RH_RF22_DEVICE_TYPE_TX;
  if (!knownType || first != second) {
    if (log != nullptr) {
      log->println(F("RF23BP device probe failed"));
    }
    return false;
  }
  return true;
}

// One-call radio init:
// 1) force safe datasheet shutdown
// 2) release SDN and wait through the worst-case POR interval
// 3) perform stable, non-mutating identity reads
// 4) call RH_RF22 init exactly once + apply the proven RF profile
// 5) enter RX or IDLE based on profile
inline bool initRadio(RH_RF22& radio, const RadioPins& pins = RadioPins(),
                      const RadioProfile& profile = RadioProfile(),
                      Print* log = nullptr) {
  shutdownRadio(pins);
  delay(50);
  setupSpi1(pins.spi1);
  digitalWrite(pins.sdn_pin, LOW);
  delay(50);

  if (!probeDeviceIdentity(radio, pins, log)) {
    shutdownRadio(pins);
    return false;
  }

  if (!radio.init()) {
    if (log != nullptr) {
      log->println(F("RF23BP init failed"));
    }
    shutdownRadio(pins);
    return false;
  }

  if (!radio.setFrequency(profile.frequency_mhz)) {
    if (log != nullptr) {
      log->println(F("RF23BP setFrequency failed"));
    }
    shutdownRadio(pins);
    return false;
  }

  radio.setModemConfig(profile.modem);
  // RFM23BP datasheet section 3.5.7: rates above 100 kbps require
  // register 0x58 = 0xC0. RadioHead's 125 kbps preset leaves the POR/default
  // 0x80 value, which increases eye closure and packet loss.
  if (profile.modem == RH_RF22::GFSK_Rb125Fd125) {
    radio.spiWrite(RH_RF22_REG_58_CHARGE_PUMP_CURRENT_TRIMMING, 0xC0);
  }
  radio.setTxPower(profile.tx_power);

  setupAmpPins(pins, profile);
  if (profile.start_in_receive) {
    setAmpReceive(pins, profile);
    radio.setModeRx();
  } else {
    setAmpIdle(pins);
    radio.setModeIdle();
  }

  if (log != nullptr) {
    log->println(F("RF23BP ready"));
  }
  return true;
}

// Recover the radio after a terminal TX wait timeout. Always return to RX so
// the peer can re-establish the link after the local transmit path wedges.
inline void recoverTransmitPath(RH_RF22& radio, const RadioPins& pins,
                                const RadioProfile& profile) {
  radio.setModeIdle();
  const uint8_t op_mode2 = radio.spiRead(RH_RF22_REG_08_OPERATING_MODE2);
  radio.spiWrite(RH_RF22_REG_08_OPERATING_MODE2,
                 op_mode2 | RH_RF22_FFCLRTX | RH_RF22_FFCLRRX);
  radio.spiWrite(RH_RF22_REG_08_OPERATING_MODE2, op_mode2);
  (void)radio.spiRead(RH_RF22_REG_03_INTERRUPT_STATUS1);
  (void)radio.spiRead(RH_RF22_REG_04_INTERRUPT_STATUS2);
  setAmpReceive(pins, profile);
  radio.setModeRx();
}

// Send one packet with a mandatory bounded completion wait.
inline SendResult sendPacket(RH_RF22& radio, const RadioPins& pins,
                             const RadioProfile& profile, const uint8_t* data,
                             uint8_t len, uint16_t tx_complete_timeout_ms,
                             Print* log = nullptr) {
  if (data == nullptr || len == 0 || len > radio.maxMessageLength() ||
      tx_complete_timeout_ms == 0) {
    return SendResult::START_FAILED;
  }

  setAmpTransmit(pins, profile);
  if (!radio.send(data, len)) {
    if (profile.start_in_receive) {
      setAmpReceive(pins, profile);
      radio.setModeRx();
    } else {
      setAmpIdle(pins);
      radio.setModeIdle();
    }
    return SendResult::START_FAILED;
  }

  if (!static_cast<RHGenericDriver&>(radio).waitPacketSent(tx_complete_timeout_ms)) {
    recoverTransmitPath(radio, pins, profile);
    if (log != nullptr) {
      log->println(F("RF23BP TX completion timeout; FIFOs cleared and RX restored"));
    }
    return SendResult::TX_TIMEOUT;
  }

  if (profile.start_in_receive) {
    setAmpReceive(pins, profile);
    radio.setModeRx();
  } else {
    setAmpIdle(pins);
    radio.setModeIdle();
  }

  return SendResult::SENT;
}

// Receive one packet if available.
// Returns false when no packet is ready or input pointers are invalid.
inline bool receivePacket(RH_RF22& radio, const RadioPins& pins,
                          const RadioProfile& profile, uint8_t* buf,
                          uint8_t* len, int16_t* last_rssi_dbm = nullptr) {
  if (buf == nullptr || len == nullptr || *len == 0) {
    return false;
  }

  setAmpReceive(pins, profile);
  radio.setModeRx();

  if (!radio.available()) {
    return false;
  }

  const bool ok = radio.recv(buf, len);
  if (ok && last_rssi_dbm != nullptr) {
    *last_rssi_dbm = radio.lastRssi();
  }
  return ok;
}

// Read interrupt status registers (0x03/0x04).
// Warning: this clears the latched interrupt flags in hardware.
inline InterruptStatus readAndClearInterruptStatus(RH_RF22& radio) {
  InterruptStatus irq;
  irq.status1 = radio.spiRead(RH_RF22_REG_03_INTERRUPT_STATUS1);
  irq.status2 = radio.spiRead(RH_RF22_REG_04_INTERRUPT_STATUS2);
  return irq;
}

// Convenience helper when only CRC-error state is needed.
inline bool readAndClearCrcError(RH_RF22& radio) {
  return readAndClearInterruptStatus(radio).crcError();
}

// Read non-clearing device status register (0x02).
inline DeviceStatus readDeviceStatus(RH_RF22& radio) {
  DeviceStatus status;
  status.raw = radio.statusRead();
  return status;
}

// True if packet CRC checking is enabled in register 0x30.
inline bool isPacketCrcEnabled(RH_RF22& radio) {
  return (radio.spiRead(RH_RF22_REG_30_DATA_ACCESS_CONTROL) & RH_RF22_ENCRC) != 0;
}

// Read current CRC polynomial selection from register 0x30.
inline RH_RF22::CRCPolynomial currentCrcPolynomial(RH_RF22& radio) {
  const uint8_t crc_bits =
      radio.spiRead(RH_RF22_REG_30_DATA_ACCESS_CONTROL) & RH_RF22_CRC;
  return static_cast<RH_RF22::CRCPolynomial>(crc_bits);
}

// Snapshot RadioHead packet counters and last RSSI.
inline LinkStats readLinkStats(RH_RF22& radio) {
  LinkStats stats;
  stats.rx_good = radio.rxGood();
  stats.rx_bad = radio.rxBad();
  stats.tx_good = radio.txGood();
  stats.last_rssi_dbm = radio.lastRssi();
  return stats;
}

// Basic FIFO fault recovery:
// 1) force IDLE
// 2) clear TX/RX FIFOs
// 3) clear latched interrupts
// 4) return to RX
inline void recoverFromFifoError(RH_RF22& radio, const RadioPins& pins,
                                 const RadioProfile& profile) {
  recoverTransmitPath(radio, pins, profile);
}

}  // namespace rf23bp
}  // namespace artemis

#endif
