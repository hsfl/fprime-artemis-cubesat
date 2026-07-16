#include "Components/CommsApp/CommsApp.hpp"

namespace Components {

CommsApp::CommsApp(const char* const compName)
    : CommsAppComponentBase(compName),
      m_desiredRadioEnabled(true),
      m_radioStatusKnown(false),
      m_driverRequestPending(false),
      m_pendingRadioOperation(Components::RadioOperation::STATUS),
      m_radioState(Components::RadioState::OFF),
      m_radioFault(Components::RadioFault::NONE),
      m_radioRpcResult(Components::RadioRpcResult::NOT_CONFIGURED),
      m_radioBootFlags(0),
      m_rssiValid(0),
      m_rssiDbm(0),
      m_rssiAgeMs(0xFFFFFFFFU),
      m_radioInitAttempts(0),
      m_rfRxPackets(0),
      m_rfTxPackets(0),
      m_rfTxDrops(0),
      m_radioRecoveryFailures(0),
      m_radioRetryTicks(0),
      m_readyStatusPollTicks(0),
      m_rssiPingPending(false),
      m_linkStatusPollPending(false),
      m_pendingProductId(0),
      m_pendingScienceBytes(0),
      m_pendingSourceKind(Components::ScienceProductSource::UNKNOWN),
      m_pendingSourcePath(""),
      m_pendingSourceCrc(0),
      m_linkPollCount(0),
      m_activeDownlinkBytes(0),
      m_activeProductId(0),
      m_activeSourceKind(Components::ScienceProductSource::UNKNOWN),
      m_activeSourcePath(""),
      m_activeSourceCrc(0),
      m_activeTransferId(0),
      m_downlinkRequestDisposition(0),
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

    this->emitRadioHealth();
    if (this->m_driverRequestPending) {
        return;
    }

    if (this->m_radioRetryTicks > 0U) {
        this->m_radioRetryTicks -= 1U;
        if (this->m_radioRetryTicks == 0U) {
            this->tlmWrite_RadioRetrySeconds(0U);
            (void)this->requestDriver(Components::RadioOperation::SET_ENABLED, 1U);
        } else if ((this->m_radioRetryTicks % RETRY_COUNTDOWN_TLM_TICKS) == 0U) {
            this->tlmWrite_RadioRetrySeconds(this->m_radioRetryTicks * POLICY_TICK_SECONDS);
        }
        return;
    }

    if (!this->m_radioStatusKnown) {
        (void)this->requestDriver(Components::RadioOperation::STATUS, 0U);
        return;
    }

    if (this->m_desiredRadioEnabled && this->m_radioState == Components::RadioState::OFF) {
        (void)this->requestDriver(Components::RadioOperation::SET_ENABLED, 1U);
        return;
    }

    if (this->m_radioState == Components::RadioState::READY) {
        this->m_readyStatusPollTicks += 1U;
        if (this->m_readyStatusPollTicks >= READY_STATUS_POLL_TICKS) {
            this->m_readyStatusPollTicks = 0U;
            (void)this->requestDriver(Components::RadioOperation::STATUS, 0U);
        }
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
    this->tlmWrite_PendingScienceBytes(this->m_pendingScienceBytes);
}

void CommsApp::driverStatusIn_handler(FwIndexType portNum,
                                      const Components::RadioOperation& operation,
                                      const Components::RadioRpcResult& result,
                                      const Components::RadioState& state,
                                      const Components::RadioFault& fault,
                                      U8 bootFlags,
                                      U8 rssiValid,
                                      I32 rssiDbm,
                                      U32 rssiAgeMs,
                                      U32 initAttempts,
                                      U32 rfRxPackets,
                                      U32 rfTxPackets,
                                      U32 rfTxDrops) {
    static_cast<void>(portNum);
    if (!this->m_driverRequestPending || operation != this->m_pendingRadioOperation) {
        return;
    }
    this->m_driverRequestPending = false;

    const bool factualStatus =
        result == Components::RadioRpcResult::OK || result == Components::RadioRpcResult::TARGET_ERROR;
    if (factualStatus) {
        this->m_radioStatusKnown = true;
        this->m_radioState = state;
        this->m_radioFault = fault;
        this->m_radioBootFlags = bootFlags;
        this->m_rssiValid = rssiValid;
        this->m_rssiDbm = rssiDbm;
        this->m_rssiAgeMs = rssiAgeMs;
        this->m_radioInitAttempts = initAttempts;
        this->m_rfRxPackets = rfRxPackets;
        this->m_rfTxPackets = rfTxPackets;
        this->m_rfTxDrops = rfTxDrops;
    }
    this->m_radioRpcResult = result;

    if (factualStatus || this->m_linkStatusPollPending || this->m_rssiPingPending) {
        this->log_ACTIVITY_LO_RadioStatusUpdated(this->m_radioState, this->m_radioFault, result);
    }
    this->m_linkStatusPollPending = false;
    if (this->m_rssiPingPending) {
        this->log_ACTIVITY_HI_LinkRssiPing(this->m_radioState,
                                          this->m_rssiValid,
                                          this->m_rssiDbm,
                                          this->m_rssiAgeMs,
                                          this->m_linkPollCount);
        this->m_rssiPingPending = false;
    }
    this->emitRadioTelemetry();
    this->emitRadioHealth();

    if (!this->m_desiredRadioEnabled) {
        return;
    }
    const bool healthyReady = factualStatus && this->m_radioState == Components::RadioState::READY &&
                              this->m_radioFault == Components::RadioFault::NONE;
    if (healthyReady) {
        if (this->m_radioRecoveryFailures > 0U) {
            this->log_ACTIVITY_HI_RadioRecovered(this->m_radioRecoveryFailures);
        }
        this->m_radioRecoveryFailures = 0U;
        this->m_radioRetryTicks = 0U;
        this->m_readyStatusPollTicks = 0U;
        this->emitRadioTelemetry();
        return;
    }
    if (operation == Components::RadioOperation::STATUS) {
        // A status command issued while a retry is already scheduled is
        // observational only. It must not advance or restart the backoff.
        if (factualStatus &&
            (this->m_radioState == Components::RadioState::OFF ||
             this->m_radioFault != Components::RadioFault::NONE) &&
            this->m_radioRecoveryFailures == 0U && this->m_radioRetryTicks == 0U) {
            (void)this->requestDriver(Components::RadioOperation::SET_ENABLED, 1U);
        } else if (!factualStatus && this->m_radioRecoveryFailures == 0U &&
                   this->m_radioRetryTicks == 0U) {
            this->scheduleRecovery();
        }
        return;
    }
    // Only a failed/non-ready SET_ENABLED attempt advances the capped retry
    // schedule. RF silence and informational STATUS responses never do.
    this->scheduleRecovery();
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
    static_cast<void>(packetsSent);
    static_cast<void>(totalPackets);

    if (!this->m_downlinkActive) {
        return;
    }

    if ((state < 1U) || (state > 4U) || (transferId == 0U) ||
        (productId != this->m_activeProductId) || (totalBytes != this->m_activeDownlinkBytes)) {
        this->log_WARNING_LO_PayloadDownlinkStatusIgnored(this->m_activeTransferId, transferId, productId);
        return;
    }
    if (this->m_activeTransferId == 0U) {
        // A terminal packet cannot establish ownership of a transfer. It may be
        // delayed status from an earlier transfer that reused the product ID.
        if (state != 1U) {
            this->log_WARNING_LO_PayloadDownlinkStatusIgnored(this->m_activeTransferId, transferId, productId);
            return;
        }
        this->m_activeTransferId = transferId;
    } else if (transferId != this->m_activeTransferId) {
        this->log_WARNING_LO_PayloadDownlinkStatusIgnored(this->m_activeTransferId, transferId, productId);
        return;
    }
    this->m_lastPayloadDownlinkState = state;

    if (state == 2U) {
        this->log_ACTIVITY_HI_DownlinkFinished(totalBytes);
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::BASE, totalBytes);
        }
        // Keep the latest science descriptor available after a successful
        // transmission. Ground-only receiver cancellation is intentionally
        // invisible to flight, so the operator must be able to request the
        // same product again until a newer capture replaces this descriptor.
        this->clearActiveDownlink();
    } else if ((state == 3U) || (state == 4U)) {
        this->log_WARNING_LO_DownlinkFailed(state, lastError);
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::BASE, lastError);
        }
        this->clearActiveDownlink();
    }
    this->emitDownlinkTelemetry();
}

void CommsApp::REQUEST_SCIENCE_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->m_downlinkActive) {
        if (this->pendingRequestMatchesActive()) {
            this->m_downlinkRequestDisposition = 1U;
            this->log_ACTIVITY_LO_DownlinkRequestDuplicate(this->m_activeProductId, this->m_activeDownlinkBytes);
            this->emitDownlinkTelemetry();
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
            return;
        }
        this->m_downlinkRequestDisposition = 2U;
        this->log_WARNING_LO_DownlinkRequestConflict(this->m_activeProductId, this->m_pendingProductId);
        this->log_WARNING_LO_CommsCommandRejected(2U, this->m_pendingProductId);
        this->emitDownlinkTelemetry();
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
        return;
    }

    if (this->m_pendingScienceBytes == 0U) {
        this->log_WARNING_LO_CommsCommandRejected(1U, 0U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }

    this->log_ACTIVITY_HI_DownlinkRequested(this->m_pendingScienceBytes);
    this->m_activeDownlinkBytes = this->m_pendingScienceBytes;
    this->m_activeProductId = this->m_pendingProductId;
    this->m_activeSourceKind = this->m_pendingSourceKind;
    this->m_activeSourcePath = this->m_pendingSourcePath;
    this->m_activeSourceCrc = this->m_pendingSourceCrc;
    this->m_activeTransferId = 0U;
    this->m_downlinkRequestDisposition = 0U;
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
    this->emitDownlinkTelemetry();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void CommsApp::REQUEST_LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->m_driverRequestPending) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
        return;
    }
    this->m_linkPollCount += 1U;
    this->m_linkStatusPollPending = true;
    const bool requested = this->requestDriver(Components::RadioOperation::STATUS, 0U);
    this->tlmWrite_LinkPollCount(this->m_linkPollCount);
    this->cmdResponse_out(opCode, cmdSeq, requested ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void CommsApp::PING_LINK_RSSI_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (this->m_driverRequestPending) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
        return;
    }
    this->m_linkPollCount += 1U;
    this->m_rssiPingPending = true;
    const bool requested = this->requestDriver(Components::RadioOperation::STATUS, 0U);
    this->tlmWrite_LinkPollCount(this->m_linkPollCount);
    this->cmdResponse_out(opCode, cmdSeq, requested ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

bool CommsApp::requestDriver(const Components::RadioOperation& operation, U8 enabled) {
    if (this->m_driverRequestPending || !this->isConnected_driverRequestOut_OutputPort(0)) {
        return false;
    }
    this->m_driverRequestPending = true;
    this->m_pendingRadioOperation = operation;
    this->driverRequestOut_out(0, operation, enabled);
    return true;
}

void CommsApp::scheduleRecovery() {
    static constexpr U32 RETRY_TICKS[] = {15U, 60U, 450U};
    this->m_radioRecoveryFailures += 1U;
    U32 retryIndex = this->m_radioRecoveryFailures - 1U;
    if (retryIndex >= FW_NUM_ARRAY_ELEMENTS(RETRY_TICKS)) {
        retryIndex = FW_NUM_ARRAY_ELEMENTS(RETRY_TICKS) - 1U;
    }
    this->m_radioRetryTicks = RETRY_TICKS[retryIndex];
    this->m_readyStatusPollTicks = 0U;
    const U32 retrySeconds = this->m_radioRetryTicks * POLICY_TICK_SECONDS;
    this->log_WARNING_LO_RadioRecoveryScheduled(this->m_radioRecoveryFailures,
                                                retrySeconds,
                                                this->m_radioFault,
                                                this->m_radioRpcResult);
    this->emitRadioTelemetry();
}

void CommsApp::emitRadioTelemetry() {
    this->tlmWrite_DesiredRadioEnabled(this->m_desiredRadioEnabled ? 1U : 0U);
    this->tlmWrite_RadioStatusKnown(this->m_radioStatusKnown ? 1U : 0U);
    this->tlmWrite_RadioState(this->m_radioState);
    this->tlmWrite_RadioFault(this->m_radioFault);
    this->tlmWrite_RadioRpcResult(this->m_radioRpcResult);
    this->tlmWrite_RadioRecoveryFailures(this->m_radioRecoveryFailures);
    this->tlmWrite_RadioRetrySeconds(this->m_radioRetryTicks * POLICY_TICK_SECONDS);
    this->tlmWrite_RadioInitAttempts(this->m_radioInitAttempts);
    this->tlmWrite_RssiValid(this->m_rssiValid);
    this->tlmWrite_RssiDbm(this->m_rssiDbm);
    this->tlmWrite_RssiAgeMs(this->m_rssiAgeMs);
}

void CommsApp::emitRadioHealth() {
    if (!this->isConnected_sohStatusOut_OutputPort(0)) {
        return;
    }
    Components::HealthState health = Components::HealthState::UNKNOWN;
    if (this->m_radioStatusKnown && this->m_radioState == Components::RadioState::READY &&
        this->m_radioFault == Components::RadioFault::NONE) {
        health = Components::HealthState::OK;
    } else if (this->m_radioStatusKnown || this->m_radioRecoveryFailures > 0U) {
        // A radio-local fault degrades comms but must not imply that the Pi or
        // the responsive F Prime component should be reset.
        health = Components::HealthState::WARN;
    }
    const U32 detail = (static_cast<U32>(this->m_radioState.e) << 8U) |
                       static_cast<U32>(this->m_radioFault.e);
    this->sohStatusOut_out(0, health, detail);
}

bool CommsApp::pendingRequestMatchesActive() const {
    return (this->m_pendingProductId == this->m_activeProductId) &&
           (this->m_pendingScienceBytes == this->m_activeDownlinkBytes) &&
           (this->m_pendingSourceKind == this->m_activeSourceKind) &&
           (this->m_pendingSourcePath == this->m_activeSourcePath) &&
           (this->m_pendingSourceCrc == this->m_activeSourceCrc);
}

void CommsApp::clearActiveDownlink() {
    this->m_activeDownlinkBytes = 0U;
    this->m_activeProductId = 0U;
    this->m_activeSourceKind = Components::ScienceProductSource::UNKNOWN;
    this->m_activeSourcePath = "";
    this->m_activeSourceCrc = 0U;
    this->m_activeTransferId = 0U;
    this->m_downlinkActive = false;
}

void CommsApp::emitDownlinkTelemetry() {
    this->tlmWrite_DownlinkActive(this->m_downlinkActive ? 1U : 0U);
    this->tlmWrite_ActiveDownlinkProductId(this->m_activeProductId);
    this->tlmWrite_ActiveDownlinkTransferId(this->m_activeTransferId);
    this->tlmWrite_DownlinkRequestDisposition(this->m_downlinkRequestDisposition);
}

}  // namespace Components
