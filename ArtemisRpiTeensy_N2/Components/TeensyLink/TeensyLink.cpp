#include "Components/TeensyLink/TeensyLink.hpp"

namespace Components {

TeensyLink::TeensyLink(const char* const compName)
    : TeensyLinkComponentBase(compName),
      m_linkHeartbeat(0),
      m_framingDrops(0),
      m_timeoutEvents(0) {}

TeensyLink::~TeensyLink() {}

void TeensyLink::pingIn_handler(FwIndexType portNum, U32 key) {
    this->pingOut_out(0, key);
}

void TeensyLink::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_linkHeartbeat += 1;
    this->tlmWrite_LinkHeartbeat(this->m_linkHeartbeat);
    this->tlmWrite_FramingDrops(this->m_framingDrops);
    this->tlmWrite_TimeoutEvents(this->m_timeoutEvents);
}

void TeensyLink::LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_HI_LinkStatus(this->m_linkHeartbeat, this->m_framingDrops, this->m_timeoutEvents);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void TeensyLink::RESET_COUNTERS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_linkHeartbeat = 0;
    this->m_framingDrops = 0;
    this->m_timeoutEvents = 0;
    this->log_ACTIVITY_LO_CountersReset();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
