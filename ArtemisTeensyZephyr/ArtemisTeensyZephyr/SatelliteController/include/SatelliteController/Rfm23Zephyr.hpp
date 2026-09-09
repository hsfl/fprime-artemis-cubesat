#pragma once

#include <cstddef>
#include <cstdint>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

namespace SatelliteController {

class Rfm23Zephyr final {
  public:
    enum class Result : std::uint8_t {
        OK,
        NOT_READY,
        IO_ERROR,
        INIT_TIMEOUT,
        TX_TIMEOUT,
        NO_PACKET,
        INVALID_ARGUMENT,
    };

    enum class Fault : std::uint8_t {
        NONE = 0,
        INIT_FAILED = 1,
        WATCHDOG_RESET = 2,
        LOCAL_TX = 3,
    };

    struct Header {
        std::uint8_t to = 0;
        std::uint8_t from = 0;
        std::uint8_t id = 0;
        std::uint8_t flags = 0;
    };

    Rfm23Zephyr();
    Result configureSafeOff();
    Result probeAndInitialize();
    Result setEnabled(bool enabled);
    Result sendPacket(const std::uint8_t* data, std::size_t length);
    void failSafeOffLocalTx();
    void markLastPacketAccepted();
    Result receivePacket(std::uint8_t* data, std::size_t capacity, std::size_t& length);
    Result serviceInterrupt();
    bool ready() const { return m_ready; }
    bool available();
    bool receiveInProgress() const;
    bool irqPending();
    Fault fault() const { return m_fault; }
    std::uint32_t initAttempts() const { return m_initAttempts; }
    std::uint32_t txGood() const { return m_txGood; }
    std::uint32_t rxGood() const { return m_rxGood; }
    std::uint32_t rxBad() const { return m_rxBad; }
    bool rssiValid() const { return m_rssiValid; }
    std::int16_t lastRssiDbm() const { return m_lastRssiDbm; }
    std::uint32_t lastAcceptedRssiAgeMs() const;
    std::uint8_t lastIrqStatus1() const { return m_lastIrqStatus1; }
    std::uint8_t lastIrqStatus2() const { return m_lastIrqStatus2; }
    const Header& lastReceivedHeader() const { return m_lastReceivedHeader; }

    static constexpr std::size_t MAX_PACKET_LENGTH = 49;

  private:
    static void irqCallback(const device*, gpio_callback*, gpio_port_pins_t);
    static void irqWork(k_work* work);
    Result readRegister(std::uint8_t address, std::uint8_t& value);
    Result writeRegister(std::uint8_t address, std::uint8_t value);
    Result readBurst(std::uint8_t address, std::uint8_t* values, std::size_t length);
    Result writeBurst(std::uint8_t address, const std::uint8_t* values, std::size_t length);
    Result processInterrupts(bool force);
    Result configureModem();
    Result enterReceiveMode();
    Result enterIdleMode();
    Result recoverTransmitPath();
    Result clearFifosAndEnterRx();
    Result failSafe(Result result);
    void amplifierIdle();
    void amplifierReceive();
    void amplifierTransmit();

    spi_dt_spec m_spi;
    gpio_dt_spec m_irq;
    gpio_dt_spec m_rxOn;
    gpio_dt_spec m_txOn;
    gpio_dt_spec m_sdn;
    gpio_callback m_irqCallback{};
    k_work m_irqWork{};
    atomic_t m_irqPending = ATOMIC_INIT(0);
    bool m_callbackRegistered = false;
    bool m_ready = false;
    bool m_rxReady = false;
    bool m_txComplete = false;
    bool m_rssiValid = false;
    std::uint8_t m_lastIrqStatus1 = 0;
    std::uint8_t m_lastIrqStatus2 = 0;
    std::uint32_t m_lastPreambleMs = 0;
    std::uint32_t m_lastAcceptedRssiMs = 0;
    std::int16_t m_pendingRssiDbm = 0;
    std::int16_t m_lastRssiDbm = 0;
    std::uint32_t m_initAttempts = 0;
    std::uint32_t m_txGood = 0;
    std::uint32_t m_rxGood = 0;
    std::uint32_t m_rxBad = 0;
    Fault m_fault = Fault::NONE;
    Header m_lastReceivedHeader{};
    std::uint8_t m_rxPacket[MAX_PACKET_LENGTH]{};
    std::size_t m_rxLength = 0;
    std::uint8_t m_mode = 0;
    static Rfm23Zephyr* s_instance;
};

}  // namespace SatelliteController
