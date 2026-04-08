#include "Components/PayloadService/PayloadService.hpp"

namespace Components {

PayloadService::PayloadService(const char* const compName)
    : PayloadServiceComponentBase(compName),
      m_lastPayloadValue(0),
      m_serviceHeartbeat(0) {}

PayloadService::~PayloadService() {}

void PayloadService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_serviceHeartbeat += 1;
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_lastPayloadValue);
    }
    this->tlmWrite_LastPayloadValue(this->m_lastPayloadValue);
    this->tlmWrite_ServiceHeartbeat(this->m_serviceHeartbeat);
}

void PayloadService::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->log_ACTIVITY_HI_PayloadCollectionForwarded(key);
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, key);
    }
}

void PayloadService::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastPayloadValue = key;
    this->log_ACTIVITY_LO_PayloadStatusUpdated(key);
    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, key);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, key);
    }
}

void PayloadService::REQUEST_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat + 1);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
