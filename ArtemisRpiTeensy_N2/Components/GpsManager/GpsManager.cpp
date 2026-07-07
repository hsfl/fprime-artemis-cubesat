#include "Components/GpsManager/GpsManager.hpp"

namespace Components {

GpsManager::GpsManager(const char* const compName)
    : GpsManagerComponentBase(compName),
      m_state(0),
      m_managerHeartbeat(0) {}

GpsManager::~GpsManager() {}

void GpsManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void GpsManager::run_handler(FwIndexType portNum, U32 context) {
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

    this->tlmWrite_GpsFixState(this->m_state);
    this->tlmWrite_ManagerHeartbeat(this->m_managerHeartbeat);
}

void GpsManager::driverStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_state = key;
    this->log_ACTIVITY_LO_GpsStatusUpdated(this->m_state);
}

void GpsManager::REQUEST_GPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_driverRequestOut_OutputPort(0)) {
        this->driverRequestOut_out(0, this->m_managerHeartbeat + 1);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
