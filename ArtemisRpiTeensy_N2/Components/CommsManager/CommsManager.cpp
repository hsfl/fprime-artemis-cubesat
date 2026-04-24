#include "Components/CommsManager/CommsManager.hpp"

namespace Components {

CommsManager::CommsManager(const char* const compName)
    : CommsManagerComponentBase(compName),
      m_linkState(0),
      m_pendingScienceBytes(0) {}

CommsManager::~CommsManager() {}

void CommsManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void CommsManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);

    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        const U32 normalizedLink = (this->m_linkState <= 3U) ? this->m_linkState : 0U;
        // Adapter poll key contract: 1=down, 2=acquiring, 3=locked, 4=degraded
        this->adapterRequestOut_out(0, normalizedLink + 1U);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_linkState);
    }

    this->tlmWrite_LinkState(this->m_linkState);
    this->tlmWrite_PendingScienceBytes(this->m_pendingScienceBytes);
}

void CommsManager::linkStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_linkState = key;
    this->log_ACTIVITY_LO_LinkStateUpdated(this->m_linkState);
}

void CommsManager::scienceReadyIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_pendingScienceBytes = key;
}

void CommsManager::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_linkState = key;
    this->log_ACTIVITY_LO_LinkStateUpdated(this->m_linkState);
}

void CommsManager::REQUEST_SCIENCE_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->isConnected_downlinkRequestOut_OutputPort(0)) {
        this->downlinkRequestOut_out(0, this->m_pendingScienceBytes);
    }
    this->log_ACTIVITY_HI_DownlinkRequested(this->m_pendingScienceBytes);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
