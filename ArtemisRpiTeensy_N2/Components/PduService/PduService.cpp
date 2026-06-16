#include "Components/PduService/PduService.hpp"

namespace Components {

PduService::PduService(const char* const compName)
    : PduServiceComponentBase(compName),
      m_state(0),
      m_payload28VRequestState(0),
      m_satnogsPowerRequestState(0),
      m_serviceHeartbeat(0) {}

PduService::~PduService() {}

void PduService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PduService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_serviceHeartbeat += 1;

    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_state);
    }

    this->tlmWrite_PduHealthState(this->m_state);
    this->tlmWrite_Payload28VRequestState(this->m_payload28VRequestState);
    this->tlmWrite_SatnogsPowerRequestState(this->m_satnogsPowerRequestState);
    this->tlmWrite_ServiceHeartbeat(this->m_serviceHeartbeat);
}

void PduService::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_state = key;
    this->log_ACTIVITY_LO_PduStatusUpdated(this->m_state);
}

void PduService::REQUEST_PDU_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, this->m_serviceHeartbeat + 1U);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PduService::SET_PAYLOAD_28V_REQUEST_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 requestState) {
    this->m_payload28VRequestState = (requestState == 0U) ? 0U : 1U;
    this->log_ACTIVITY_HI_Payload28VRequestRecorded(this->m_payload28VRequestState);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PduService::SET_SATNOGS_POWER_REQUEST_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 requestState) {
    this->m_satnogsPowerRequestState = (requestState == 0U) ? 0U : 1U;
    this->log_ACTIVITY_HI_SatnogsPowerRequestRecorded(this->m_satnogsPowerRequestState);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
