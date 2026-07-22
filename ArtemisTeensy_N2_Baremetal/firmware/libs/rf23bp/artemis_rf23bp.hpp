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
  uint16_t chip_ready_timeout_ms = 100;
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

// First-failure evidence captured before recovery changes radio state. Reading
// interrupt status registers 0x03/0x04 clears them, so they are always sampled
// last and diagnostics must print this stored snapshot instead of reading live.
struct FaultSnapshot {
  bool valid = false;
  SendResult cause = SendResult::START_FAILED;
  uint32_t captured_ms = 0;
  uint8_t nirq_level = 0xFF;
  uint8_t radiohead_mode = 0xFF;
  uint8_t device_type = 0;
  uint8_t version_code = 0;
  uint8_t device_status = 0;
  uint8_t interrupt_enable1 = 0;
  uint8_t interrupt_enable2 = 0;
  uint8_t operating_mode1 = 0;
  uint8_t operating_mode2 = 0;
  uint8_t raw_rssi = 0;
  uint8_t interrupt_status1 = 0;
  uint8_t interrupt_status2 = 0;
};

// This is the tagged RadioHead RH_RF22::init() sequence with only its
// unbounded CHIPRDY wait changed to a timeout. It lets the caller pulse SDN
// and retry rather than hanging the Teensy forever.
class BoundedRf22 : public RH_RF22 {
 public:
  BoundedRf22(uint8_t slave_select_pin, uint8_t interrupt_pin, RHGenericSPI& spi)
      : RH_RF22(slave_select_pin, interrupt_pin, spi) {}

  bool initBounded(uint16_t chip_ready_timeout_ms) {
    if (chip_ready_timeout_ms == 0 || !RHSPIDriver::init()) {
      return false;
    }
    int interrupt_number = digitalPinToInterrupt(_interruptPin);
    if (interrupt_number == NOT_AN_INTERRUPT) {
      return false;
    }
#ifdef RH_ATTACHINTERRUPT_TAKES_PIN_NUMBER
    interrupt_number = _interruptPin;
#endif
    spiUsingInterrupt(interrupt_number);
    reset();
    _deviceType = spiRead(RH_RF22_REG_00_DEVICE_TYPE);
    if (_deviceType != RH_RF22_DEVICE_TYPE_RX_TRX &&
        _deviceType != RH_RF22_DEVICE_TYPE_TX) {
      return false;
    }

    spiWrite(RH_RF22_REG_07_OPERATING_MODE1, RH_RF22_SWRES);
    const uint32_t started_ms = millis();
    while ((spiRead(RH_RF22_REG_04_INTERRUPT_STATUS2) & RH_RF22_ICHIPRDY) == 0) {
      if ((millis() - started_ms) >= chip_ready_timeout_ms) {
        return false;
      }
      yield();
    }

    pinMode(_interruptPin, INPUT);
    spiWrite(RH_RF22_REG_05_INTERRUPT_ENABLE1,
             RH_RF22_ENTXFFAEM | RH_RF22_ENRXFFAFULL | RH_RF22_ENPKSENT |
                 RH_RF22_ENPKVALID | RH_RF22_ENCRCERROR | RH_RF22_ENFFERR);
    spiWrite(RH_RF22_REG_06_INTERRUPT_ENABLE2, RH_RF22_ENPREAVAL);
    if (_myInterruptIndex == 0xff) {
      if (_interruptCount >= RH_RF22_NUM_INTERRUPTS) {
        return false;
      }
      _myInterruptIndex = _interruptCount++;
    }
    _deviceForInterrupt[_myInterruptIndex] = this;
    if (_myInterruptIndex == 0) {
      attachInterrupt(interrupt_number, isr0, FALLING);
    } else if (_myInterruptIndex == 1) {
      attachInterrupt(interrupt_number, isr1, FALLING);
    } else if (_myInterruptIndex == 2) {
      attachInterrupt(interrupt_number, isr2, FALLING);
    } else {
      return false;
    }

    setModeIdle();
    clearTxBuf();
    clearRxBuf();
    spiWrite(RH_RF22_REG_7D_TX_FIFO_CONTROL2, RH_RF22_TXFFAEM_THRESHOLD);
    spiWrite(RH_RF22_REG_7E_RX_FIFO_CONTROL, RH_RF22_RXFFAFULL_THRESHOLD);
    spiWrite(RH_RF22_REG_30_DATA_ACCESS_CONTROL,
             RH_RF22_ENPACRX | RH_RF22_ENPACTX | RH_RF22_ENCRC |
                 (_polynomial & RH_RF22_CRC));
    spiWrite(RH_RF22_REG_32_HEADER_CONTROL1,
             RH_RF22_BCEN_HEADER3 | RH_RF22_HDCH_HEADER3);
    spiWrite(RH_RF22_REG_33_HEADER_CONTROL2,
             RH_RF22_HDLEN_4 | RH_RF22_SYNCLEN_2);
    setPreambleLength(8);
    uint8_t sync_words[] = {0x2d, 0xd4};
    setSyncWords(sync_words, sizeof(sync_words));
    setPromiscuous(false);
    setFrequency(434.0f, 0.05f);
    setModemConfig(FSK_Rb2_4Fd36);
    setGpioReversed(false);
    setTxPower(RH_RF22_TXPOW_8DBM);
    return true;
  }
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

// One-call radio init:
// 1) reset the separately powered radio through SDN
// 2) initialize SPI1 and use the tagged RadioHead initialization path
// 3) apply the proven RF profile
// 4) enter RX or IDLE based on profile
inline bool initRadio(BoundedRf22& radio, const RadioPins& pins = RadioPins(),
                      const RadioProfile& profile = RadioProfile(),
                      Print* log = nullptr) {
  static constexpr uint8_t MAX_INIT_ATTEMPTS = 3;
  for (uint8_t attempt = 1; attempt <= MAX_INIT_ATTEMPTS; ++attempt) {
    // The radio remains powered during a Teensy reset. Pulse SDN before every
    // attempt so each probe begins from a known radio state.
    shutdownRadio(pins);
    delay(100);
    SPI1.end();
    setupSpi1(pins.spi1);
    digitalWrite(pins.sdn_pin, LOW);
    delay(100);

    if (!radio.initBounded(profile.chip_ready_timeout_ms) ||
        !radio.setFrequency(profile.frequency_mhz)) {
      if (log != nullptr) {
        log->print(F("RF23BP init attempt failed: "));
        log->println(attempt);
      }
      continue;
    }

    radio.setModemConfig(profile.modem);
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

  shutdownRadio(pins);
  if (log != nullptr) {
    log->println(F("RF23BP init failed after 3 attempts"));
  }
  return false;
}

inline void captureFirstFaultSnapshot(RH_RF22& radio, const RadioPins& pins,
                                      SendResult cause,
                                      FaultSnapshot* first_fault) {
  if (first_fault == nullptr || first_fault->valid) {
    return;
  }

  FaultSnapshot snapshot;
  snapshot.cause = cause;
  snapshot.captured_ms = millis();
  ATOMIC_BLOCK_START;
  snapshot.nirq_level = static_cast<uint8_t>(digitalRead(pins.irq_pin));
  snapshot.radiohead_mode = static_cast<uint8_t>(radio.mode());
  snapshot.device_type = radio.spiRead(RH_RF22_REG_00_DEVICE_TYPE);
  snapshot.version_code = radio.spiRead(RH_RF22_REG_01_VERSION_CODE);
  snapshot.device_status = radio.spiRead(RH_RF22_REG_02_DEVICE_STATUS);
  snapshot.interrupt_enable1 = radio.spiRead(RH_RF22_REG_05_INTERRUPT_ENABLE1);
  snapshot.interrupt_enable2 = radio.spiRead(RH_RF22_REG_06_INTERRUPT_ENABLE2);
  snapshot.operating_mode1 = radio.spiRead(RH_RF22_REG_07_OPERATING_MODE1);
  snapshot.operating_mode2 = radio.spiRead(RH_RF22_REG_08_OPERATING_MODE2);
  snapshot.raw_rssi = radio.spiRead(RH_RF22_REG_26_RSSI);
  // Destructive reads must remain last.
  snapshot.interrupt_status1 = radio.spiRead(RH_RF22_REG_03_INTERRUPT_STATUS1);
  snapshot.interrupt_status2 = radio.spiRead(RH_RF22_REG_04_INTERRUPT_STATUS2);
  ATOMIC_BLOCK_END;
  snapshot.valid = true;
  *first_fault = snapshot;
}

// Send one packet with a mandatory bounded completion wait.
inline SendResult sendPacket(RH_RF22& radio, const RadioPins& pins,
                             const RadioProfile& profile, const uint8_t* data,
                             uint8_t len, uint16_t tx_complete_timeout_ms,
                             Print* log = nullptr,
                             FaultSnapshot* first_fault = nullptr) {
  if (data == nullptr || len == 0 || len > radio.maxMessageLength() ||
      tx_complete_timeout_ms == 0) {
    return SendResult::START_FAILED;
  }

  // RH_RF22::send() begins with an unbounded waitPacketSent(). Never enter it
  // while a previous TX is still wedged.
  if (radio.mode() == RHGenericDriver::RHModeTx) {
    captureFirstFaultSnapshot(radio, pins, SendResult::TX_TIMEOUT, first_fault);
    if (log != nullptr) {
      log->println(F("RF23BP pre-send TX wedge"));
    }
    return SendResult::TX_TIMEOUT;
  }

  setAmpTransmit(pins, profile);
  if (!radio.send(data, len)) {
    captureFirstFaultSnapshot(radio, pins, SendResult::START_FAILED, first_fault);
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
    captureFirstFaultSnapshot(radio, pins, SendResult::TX_TIMEOUT, first_fault);
    if (log != nullptr) {
      log->println(F("RF23BP TX completion timeout"));
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

}  // namespace rf23bp
}  // namespace artemis

#endif
