#include "Components/TeensyTransportService/TeensyTransportService.hpp"

namespace Components {

TeensyTransportService::TeensyTransportService(const char* const compName)
    : TeensyTransportServiceComponentBase(compName),
      m_linkHeartbeat(0),
      m_uplinkFrames(0),
      m_downlinkFrames(0) {}

TeensyTransportService::~TeensyTransportService() {}

void TeensyTransportService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void TeensyTransportService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_linkHeartbeat += 1;

    this->tlmWrite_LinkHeartbeat(this->m_linkHeartbeat);
    this->tlmWrite_UplinkFrames(this->m_uplinkFrames);
    this->tlmWrite_DownlinkFrames(this->m_downlinkFrames);

    if (this->isConnected_linkStatusOut_OutputPort(0)) {
        this->linkStatusOut_out(0, this->m_linkHeartbeat);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_linkHeartbeat);
    }
}

void TeensyTransportService::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_uplinkFrames += 1;
    this->m_downlinkFrames = key;
}

void TeensyTransportService::LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_HI_LinkStatus(this->m_linkHeartbeat, this->m_uplinkFrames, this->m_downlinkFrames);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void TeensyTransportService::RESET_COUNTERS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_linkHeartbeat = 0;
    this->m_uplinkFrames = 0;
    this->m_downlinkFrames = 0;
    this->log_ACTIVITY_LO_CountersReset();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
