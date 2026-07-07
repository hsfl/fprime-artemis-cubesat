#include "Components/ThermalManager/ThermalManager.hpp"

namespace Components {

ThermalManager::ThermalManager(const char* const compName)
    : ThermalManagerComponentBase(compName),
      m_state(0),
      m_mode(0),
      m_managerHeartbeat(0) {}

ThermalManager::~ThermalManager() {}

void ThermalManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void ThermalManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_managerHeartbeat += 1;

    if (this->isConnected_driverRequestOut_OutputPort(0)) {
        this->driverRequestOut_out(0, this->m_managerHeartbeat);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        const Components::HealthState health =
            (this->m_state == 0U) ? Components::HealthState::UNKNOWN : Components::HealthState::OK;
        this->sohStatusOut_out(0, health, this->m_state);
    }

    this->tlmWrite_ThermalState(this->m_state);
    this->tlmWrite_ThermalMode(this->m_mode);
    this->tlmWrite_ManagerHeartbeat(this->m_managerHeartbeat);
}

void ThermalManager::driverStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_state = key;
    this->log_ACTIVITY_LO_ThermalStatusUpdated(this->m_state);
}

void ThermalManager::REQUEST_THERMAL_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_driverRequestOut_OutputPort(0)) {
        this->driverRequestOut_out(0, this->m_managerHeartbeat + 1);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ThermalManager::SET_THERMAL_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 mode) {
    this->m_mode = (mode <= 2U) ? mode : 0U;
    this->log_ACTIVITY_HI_ThermalModeUpdated(this->m_mode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
