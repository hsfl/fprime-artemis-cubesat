#include "Components/PayloadService/PayloadService.hpp"

namespace Components {

PayloadService::PayloadService(const char* const compName)
    : PayloadServiceComponentBase(compName),
      m_lastPayloadValue(0),
      m_lastCollectionId(0),
      m_sampleCount(1),
      m_samplePeriodMs(1000),
      m_simModeEnabled(1),
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
    this->tlmWrite_LastCollectionId(this->m_lastCollectionId);
    this->tlmWrite_SampleCount(this->m_sampleCount);
    this->tlmWrite_SamplePeriodMs(this->m_samplePeriodMs);
    this->tlmWrite_SimModeEnabled(this->m_simModeEnabled);
    this->tlmWrite_ServiceHeartbeat(this->m_serviceHeartbeat);
}

void PayloadService::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastCollectionId = key;
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

void PayloadService::CONFIGURE_PAYLOAD_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    U32 sampleCount,
    U32 periodMs
) {
    this->m_sampleCount = sampleCount;
    this->m_samplePeriodMs = periodMs;
    this->log_ACTIVITY_HI_PayloadConfigured(this->m_sampleCount, this->m_samplePeriodMs);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadService::START_PAYLOAD_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 collectionId) {
    this->m_lastCollectionId = collectionId;
    this->log_ACTIVITY_HI_PayloadCollectionForwarded(collectionId);
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, collectionId);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadService::SET_PAYLOAD_SIM_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 enable) {
    this->m_simModeEnabled = (enable == 0U) ? 0U : 1U;
    this->log_ACTIVITY_HI_PayloadSimModeChanged(this->m_simModeEnabled);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
