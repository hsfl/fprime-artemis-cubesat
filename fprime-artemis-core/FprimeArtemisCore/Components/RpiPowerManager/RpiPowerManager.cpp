// ======================================================================
// \title  RpiPowerManager.cpp
// \brief  cpp file for RpiPowerManager component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/RpiPowerManager/RpiPowerManager.hpp"

namespace Components {

RpiPowerManager::RpiPowerManager(const char* const compName) : RpiPowerManagerComponentBase(compName) {}

RpiPowerManager::~RpiPowerManager() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void RpiPowerManager::run_handler(FwIndexType portNum, U32 context) {
    // A peer heartbeat only means anything while the rail is energized.
    if (this->m_state != RpiPowerState::OFF) {
        if (this->m_ticksSincePeer < PEER_TIMEOUT_TICKS) {
            this->m_ticksSincePeer++;
        } else if (this->m_state == RpiPowerState::READY) {
            // The payload computer stopped reporting: it is powered but no
            // longer known to be up.
            this->setState(RpiPowerState::BOOT);
        }
    }
    this->tlmWrite_RpiState(this->m_state);
    this->tlmWrite_RpiPowerCycles(this->m_powerCycles);
}

void RpiPowerManager::peerAliveIn_handler(FwIndexType portNum, U32 key) {
    this->m_ticksSincePeer = 0;
    // Ignore a heartbeat while the rail is off: the state would be inconsistent
    // with the hardware, and something else is wrong.
    if (this->m_state == RpiPowerState::BOOT) {
        this->setState(RpiPowerState::READY);
    }
}

// ----------------------------------------------------------------------
// Command handlers
// ----------------------------------------------------------------------

Fw::Success RpiPowerManager::powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) {
    return this->applyPower(state);
}

void RpiPowerManager::SET_RPI_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::On& state) {
    const Fw::Success status = this->applyPower(state);
    this->cmdResponse_out(opCode, cmdSeq,
                          (status == Fw::Success::SUCCESS) ? Fw::CmdResponse::OK
                                                           : Fw::CmdResponse::EXECUTION_ERROR);
}

void RpiPowerManager::GET_RPI_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_LO_RpiStatusReport(this->m_state);
    this->tlmWrite_RpiState(this->m_state);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

Fw::Success RpiPowerManager::applyPower(const Fw::On& state) {
    const bool turnOn = (state == Fw::On::ON);
    const Fw::Logic level = turnOn ? Fw::Logic::HIGH : Fw::Logic::LOW;

    const Drv::GpioStatus status = this->gpioSet_out(0, level);
    if (status != Drv::GpioStatus::OP_OK) {
        this->log_WARNING_HI_RpiGpioError(status);
        return Fw::Success::FAILURE;
    }

    this->log_ACTIVITY_HI_RpiPowerCommanded(state);
    if (turnOn) {
        // A redundant power-on (rail already up) changes nothing: it must not
        // demote READY to BOOT or count a power cycle that did not happen.
        if (this->m_state == RpiPowerState::OFF) {
            this->m_powerCycles++;
            // The rail is up but the payload computer has not reported in yet.
            this->m_ticksSincePeer = 0;
            this->setState(RpiPowerState::BOOT);
        }
    } else {
        this->setState(RpiPowerState::OFF);
    }
    return Fw::Success::SUCCESS;
}

void RpiPowerManager::setState(RpiPowerState state) {
    if (state != this->m_state) {
        this->m_state = state;
        this->log_ACTIVITY_HI_RpiStateChanged(state);
        this->tlmWrite_RpiState(state);
        if (this->isConnected_stateOut_OutputPort(0)) {
            this->stateOut_out(0, state);
        }
    }
}

}  // namespace Components
