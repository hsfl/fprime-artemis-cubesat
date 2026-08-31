#pragma once

#include "SatelliteController/LocalServices.hpp"
#include "SatelliteController/Rfm23Zephyr.hpp"
#include "SatelliteController/ZephyrUart.hpp"

namespace SatelliteController {

class ZephyrPduTransport final : public PduTransport {
  public:
    explicit ZephyrPduTransport(ZephyrUart& uart) : m_uart(uart) {}

    void discardRx() override { m_uart.discardRx(); }
    bool write(const std::uint8_t* data, std::size_t length) override { return m_uart.write(data, length); }
    void flush() override {}
    bool readByte(std::uint8_t& value) override { return m_uart.readByte(value); }

  private:
    ZephyrUart& m_uart;
};

class ZephyrRfStatusProvider final : public RfStatusProvider {
  public:
    explicit ZephyrRfStatusProvider(Rfm23Zephyr& radio) : m_radio(radio) {}

    bool setEnabled(bool enabled) override;
    RfStatusSnapshot status() const override;
    void setBootWatchdogReset(bool detected) {
        m_watchdogReset = detected;
        m_bootFaultActive = detected;
    }
    void noteTransmitResult(bool sent) {
        if (sent) ++m_txPackets;
        else ++m_txDrops;
    }
    void noteTransmitDrops(std::size_t count) { m_txDrops += static_cast<std::uint32_t>(count); }
    void noteAcceptedPacket() { ++m_rxPackets; }

  private:
    static std::uint16_t narrowCounter(std::uint32_t value);
    Rfm23Zephyr& m_radio;
    bool m_watchdogReset = false;
    bool m_bootFaultActive = false;
    std::uint32_t m_rxPackets = 0;
    std::uint32_t m_txPackets = 0;
    std::uint32_t m_txDrops = 0;
};

}  // namespace SatelliteController
