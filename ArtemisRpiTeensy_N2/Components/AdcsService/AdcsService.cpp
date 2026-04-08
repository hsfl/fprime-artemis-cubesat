#include "Components/AdcsService/AdcsService.hpp"

namespace Components {

AdcsService::AdcsService(const char* const compName)
    : AdcsServiceComponentBase(compName),
      m_state(0),
      m_serviceHeartbeat(0) {}

AdcsService::~AdcsService() {}

void AdcsService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void AdcsService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_serviceHeartbeat += 1;

    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_state);
    }

    this->tlmWrite_AdcsState(this->m_state);
    this->tlmWrite_ServiceHeartbeat(this->m_serviceHeartbeat);
}

void AdcsService::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_state = key;
    this->log_ACTIVITY_LO_AdcsStatusUpdated(this->m_state);
}

void AdcsService::REQUEST_ADCS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat + 1);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
