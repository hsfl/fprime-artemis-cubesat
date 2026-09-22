// ======================================================================
// \title  ImuManager.cpp
// \brief  cpp file for ImuManager component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/ImuManager/ImuManager.hpp"

namespace Components {

ImuManager::ImuManager(const char* const compName) : ImuManagerComponentBase(compName) {}

ImuManager::~ImuManager() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void ImuManager::run_handler(FwIndexType portNum, U32 context) {
    // A Teensy reset does not reset the IMU: it may still be sampling from
    // before. Force it off so the OFF state is true, not assumed.
    if (!this->m_bootPowerOffDone) {
        this->m_bootPowerOffDone = true;
        (void)this->applyPower(Fw::On::OFF);
    } else if (this->m_state == ImuState::ON) {
        this->readOnce();
    }
    this->tlmWrite_ImuState(this->m_state);
    this->tlmWrite_ReadErrors(this->m_readErrors);
}

Fw::Success ImuManager::powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) {
    return this->applyPower(state);
}

// ----------------------------------------------------------------------
// Command handlers
// ----------------------------------------------------------------------

void ImuManager::SET_IMU_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::On& state) {
    const Fw::Success status = this->applyPower(state);
    this->cmdResponse_out(opCode, cmdSeq,
                          (status == Fw::Success::SUCCESS) ? Fw::CmdResponse::OK
                                                           : Fw::CmdResponse::EXECUTION_ERROR);
}

void ImuManager::GET_IMU_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_LO_ImuStatusReport(this->m_state, this->m_readErrors);
    this->tlmWrite_ImuState(this->m_state);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

Fw::Success ImuManager::applyPower(const Fw::On& state) {
    // A redundant power-on must not reset a chip that is already sampling
    if ((state == Fw::On::ON) && (this->m_state == ImuState::ON)) {
        return Fw::Success::SUCCESS;
    }

    const Fw::Success status = this->driverPowerOut_out(0, state);
    if (status != Fw::Success::SUCCESS) {
        this->log_WARNING_HI_ImuPowerFailed(state);
        // FAULT means "requested on but not responding". After a failed OFF
        // the driver has stopped reading, so no reading will be claimed: OFF
        // is the honest state, and it matches the driver's PowerState.
        this->setState((state == Fw::On::ON) ? ImuState::FAULT : ImuState::OFF);
        return Fw::Success::FAILURE;
    }

    this->m_consecutiveErrors = 0;
    this->setState((state == Fw::On::ON) ? ImuState::ON : ImuState::OFF);
    return Fw::Success::SUCCESS;
}

void ImuManager::readOnce() {
    ImuReading reading;
    const ImuReadStatus status = this->driverReadingGet_out(0, reading);
    if (status == ImuReadStatus::OK) {
        this->m_consecutiveErrors = 0;
        this->tlmWrite_Acceleration(reading.get_acceleration());
        this->tlmWrite_AngularRate(reading.get_angularRate());
        this->tlmWrite_ImuTemperature(reading.get_temperature());
        return;
    }

    // BUS_ERROR, or POWERED_OFF while this manager believes the IMU is ON:
    // either way no reading arrived.
    this->m_readErrors++;
    this->m_consecutiveErrors++;
    if (this->m_consecutiveErrors >= FAULT_THRESHOLD) {
        this->log_WARNING_HI_ImuFaulted(this->m_consecutiveErrors);
        this->setState(ImuState::FAULT);
    }
}

void ImuManager::setState(ImuState state) {
    if (state != this->m_state) {
        this->m_state = state;
        this->log_ACTIVITY_HI_ImuStateChanged(state);
        this->tlmWrite_ImuState(state);
    }
}

}  // namespace Components
