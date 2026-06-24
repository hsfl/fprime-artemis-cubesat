#include "Components/GpsService/GpsService.hpp"

namespace Components {

GpsService::GpsService(const char* const compName)
    : GpsServiceComponentBase(compName),
      m_state(0),
      m_serviceHeartbeat(0) {}

GpsService::~GpsService() {}

void GpsService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void GpsService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_serviceHeartbeat += 1;

    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        const Components::HealthState health =
            (this->m_state == 0U) ? Components::HealthState::UNKNOWN : Components::HealthState::OK;
        this->sohStatusOut_out(0, health, this->m_state);
    }

    this->tlmWrite_GpsFixState(this->m_state);
    this->tlmWrite_ServiceHeartbeat(this->m_serviceHeartbeat);
}

void GpsService::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_state = key;
    this->log_ACTIVITY_LO_GpsStatusUpdated(this->m_state);
}

void GpsService::REQUEST_GPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat + 1);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
