#pragma once

#include <cstddef>
#include <cstdint>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

namespace SatelliteController {

class ZephyrUart final {
  public:
    ZephyrUart(const device* device, std::uint8_t* rxStorage, std::size_t rxCapacity);

    bool initialize();
    bool readByte(std::uint8_t& value);
    bool write(const std::uint8_t* data, std::size_t length);
    void discardRx();
    std::uint32_t droppedRxBytes() const { return m_droppedRxBytes; }

  private:
    static void irqCallback(const device* device, void* context);
    void drainRxIsr();

    const device* m_device;
    ring_buf m_rxRing{};
    std::uint8_t* m_rxStorage;
    std::size_t m_rxCapacity;
    std::uint32_t m_droppedRxBytes = 0;
    bool m_initialized = false;
};

}  // namespace SatelliteController
