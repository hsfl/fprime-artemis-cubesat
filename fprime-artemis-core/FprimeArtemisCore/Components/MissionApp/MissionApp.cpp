// ======================================================================
// \title  MissionApp.cpp
// \brief  cpp file for MissionApp component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/MissionApp/MissionApp.hpp"

namespace Components {

MissionApp::MissionApp(const char* const compName) : MissionAppComponentBase(compName) {}

MissionApp::~MissionApp() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void MissionApp::rpiStateIn_handler(FwIndexType portNum, const Components::RpiPowerState& state) {
    this->m_rpiState = state;

    switch (this->m_mode.e) {
        case MissionMode::ENTERING_BASE:
            if (state == RpiPowerState::READY) {
                this->setMode(MissionMode::BASE);
            } else if (state == RpiPowerState::OFF) {
                // Powered off during entry, e.g. by the engineering command.
                // Already off, so no power request is needed.
                this->log_WARNING_HI_BaseModeFailed(BaseModeFailure::POWERED_OFF);
                this->setMode(MissionMode::STANDBY);
            }
            break;

        case MissionMode::BASE:
            // Downgrade to match reality: STANDBY means the payload computer
            // is off. A drop to BOOT is report-only; systemd restarts the
            // payload deployment and RpiState recovers.
            if (state == RpiPowerState::OFF) {
                this->setMode(MissionMode::STANDBY);
            }
            break;

        case MissionMode::STANDBY:
        default:
            // An engineering power-on while in STANDBY does not enter BASE:
            // upgrades always go through ENTER_BASE_MODE.
            break;
    }
}

void MissionApp::run_handler(FwIndexType portNum, U32 context) {
    // The power pin's level at boot is whatever it reset to, so STANDBY is
    // enforced rather than assumed. This waits for the first tick because
    // the GPIO driver is opened after components are configured.
    if (!this->m_standbyEnforced) {
        this->m_standbyEnforced = true;
        this->goToStandby();
    }

    if (this->m_mode == MissionMode::ENTERING_BASE) {
        this->m_entryTicks++;
        if (this->m_entryTicks >= BASE_ENTRY_TIMEOUT_TICKS) {
            this->failBaseEntry(BaseModeFailure::TIMEOUT);
        }
    }

    this->tlmWrite_CurrentMode(this->m_mode);
}

void MissionApp::pingIn_handler(FwIndexType portNum, U32 key) {
    this->pingOut_out(0, key);
}

// ----------------------------------------------------------------------
// Command handlers
// ----------------------------------------------------------------------

void MissionApp::ENTER_BASE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    switch (this->m_mode.e) {
        case MissionMode::BASE:
            // Already there.
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
            return;

        case MissionMode::ENTERING_BASE:
            this->log_WARNING_LO_ModeCommandRejected(this->m_mode);
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
            return;

        case MissionMode::STANDBY:
        default:
            break;
    }

    if (this->rpiPowerRequestOut_out(0, Fw::On::ON) != Fw::Success::SUCCESS) {
        this->log_WARNING_HI_BaseModeFailed(BaseModeFailure::POWER_REQUEST_FAILED);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    this->m_entryTicks = 0;
    // The payload computer may already be up, e.g. powered on with the
    // engineering command. A redundant power-on changes nothing, so no state
    // report will arrive; enter BASE directly.
    if (this->m_rpiState == RpiPowerState::READY) {
        this->setMode(MissionMode::BASE);
    } else {
        this->setMode(MissionMode::ENTERING_BASE);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionApp::ENTER_STANDBY_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Always request power-off, even from STANDBY: the payload computer may
    // have been powered on with the engineering command.
    this->goToStandby();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void MissionApp::setMode(MissionMode mode) {
    if (mode != this->m_mode) {
        this->m_mode = mode;
        this->log_ACTIVITY_HI_ModeChanged(mode);
        this->tlmWrite_CurrentMode(mode);
    }
}

void MissionApp::goToStandby() {
    // A failed power-off leaves the rail state unknown; RpiPowerManager
    // reports it with RpiGpioError. STANDBY is still entered, because MissionApp
    // cannot do more from here.
    (void)this->rpiPowerRequestOut_out(0, Fw::On::OFF);
    this->setMode(MissionMode::STANDBY);
}

void MissionApp::failBaseEntry(BaseModeFailure reason) {
    this->log_WARNING_HI_BaseModeFailed(reason);
    this->goToStandby();
}

}  // namespace Components
