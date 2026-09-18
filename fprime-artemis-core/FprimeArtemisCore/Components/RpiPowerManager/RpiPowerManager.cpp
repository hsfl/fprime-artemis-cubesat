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

void RpiPowerManager::SET_RPI_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::On& state) {
    const bool turnOn = (state == Fw::On::ON);
    const Fw::Logic level = turnOn ? Fw::Logic::HIGH : Fw::Logic::LOW;

    const Drv::GpioStatus status = this->gpioSet_out(0, level);
    if (status != Drv::GpioStatus::OP_OK) {
        this->log_WARNING_HI_RpiGpioError(status);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    this->log_ACTIVITY_HI_RpiPowerCommanded(state);
    if (turnOn) {
        this->m_powerCycles++;
        // The rail is up but the payload computer has not reported in yet.
        this->m_ticksSincePeer = 0;
        this->setState(RpiPowerState::BOOT);
    } else {
        this->setState(RpiPowerState::OFF);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void RpiPowerManager::GET_RPI_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_LO_RpiStatusReport(this->m_state);
    this->tlmWrite_RpiState(this->m_state);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void RpiPowerManager::setState(RpiPowerState state) {
    if (state != this->m_state) {
        this->m_state = state;
        this->log_ACTIVITY_HI_RpiStateChanged(state);
        this->tlmWrite_RpiState(state);
    }
}

}  // namespace Components
