#include "Components/CommsManager/CommsManager.hpp"

namespace Components {

CommsManager::CommsManager(const char* const compName)
    : CommsManagerComponentBase(compName),
      m_linkState(0),
      m_pendingScienceBytes(0),
      m_radioBackend(0),
      m_linkPollCount(0),
      m_activeDownlinkBytes(0),
      m_lastPayloadDownlinkState(0),
      m_downlinkActive(false) {}

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
        Components::HealthState health = Components::HealthState::UNKNOWN;
        if (this->m_linkState == 2U) {
            health = Components::HealthState::OK;
        } else if ((this->m_linkState == 1U) || (this->m_linkState == 3U)) {
            health = Components::HealthState::WARN;
        } else if (this->m_linkState == 0U) {
            health = Components::HealthState::FAIL;
        }
        this->sohStatusOut_out(0, health, this->m_linkState);
    }

    this->tlmWrite_LinkState(this->m_linkState);
    this->tlmWrite_PendingScienceBytes(this->m_pendingScienceBytes);
    this->tlmWrite_ActiveRadioBackend(this->m_radioBackend);
    this->tlmWrite_LinkPollCount(this->m_linkPollCount);
}

void CommsManager::linkStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_linkState = key;
    this->log_ACTIVITY_LO_LinkStateUpdated(this->m_linkState);
}

void CommsManager::scienceReadyIn_handler(FwIndexType portNum, U32 productBytes) {
    static_cast<void>(portNum);
    this->m_pendingScienceBytes = productBytes;
}

void CommsManager::adapterStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_linkState = key;
    this->log_ACTIVITY_LO_LinkStateUpdated(this->m_linkState);
}

void CommsManager::payloadDownlinkStatusIn_handler(FwIndexType portNum,
                                                   U32 state,
                                                   U32 transferId,
                                                   U32 productId,
                                                   U32 totalBytes,
                                                   U32 packetsSent,
                                                   U32 totalPackets,
                                                   U32 lastError) {
    static_cast<void>(portNum);
    static_cast<void>(transferId);
    static_cast<void>(productId);
    static_cast<void>(packetsSent);
    static_cast<void>(totalPackets);

    this->m_lastPayloadDownlinkState = state;
    if (!this->m_downlinkActive) {
        return;
    }

    if (state == 2U) {
        this->log_ACTIVITY_HI_DownlinkFinished(totalBytes);
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::BASE, totalBytes);
        }
        this->m_pendingScienceBytes = 0;
        this->m_activeDownlinkBytes = 0;
        this->m_downlinkActive = false;
    } else if ((state == 3U) || (state == 4U)) {
        this->log_WARNING_LO_DownlinkFailed(state, lastError);
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::BASE, lastError);
        }
        this->m_activeDownlinkBytes = 0;
        this->m_downlinkActive = false;
    }
}

void CommsManager::REQUEST_SCIENCE_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->m_pendingScienceBytes == 0U) {
        this->log_WARNING_LO_DownlinkFailed(0U, 1U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }

    this->log_ACTIVITY_HI_DownlinkRequested(this->m_pendingScienceBytes);
    this->m_activeDownlinkBytes = this->m_pendingScienceBytes;
    this->m_downlinkActive = true;
    if (this->isConnected_missionModeOut_OutputPort(0)) {
        this->missionModeOut_out(0, Components::MissionMode::DOWNLINKING, this->m_pendingScienceBytes);
    }
    if (this->isConnected_downlinkRequestOut_OutputPort(0)) {
        this->downlinkRequestOut_out(0, this->m_pendingScienceBytes);
    }
    if (this->isConnected_payloadDownlinkRequestOut_OutputPort(0)) {
        this->payloadDownlinkRequestOut_out(0, this->m_pendingScienceBytes);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void CommsManager::REQUEST_LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_linkPollCount += 1U;
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        const U32 normalizedLink = (this->m_linkState <= 3U) ? this->m_linkState : 0U;
        this->adapterRequestOut_out(0, normalizedLink + 1U);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void CommsManager::SELECT_RADIO_BACKEND_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 backend) {
    this->m_radioBackend = (backend == 0U) ? 0U : 1U;
    this->log_ACTIVITY_HI_RadioBackendSelected(this->m_radioBackend);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
