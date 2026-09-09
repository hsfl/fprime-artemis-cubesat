#include "SatelliteController/Rfm23Zephyr.hpp"

namespace SatelliteController {

namespace {
constexpr std::uint8_t DEVICE_TYPE = 0x00;
constexpr std::uint8_t DEVICE_STATUS = 0x02;
constexpr std::uint8_t IRQ_STATUS_1 = 0x03;
constexpr std::uint8_t IRQ_STATUS_2 = 0x04;
constexpr std::uint8_t IRQ_ENABLE_1 = 0x05;
constexpr std::uint8_t IRQ_ENABLE_2 = 0x06;
constexpr std::uint8_t MODE_1 = 0x07;
constexpr std::uint8_t MODE_2 = 0x08;
constexpr std::uint8_t GPIO_0 = 0x0B;
constexpr std::uint8_t GPIO_1 = 0x0C;
constexpr std::uint8_t RSSI = 0x26;
constexpr std::uint8_t AFC_LIMITER = 0x2A;
constexpr std::uint8_t DATA_ACCESS = 0x30;
constexpr std::uint8_t HEADER_CONTROL_1 = 0x32;
constexpr std::uint8_t HEADER_CONTROL_2 = 0x33;
constexpr std::uint8_t PREAMBLE_LENGTH = 0x34;
constexpr std::uint8_t SYNC_1 = 0x36;
constexpr std::uint8_t SYNC_0 = 0x37;
constexpr std::uint8_t TX_HEADER_3 = 0x3A;
constexpr std::uint8_t TX_HEADER_2 = 0x3B;
constexpr std::uint8_t TX_HEADER_1 = 0x3C;
constexpr std::uint8_t TX_HEADER_0 = 0x3D;
constexpr std::uint8_t PACKET_LENGTH = 0x3E;
constexpr std::uint8_t CHECK_HEADER_3 = 0x3F;
constexpr std::uint8_t HEADER_ENABLE_3 = 0x43;
constexpr std::uint8_t RX_HEADER_3 = 0x47;
constexpr std::uint8_t RX_HEADER_2 = 0x48;
constexpr std::uint8_t RX_HEADER_1 = 0x49;
constexpr std::uint8_t RX_HEADER_0 = 0x4A;
constexpr std::uint8_t RX_PACKET_LENGTH = 0x4B;
constexpr std::uint8_t TX_POWER = 0x6D;
constexpr std::uint8_t TX_RATE_1 = 0x6E;
constexpr std::uint8_t CHARGE_PUMP = 0x58;
constexpr std::uint8_t FREQ_OFFSET_1 = 0x73;
constexpr std::uint8_t FREQ_OFFSET_2 = 0x74;
constexpr std::uint8_t FREQ_BAND = 0x75;
constexpr std::uint8_t FREQ_CARRIER_1 = 0x76;
constexpr std::uint8_t FREQ_CARRIER_0 = 0x77;
constexpr std::uint8_t TX_FIFO_THRESHOLD = 0x7D;
constexpr std::uint8_t RX_FIFO_THRESHOLD = 0x7E;
constexpr std::uint8_t FIFO = 0x7F;

constexpr std::uint8_t SW_RESET = 0x80;
constexpr std::uint8_t XTON = 0x01;
constexpr std::uint8_t RXON = 0x04;
constexpr std::uint8_t TXON = 0x08;
constexpr std::uint8_t CLEAR_RX = 0x02;
constexpr std::uint8_t CLEAR_TX = 0x01;
constexpr std::uint8_t CHIP_READY = 0x02;
constexpr std::uint8_t FIFO_ERROR = 0x80;
constexpr std::uint8_t PACKET_SENT = 0x04;
constexpr std::uint8_t PACKET_VALID = 0x02;
constexpr std::uint8_t CRC_ERROR = 0x01;
constexpr std::uint8_t PREAMBLE_VALID = 0x40;
constexpr std::uint8_t FREQUENCY_ERROR = 0x08;

constexpr std::uint8_t DEV_TX = 0x07;
constexpr std::uint8_t DEV_RX_TRX = 0x08;
constexpr std::uint32_t INIT_TIMEOUT_MS = 100;
constexpr std::uint32_t TX_TIMEOUT_MS = 500;
constexpr std::uint32_t PREAMBLE_GUARD_MS = 6;

constexpr std::uint8_t RF_NETWORK = 0xC3;
constexpr std::uint8_t RF_LOCAL = 0xA2;
constexpr std::uint8_t RF_REMOTE = 0xA1;
constexpr std::uint8_t RF_VERSION = 0x01;

constexpr std::uint8_t MODE_IDLE = 0;
constexpr std::uint8_t MODE_RX = 1;
constexpr std::uint8_t MODE_TX = 2;

constexpr std::uint8_t CLOCK_RECOVERY[] = {0x60, 0x01, 0x55, 0x55, 0x02, 0xAD};
constexpr std::uint8_t OOK[] = {0x40, 0x0A, 0x50};
constexpr std::uint8_t TX_RATE[] = {0x20, 0x00, 0x0C, 0x23, 0xC8};
}  // namespace

Rfm23Zephyr* Rfm23Zephyr::s_instance = nullptr;

Rfm23Zephyr::Rfm23Zephyr()
    : m_spi(SPI_DT_SPEC_GET(DT_NODELABEL(rfm23), SPI_WORD_SET(8) | SPI_TRANSFER_MSB)),
      m_irq(GPIO_DT_SPEC_GET(DT_NODELABEL(rfm23), irq_gpios)),
      m_rxOn(GPIO_DT_SPEC_GET(DT_NODELABEL(rfm23), rx_on_gpios)),
      m_txOn(GPIO_DT_SPEC_GET(DT_NODELABEL(rfm23), tx_on_gpios)),
      m_sdn(GPIO_DT_SPEC_GET(DT_NODELABEL(rfm23), sdn_gpios)) {
    s_instance = this;
}

void Rfm23Zephyr::amplifierIdle() {
    (void)gpio_pin_set_dt(&m_rxOn, 0);
    (void)gpio_pin_set_dt(&m_txOn, 0);
}

void Rfm23Zephyr::amplifierReceive() {
    (void)gpio_pin_set_dt(&m_rxOn, 0);
    (void)gpio_pin_set_dt(&m_txOn, 1);
    k_busy_wait(300);
}

void Rfm23Zephyr::amplifierTransmit() {
    (void)gpio_pin_set_dt(&m_rxOn, 1);
    (void)gpio_pin_set_dt(&m_txOn, 0);
    k_busy_wait(300);
}

Rfm23Zephyr::Result Rfm23Zephyr::configureSafeOff() {
    if (!spi_is_ready_dt(&m_spi) || !gpio_is_ready_dt(&m_irq) || !gpio_is_ready_dt(&m_rxOn) ||
        !gpio_is_ready_dt(&m_txOn) || !gpio_is_ready_dt(&m_sdn)) {
        return Result::NOT_READY;
    }
    if (gpio_pin_configure_dt(&m_rxOn, GPIO_OUTPUT_INACTIVE) != 0 ||
        gpio_pin_configure_dt(&m_txOn, GPIO_OUTPUT_INACTIVE) != 0 ||
        gpio_pin_configure_dt(&m_sdn, GPIO_OUTPUT_ACTIVE) != 0 ||
        gpio_pin_configure_dt(&m_irq, GPIO_INPUT) != 0) {
        return Result::IO_ERROR;
    }
    amplifierIdle();
    (void)gpio_pin_set_dt(&m_sdn, 1);
    m_ready = false;
    m_mode = MODE_IDLE;
    m_rxReady = false;
    m_txComplete = false;
    m_rssiValid = false;
    m_lastPreambleMs = 0;
    m_lastAcceptedRssiMs = 0;
    m_pendingRssiDbm = 0;
    m_lastRssiDbm = 0;
    m_fault = Fault::NONE;
    atomic_set(&m_irqPending, 0);
    if (!m_callbackRegistered) {
        k_work_init(&m_irqWork, irqWork);
        gpio_init_callback(&m_irqCallback, irqCallback, BIT(m_irq.pin));
        if (gpio_add_callback(m_irq.port, &m_irqCallback) != 0) return Result::IO_ERROR;
        m_callbackRegistered = true;
    }
    (void)gpio_pin_interrupt_configure_dt(&m_irq, GPIO_INT_DISABLE);
    return Result::OK;
}

Rfm23Zephyr::Result Rfm23Zephyr::readRegister(std::uint8_t address, std::uint8_t& value) {
    std::uint8_t tx[2] = {static_cast<std::uint8_t>(address & 0x7FU), 0};
    std::uint8_t rx[2] = {};
    const spi_buf txBuf{tx, sizeof(tx)};
    const spi_buf rxBuf{rx, sizeof(rx)};
    const spi_buf_set txSet{&txBuf, 1};
    const spi_buf_set rxSet{&rxBuf, 1};
    if (spi_transceive_dt(&m_spi, &txSet, &rxSet) != 0) return Result::IO_ERROR;
    value = rx[1];
    return Result::OK;
}

Rfm23Zephyr::Result Rfm23Zephyr::writeRegister(std::uint8_t address, std::uint8_t value) {
    std::uint8_t tx[2] = {static_cast<std::uint8_t>(address | 0x80U), value};
    const spi_buf txBuf{tx, sizeof(tx)};
    const spi_buf_set txSet{&txBuf, 1};
    return spi_write_dt(&m_spi, &txSet) == 0 ? Result::OK : Result::IO_ERROR;
}

Rfm23Zephyr::Result Rfm23Zephyr::readBurst(std::uint8_t address, std::uint8_t* values,
                                           std::size_t length) {
    if (values == nullptr || length == 0 || length > MAX_PACKET_LENGTH + 2) return Result::INVALID_ARGUMENT;
    std::uint8_t tx[MAX_PACKET_LENGTH + 3] = {};
    std::uint8_t rx[MAX_PACKET_LENGTH + 3] = {};
    tx[0] = static_cast<std::uint8_t>(address & 0x7FU);
    const spi_buf txBuf{tx, length + 1};
    const spi_buf rxBuf{rx, length + 1};
    const spi_buf_set txSet{&txBuf, 1};
    const spi_buf_set rxSet{&rxBuf, 1};
    if (spi_transceive_dt(&m_spi, &txSet, &rxSet) != 0) return Result::IO_ERROR;
    for (std::size_t i = 0; i < length; ++i) values[i] = rx[i + 1];
    return Result::OK;
}

Rfm23Zephyr::Result Rfm23Zephyr::writeBurst(std::uint8_t address, const std::uint8_t* values,
                                            std::size_t length) {
    if (values == nullptr || length == 0 || length > MAX_PACKET_LENGTH + 2) return Result::INVALID_ARGUMENT;
    std::uint8_t tx[MAX_PACKET_LENGTH + 3] = {};
    tx[0] = static_cast<std::uint8_t>(address | 0x80U);
    for (std::size_t i = 0; i < length; ++i) tx[i + 1] = values[i];
    const spi_buf txBuf{tx, length + 1};
    const spi_buf_set txSet{&txBuf, 1};
    return spi_write_dt(&m_spi, &txSet) == 0 ? Result::OK : Result::IO_ERROR;
}

Rfm23Zephyr::Result Rfm23Zephyr::enterIdleMode() {
    const Result result = writeRegister(MODE_1, XTON);
    if (result == Result::OK) m_mode = MODE_IDLE;
    return result;
}

Rfm23Zephyr::Result Rfm23Zephyr::enterReceiveMode() {
    amplifierReceive();
    const Result result = writeRegister(MODE_1, XTON | RXON);
    if (result == Result::OK) m_mode = MODE_RX;
    return result;
}

Rfm23Zephyr::Result Rfm23Zephyr::clearFifosAndEnterRx() {
    if (enterIdleMode() != Result::OK) return Result::IO_ERROR;
    std::uint8_t mode2 = 0;
    if (readRegister(MODE_2, mode2) != Result::OK ||
        writeRegister(MODE_2, static_cast<std::uint8_t>(mode2 | CLEAR_RX | CLEAR_TX)) != Result::OK ||
        writeRegister(MODE_2, mode2) != Result::OK) return Result::IO_ERROR;
    std::uint8_t ignored = 0;
    if (readRegister(IRQ_STATUS_1, ignored) != Result::OK || readRegister(IRQ_STATUS_2, ignored) != Result::OK) {
        return Result::IO_ERROR;
    }
    m_rxReady = false;
    m_rxLength = 0;
    m_txComplete = false;
    return enterReceiveMode();
}

Rfm23Zephyr::Result Rfm23Zephyr::recoverTransmitPath() {
    return clearFifosAndEnterRx();
}

Rfm23Zephyr::Result Rfm23Zephyr::configureModem() {
    if (writeRegister(0x1C, 0x8A) != Result::OK || writeRegister(0x1F, 0x03) != Result::OK ||
        writeBurst(0x20, CLOCK_RECOVERY, sizeof(CLOCK_RECOVERY)) != Result::OK ||
        writeBurst(0x2C, OOK, sizeof(OOK)) != Result::OK ||
        writeRegister(CHARGE_PUMP, 0xC0) != Result::OK || writeRegister(0x69, 0x60) != Result::OK ||
        writeBurst(TX_RATE_1, TX_RATE, sizeof(TX_RATE)) != Result::OK ||
        writeRegister(FREQ_OFFSET_1, 0) != Result::OK || writeRegister(FREQ_OFFSET_2, 0) != Result::OK ||
        // RadioHead setFrequency(424.0): 0x52 selects the 420 MHz band and
        // fractional carrier 0x6400 adds 4 MHz (4 / 0.00015625).
        writeRegister(FREQ_BAND, 0x52) != Result::OK || writeRegister(FREQ_CARRIER_1, 0x64) != Result::OK ||
        writeRegister(FREQ_CARRIER_0, 0) != Result::OK || writeRegister(AFC_LIMITER, 0x50) != Result::OK ||
        writeRegister(TX_POWER, 0x0F) != Result::OK) {
        return Result::IO_ERROR;
    }
    std::uint8_t status = 0;
    if (readRegister(DEVICE_STATUS, status) != Result::OK || (status & FREQUENCY_ERROR) != 0U) {
        return Result::IO_ERROR;
    }
    return Result::OK;
}

Rfm23Zephyr::Result Rfm23Zephyr::failSafe(Result result) {
    (void)gpio_pin_interrupt_configure_dt(&m_irq, GPIO_INT_DISABLE);
    amplifierIdle();
    (void)gpio_pin_set_dt(&m_sdn, 1);
    m_ready = false;
    m_mode = MODE_IDLE;
    m_rxReady = false;
    m_txComplete = false;
    m_rssiValid = false;
    m_lastPreambleMs = 0;
    m_lastAcceptedRssiMs = 0;
    m_pendingRssiDbm = 0;
    m_lastRssiDbm = 0;
    atomic_set(&m_irqPending, 0);
    if (result == Result::TX_TIMEOUT) m_fault = Fault::LOCAL_TX;
    else if (result == Result::IO_ERROR || result == Result::INIT_TIMEOUT) m_fault = Fault::INIT_FAILED;
    return result;
}

Rfm23Zephyr::Result Rfm23Zephyr::probeAndInitialize() {
    (void)gpio_pin_interrupt_configure_dt(&m_irq, GPIO_INT_DISABLE);
    (void)gpio_pin_set_dt(&m_sdn, 1);
    amplifierIdle();
    k_msleep(50);
    (void)gpio_pin_set_dt(&m_sdn, 0);
    k_msleep(50);
    std::uint8_t first = 0;
    std::uint8_t second = 0;
    if (readRegister(DEVICE_TYPE, first) != Result::OK) return failSafe(Result::IO_ERROR);
    k_msleep(1);
    if (readRegister(DEVICE_TYPE, second) != Result::OK || first != second ||
        (first != DEV_TX && first != DEV_RX_TRX)) return failSafe(Result::IO_ERROR);
    if (writeRegister(MODE_1, SW_RESET) != Result::OK) return failSafe(Result::IO_ERROR);
    const std::uint32_t deadline = k_uptime_get_32() + INIT_TIMEOUT_MS;
    std::uint8_t irq2 = 0;
    while ((irq2 & CHIP_READY) == 0U) {
        if (static_cast<std::int32_t>(k_uptime_get_32() - deadline) >= 0) {
            return failSafe(Result::INIT_TIMEOUT);
        }
        if (readRegister(IRQ_STATUS_2, irq2) != Result::OK) return failSafe(Result::IO_ERROR);
        k_msleep(1);
    }
    // Exact RFM22-compatible packet setup followed by the Artemis RFM23BP profile.
    if (writeRegister(IRQ_ENABLE_1, 0xB7) != Result::OK || writeRegister(IRQ_ENABLE_2, 0x40) != Result::OK ||
        writeRegister(TX_FIFO_THRESHOLD, 4) != Result::OK || writeRegister(RX_FIFO_THRESHOLD, 55) != Result::OK ||
        writeRegister(DATA_ACCESS, 0x8D) != Result::OK || writeRegister(HEADER_CONTROL_1, 0x88) != Result::OK ||
        writeRegister(HEADER_CONTROL_2, 0x42) != Result::OK || writeRegister(PREAMBLE_LENGTH, 8) != Result::OK ||
        writeRegister(SYNC_1, 0x2D) != Result::OK || writeRegister(SYNC_0, 0xD4) != Result::OK ||
        writeRegister(HEADER_ENABLE_3, 0x00) != Result::OK || writeRegister(CHECK_HEADER_3, RF_LOCAL) != Result::OK ||
        writeRegister(TX_HEADER_3, RF_REMOTE) != Result::OK || writeRegister(TX_HEADER_2, RF_LOCAL) != Result::OK ||
        writeRegister(TX_HEADER_1, RF_NETWORK) != Result::OK || writeRegister(TX_HEADER_0, RF_VERSION) != Result::OK ||
        writeRegister(GPIO_0, 0x12) != Result::OK || writeRegister(GPIO_1, 0x15) != Result::OK ||
        configureModem() != Result::OK || clearFifosAndEnterRx() != Result::OK) {
        return failSafe(Result::IO_ERROR);
    }
    if (gpio_pin_interrupt_configure_dt(&m_irq, GPIO_INT_EDGE_TO_ACTIVE) != 0) return failSafe(Result::IO_ERROR);
    m_ready = true;
    m_fault = Fault::NONE;
    return Result::OK;
}

Rfm23Zephyr::Result Rfm23Zephyr::setEnabled(bool enabled) {
    if (!enabled) {
        m_fault = Fault::NONE;
        return failSafe(Result::OK);
    }
    if (m_ready && m_fault == Fault::NONE) return Result::OK;
    ++m_initAttempts;
    return probeAndInitialize();
}

Rfm23Zephyr::Result Rfm23Zephyr::processInterrupts(bool force) {
    if (!m_ready && m_mode != MODE_RX && m_mode != MODE_TX) return Result::NOT_READY;
    if (!force && !irqPending()) return Result::OK;
    std::uint8_t status1 = 0;
    std::uint8_t status2 = 0;
    if (readRegister(IRQ_STATUS_1, status1) != Result::OK || readRegister(IRQ_STATUS_2, status2) != Result::OK) {
        return failSafe(Result::IO_ERROR);
    }
    m_lastIrqStatus1 = status1;
    m_lastIrqStatus2 = status2;
    if ((status1 & FIFO_ERROR) != 0U) {
        ++m_rxBad;
        if (recoverTransmitPath() != Result::OK) return failSafe(Result::IO_ERROR);
    }
    if ((status1 & PACKET_SENT) != 0U) {
        m_txComplete = true;
        m_mode = MODE_IDLE;
        ++m_txGood;
    }
    if ((status1 & PACKET_VALID) != 0U) {
        std::uint8_t length = 0;
        if (readRegister(RX_PACKET_LENGTH, length) != Result::OK || length > MAX_PACKET_LENGTH ||
            readBurst(FIFO, m_rxPacket, length) != Result::OK || readRegister(RX_HEADER_3, m_lastReceivedHeader.to) != Result::OK ||
            readRegister(RX_HEADER_2, m_lastReceivedHeader.from) != Result::OK || readRegister(RX_HEADER_1, m_lastReceivedHeader.id) != Result::OK ||
            readRegister(RX_HEADER_0, m_lastReceivedHeader.flags) != Result::OK) {
            ++m_rxBad;
            return failSafe(Result::IO_ERROR);
        }
        m_rxLength = length;
        m_rxReady = true;
        ++m_rxGood;
        m_mode = MODE_IDLE;
    }
    if ((status1 & CRC_ERROR) != 0U) {
        ++m_rxBad;
        m_rxReady = false;
        m_rxLength = 0;
        if (recoverTransmitPath() != Result::OK) return failSafe(Result::IO_ERROR);
        return Result::OK;
    }
    if ((status2 & PREAMBLE_VALID) != 0U) {
        std::uint8_t rawRssi = 0;
        if (readRegister(RSSI, rawRssi) != Result::OK) return failSafe(Result::IO_ERROR);
        m_pendingRssiDbm = static_cast<std::int16_t>(-120 + rawRssi / 2);
        m_lastPreambleMs = k_uptime_get_32();
        std::uint8_t mode2 = 0;
        if (readRegister(MODE_2, mode2) != Result::OK ||
            writeRegister(MODE_2, static_cast<std::uint8_t>(mode2 | CLEAR_RX)) != Result::OK ||
            writeRegister(MODE_2, mode2) != Result::OK) return failSafe(Result::IO_ERROR);
        m_rxReady = false;
        m_rxLength = 0;
    }
    return Result::OK;
}

void Rfm23Zephyr::markLastPacketAccepted() {
    m_lastRssiDbm = m_pendingRssiDbm;
    m_lastAcceptedRssiMs = k_uptime_get_32();
    m_rssiValid = true;
}

std::uint32_t Rfm23Zephyr::lastAcceptedRssiAgeMs() const {
    return m_rssiValid ? k_uptime_get_32() - m_lastAcceptedRssiMs : 0xFFFFFFFFU;
}

Rfm23Zephyr::Result Rfm23Zephyr::serviceInterrupt() { return processInterrupts(false); }

bool Rfm23Zephyr::available() {
    if (!m_ready) return false;
    (void)processInterrupts(true);
    return m_rxReady;
}

bool Rfm23Zephyr::receiveInProgress() const {
    return m_ready && m_mode == MODE_RX && m_lastPreambleMs != 0U &&
           (k_uptime_get_32() - m_lastPreambleMs) < PREAMBLE_GUARD_MS;
}

Rfm23Zephyr::Result Rfm23Zephyr::receivePacket(std::uint8_t* data, std::size_t capacity, std::size_t& length) {
    length = 0;
    if (data == nullptr || capacity == 0) return Result::INVALID_ARGUMENT;
    if (!m_ready) return Result::NOT_READY;
    const Result irqResult = processInterrupts(true);
    if (irqResult != Result::OK) return irqResult;
    if (!m_rxReady) return Result::NO_PACKET;
    if (m_rxLength > capacity) {
        m_rxReady = false;
        m_rxLength = 0;
        return Result::INVALID_ARGUMENT;
    }
    for (std::size_t i = 0; i < m_rxLength; ++i) data[i] = m_rxPacket[i];
    length = m_rxLength;
    m_rxReady = false;
    m_rxLength = 0;
    return clearFifosAndEnterRx() == Result::OK ? Result::OK : failSafe(Result::IO_ERROR);
}

Rfm23Zephyr::Result Rfm23Zephyr::sendPacket(const std::uint8_t* data, std::size_t length) {
    if (data == nullptr || length == 0 || length > MAX_PACKET_LENGTH) return Result::INVALID_ARGUMENT;
    if (!m_ready) return Result::NOT_READY;
    if (m_mode == MODE_TX) {
        m_fault = Fault::LOCAL_TX;
        return recoverTransmitPath() == Result::OK ? Result::TX_TIMEOUT : failSafe(Result::TX_TIMEOUT);
    }
    if (enterIdleMode() != Result::OK) return failSafe(Result::IO_ERROR);
    m_rxReady = false;
    m_rxLength = 0;
    m_txComplete = false;
    amplifierTransmit();
    if (writeRegister(TX_HEADER_3, RF_REMOTE) != Result::OK || writeRegister(TX_HEADER_2, RF_LOCAL) != Result::OK ||
        writeRegister(TX_HEADER_1, RF_NETWORK) != Result::OK || writeRegister(TX_HEADER_0, RF_VERSION) != Result::OK ||
        writeRegister(PACKET_LENGTH, static_cast<std::uint8_t>(length)) != Result::OK || writeBurst(FIFO, data, length) != Result::OK ||
        writeRegister(MODE_1, XTON | TXON) != Result::OK) return failSafe(Result::IO_ERROR);
    m_mode = MODE_TX;
    const std::uint32_t deadline = k_uptime_get_32() + TX_TIMEOUT_MS;
    while (!m_txComplete) {
        const Result irqResult = processInterrupts(true);
        if (irqResult != Result::OK) return irqResult;
        if (m_txComplete) break;
        if (static_cast<std::int32_t>(k_uptime_get_32() - deadline) >= 0) {
            m_fault = Fault::LOCAL_TX;
            return recoverTransmitPath() == Result::OK ? Result::TX_TIMEOUT : failSafe(Result::TX_TIMEOUT);
        }
        k_msleep(1);
    }
    m_fault = Fault::NONE;
    return enterReceiveMode() == Result::OK ? Result::OK : failSafe(Result::IO_ERROR);
}

void Rfm23Zephyr::failSafeOffLocalTx() { (void)failSafe(Result::TX_TIMEOUT); }

void Rfm23Zephyr::irqCallback(const device*, gpio_callback*, gpio_port_pins_t) {
    if (s_instance != nullptr) (void)k_work_submit(&s_instance->m_irqWork);
}

void Rfm23Zephyr::irqWork(k_work*) {
    if (s_instance != nullptr) atomic_set(&s_instance->m_irqPending, 1);
}

bool Rfm23Zephyr::irqPending() { return atomic_cas(&m_irqPending, 1, 0); }

}  // namespace SatelliteController
