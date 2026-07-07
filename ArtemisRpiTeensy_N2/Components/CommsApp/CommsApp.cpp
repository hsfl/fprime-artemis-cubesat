#include "Components/CommsApp/CommsApp.hpp"

namespace Components {

CommsApp::CommsApp(const char* const compName)
    : CommsAppComponentBase(compName),
      m_linkState(0),
      m_rssiDbm(-120),
      m_rssiPingPending(false),
      m_linkStatusPollPending(false),
      m_pendingProductId(0),
      m_pendingScienceBytes(0),
      m_pendingSourceKind(Components::ScienceProductSource::UNKNOWN),
      m_pendingSourcePath(""),
      m_pendingSourceCrc(0),
      m_linkPollCount(0),
      m_activeDownlinkBytes(0),
      m_lastPayloadDownlinkState(0),
      m_downlinkActive(false) {}

CommsApp::~CommsApp() {}

void CommsApp::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void CommsApp::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);

    this->requestDriverStatus();
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
    this->tlmWrite_LinkPollCount(this->m_linkPollCount);
    this->tlmWrite_RssiDbm(this->m_rssiDbm);
}

void CommsApp::linkStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    const U32 previousLinkState = this->m_linkState;
    this->m_linkState = key;
    if (this->m_linkState != previousLinkState) {
        this->log_ACTIVITY_LO_LinkStateUpdated(this->m_linkState, this->m_rssiDbm);
    }
}

void CommsApp::scienceReadyIn_handler(FwIndexType portNum,
                                          U32 productId,
                                          U32 productBytes,
                                          const Components::ScienceProductSource& sourceKind,
                                          const Fw::StringBase& sourcePath,
                                          U32 sourceCrc) {
    static_cast<void>(portNum);
    this->m_pendingProductId = productId;
    this->m_pendingScienceBytes = productBytes;
    this->m_pendingSourceKind = sourceKind;
    this->m_pendingSourcePath = sourcePath;
    this->m_pendingSourceCrc = sourceCrc;
}

void CommsApp::driverStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    const U32 previousLinkState = this->m_linkState;
    this->m_linkState = key;
    if ((this->m_linkState != previousLinkState) || this->m_linkStatusPollPending || this->m_rssiPingPending) {
        this->log_ACTIVITY_LO_LinkStateUpdated(this->m_linkState, this->m_rssiDbm);
    }
    this->m_linkStatusPollPending = false;
    if (this->m_rssiPingPending) {
        this->log_ACTIVITY_HI_LinkRssiPing(this->m_linkState, this->m_rssiDbm, this->m_linkPollCount);
        this->m_rssiPingPending = false;
    }
}

void CommsApp::rssiStatusIn_handler(FwIndexType portNum, I32 rssiDbm) {
    static_cast<void>(portNum);
    this->m_rssiDbm = rssiDbm;
}

void CommsApp::payloadDownlinkStatusIn_handler(FwIndexType portNum,
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
        this->m_pendingProductId = 0;
        this->m_pendingSourceKind = Components::ScienceProductSource::UNKNOWN;
        this->m_pendingSourcePath = "";
        this->m_pendingSourceCrc = 0;
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

void CommsApp::REQUEST_SCIENCE_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->m_pendingScienceBytes == 0U) {
        this->log_WARNING_LO_CommsCommandRejected(1U, 0U);
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
        this->downlinkRequestOut_out(0,
                                     this->m_pendingProductId,
                                     this->m_pendingScienceBytes,
                                     this->m_pendingSourceKind,
                                     this->m_pendingSourcePath,
                                     this->m_pendingSourceCrc);
    }
    if (this->isConnected_payloadDownlinkRequestOut_OutputPort(0)) {
        this->payloadDownlinkRequestOut_out(0,
                                            this->m_pendingProductId,
                                            this->m_pendingScienceBytes,
                                            this->m_pendingSourceKind,
                                            this->m_pendingSourcePath,
                                            this->m_pendingSourceCrc);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void CommsApp::REQUEST_LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_linkPollCount += 1U;
    this->m_linkStatusPollPending = true;
    this->requestDriverStatus();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void CommsApp::PING_LINK_RSSI_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_linkPollCount += 1U;
    this->m_rssiPingPending = true;
    this->requestDriverStatus();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void CommsApp::requestDriverStatus() {
    if (this->isConnected_driverRequestOut_OutputPort(0)) {
        const U32 normalizedLink = (this->m_linkState <= 3U) ? this->m_linkState : 0U;
        // Driver poll key contract: 1=down, 2=acquiring, 3=locked, 4=degraded
        this->driverRequestOut_out(0, normalizedLink + 1U);
    }
}

}  // namespace Components
