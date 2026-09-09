#include "SatelliteController/RadioStateMachine.hpp"

namespace SatelliteController {

RadioAction RadioStateMachine::boot(bool watchdogReset) {
    m_state = RadioState::OFF;
    m_fault = watchdogReset ? RadioFault::WATCHDOG_RESET : RadioFault::NONE;
    return RadioAction::SAFE_OFF;
}

RadioAction RadioStateMachine::setEnabled(bool enabled) {
    if (!enabled) {
        m_state = RadioState::OFF;
        m_fault = RadioFault::NONE;
        return RadioAction::SAFE_OFF;
    }
    if (m_state == RadioState::READY && m_fault == RadioFault::NONE) return RadioAction::NONE;
    m_state = RadioState::INITIALIZING;
    ++m_initAttempts;
    return RadioAction::START_INIT;
}

RadioAction RadioStateMachine::initComplete(bool success) {
    if (m_state != RadioState::INITIALIZING) return RadioAction::NONE;
    if (!success) {
        m_state = RadioState::OFF;
        m_fault = RadioFault::INIT_FAILED;
        return RadioAction::SAFE_OFF;
    }
    m_state = RadioState::READY;
    m_fault = RadioFault::NONE;
    return RadioAction::ENTER_RX;
}

RadioAction RadioStateMachine::requestTx() {
    if (m_state != RadioState::READY) return RadioAction::NONE;
    m_state = RadioState::TX;
    return RadioAction::START_TX;
}

RadioAction RadioStateMachine::txComplete(bool success) {
    if (m_state != RadioState::TX) return RadioAction::NONE;
    if (success) {
        m_state = RadioState::READY;
        m_fault = RadioFault::NONE;
        return RadioAction::ENTER_RX;
    }
    m_state = RadioState::RECOVERING;
    m_fault = RadioFault::LOCAL_TX;
    return RadioAction::RECOVER_FIFO;
}

RadioAction RadioStateMachine::recoveryComplete(bool success) {
    if (m_state != RadioState::RECOVERING) return RadioAction::NONE;
    if (success) {
        m_state = RadioState::READY;
        // Recovery alone does not erase the factual timeout. The Arduino
        // oracle clears LOCAL_TX only after a subsequent send succeeds.
        return RadioAction::ENTER_RX;
    }
    m_state = RadioState::OFF;
    m_fault = RadioFault::LOCAL_TX;
    return RadioAction::SAFE_OFF;
}

}  // namespace SatelliteController
