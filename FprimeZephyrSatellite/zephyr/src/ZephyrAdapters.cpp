#include "SatelliteController/ZephyrAdapters.hpp"

#include <limits>

namespace SatelliteController {

bool ZephyrRfStatusProvider::setEnabled(bool enabled) {
    m_bootFaultActive = false;
    return m_radio.setEnabled(enabled) == Rfm23Zephyr::Result::OK;
}

std::uint16_t ZephyrRfStatusProvider::narrowCounter(std::uint32_t value) {
    constexpr auto MAX = std::numeric_limits<std::uint16_t>::max();
    return value > MAX ? MAX : static_cast<std::uint16_t>(value);
}

RfStatusSnapshot ZephyrRfStatusProvider::status() const {
    RfStatusSnapshot snapshot{};
    snapshot.lastRssiDbm = m_radio.rssiValid() ? m_radio.lastRssiDbm() : -127;
    snapshot.rxGood = narrowCounter(m_radio.rxGood());
    snapshot.rxBad = narrowCounter(m_radio.rxBad());
    snapshot.txGood = narrowCounter(m_radio.txGood());
    snapshot.rfRxPackets = m_rxPackets;
    snapshot.rfTxPackets = m_txPackets;
    snapshot.rfTxDrops = m_txDrops;
    snapshot.state = m_radio.ready() ? RadioState::READY : RadioState::OFF;
    snapshot.fault = static_cast<RadioFault>(m_radio.fault());
    if (m_bootFaultActive) snapshot.fault = RadioFault::WATCHDOG_RESET;
    snapshot.bootFlags = m_watchdogReset ? 1U : 0U;
    snapshot.rssiValid = m_radio.rssiValid();
    snapshot.initAttempts = m_radio.initAttempts();
    snapshot.lastAcceptedRssiAgeMs = m_radio.lastAcceptedRssiAgeMs();
    return snapshot;
}

}  // namespace SatelliteController
