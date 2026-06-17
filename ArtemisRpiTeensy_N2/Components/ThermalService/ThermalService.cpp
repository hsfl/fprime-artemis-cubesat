#include "Components/ThermalService/ThermalService.hpp"

namespace Components {

ThermalService::ThermalService(const char* const compName)
    : ThermalServiceComponentBase(compName),
      m_state(0),
      m_mode(0),
      m_serviceHeartbeat(0) {}

ThermalService::~ThermalService() {}

void ThermalService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void ThermalService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_serviceHeartbeat += 1;

    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_state);
    }

    this->tlmWrite_ThermalState(this->m_state);
    this->tlmWrite_ThermalMode(this->m_mode);
    this->tlmWrite_ServiceHeartbeat(this->m_serviceHeartbeat);
}

void ThermalService::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_state = key;
    this->log_ACTIVITY_LO_ThermalStatusUpdated(this->m_state);
}

void ThermalService::REQUEST_THERMAL_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat + 1);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ThermalService::SET_THERMAL_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 mode) {
    this->m_mode = (mode <= 2U) ? mode : 0U;
    this->log_ACTIVITY_HI_ThermalModeUpdated(this->m_mode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
