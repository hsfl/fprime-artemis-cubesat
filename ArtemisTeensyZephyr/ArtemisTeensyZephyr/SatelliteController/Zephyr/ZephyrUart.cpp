#include "SatelliteController/ZephyrUart.hpp"

#include <zephyr/irq.h>

namespace SatelliteController {

ZephyrUart::ZephyrUart(const device* device, std::uint8_t* rxStorage, std::size_t rxCapacity)
    : m_device(device), m_rxStorage(rxStorage), m_rxCapacity(rxCapacity) {}

bool ZephyrUart::initialize() {
    if (m_device == nullptr || !device_is_ready(m_device) || m_rxStorage == nullptr || m_rxCapacity == 0) return false;
    ring_buf_init(&m_rxRing, static_cast<std::uint32_t>(m_rxCapacity), m_rxStorage);
    if (uart_irq_callback_user_data_set(m_device, irqCallback, this) != 0) return false;
    uart_irq_rx_enable(m_device);
    m_initialized = true;
    return true;
}

void ZephyrUart::irqCallback(const device*, void* context) {
    auto* self = static_cast<ZephyrUart*>(context);
    if (self != nullptr) self->drainRxIsr();
}

void ZephyrUart::drainRxIsr() {
    if (!uart_irq_update(m_device) || !uart_irq_rx_ready(m_device)) return;
    std::uint8_t scratch[32];
    while (uart_irq_rx_ready(m_device)) {
        const int received = uart_fifo_read(m_device, scratch, sizeof(scratch));
        if (received <= 0) break;
        const std::uint32_t accepted = ring_buf_put(&m_rxRing, scratch, static_cast<std::uint32_t>(received));
        m_droppedRxBytes += static_cast<std::uint32_t>(received) - accepted;
    }
}

bool ZephyrUart::readByte(std::uint8_t& value) {
    if (!m_initialized) return false;
    unsigned int key = irq_lock();
    const std::uint32_t read = ring_buf_get(&m_rxRing, &value, 1);
    irq_unlock(key);
    return read == 1;
}

bool ZephyrUart::write(const std::uint8_t* data, std::size_t length) {
    if (!m_initialized || (data == nullptr && length != 0)) return false;
    for (std::size_t i = 0; i < length; ++i) uart_poll_out(m_device, data[i]);
    return true;
}

void ZephyrUart::discardRx() {
    unsigned int key = irq_lock();
    ring_buf_reset(&m_rxRing);
    irq_unlock(key);
}

}  // namespace SatelliteController
