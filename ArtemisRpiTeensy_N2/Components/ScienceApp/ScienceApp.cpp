#include "Components/ScienceApp/ScienceApp.hpp"
#include "Components/Types/FppConstantsAc.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace Components {

namespace {
constexpr U32 DURATION_CONFIG_TIMEOUT_TICKS = 30U;
constexpr U32 REJECT_INVALID_DELAY_SECONDS = 1U;
constexpr U32 REJECT_INVALID_CAPTURE_DURATION_MINUTES = 2U;
constexpr U32 REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED = 3U;
constexpr U32 REJECT_DURATION_CONFIG_BUSY = 4U;
constexpr U32 REJECT_CONFIGURED_CAPTURE_PORT_MISSING = 5U;
constexpr U32 REJECT_DURATION_CONFIG_REQUIRED = 6U;
constexpr U32 VERIFIED_REJECT_ZERO_TOKEN = 1U;
constexpr U32 VERIFIED_REJECT_REUSED_TOKEN = 2U;
constexpr U32 VERIFIED_REJECT_TABLE_FULL = 3U;
constexpr U32 VERIFIED_REJECT_INVALID_DURATION = 4U;
constexpr U32 VERIFIED_REJECT_NO_REQUEST_PORT = 5U;
constexpr U32 VERIFIED_REJECT_INVALID_PRODUCT = 6U;
}  // namespace

ScienceApp::ScienceApp(const char* const compName)
    : ScienceAppComponentBase(compName),
      m_pendingDelaySeconds(0),
      m_captureDurationMinutes(DEFAULT_CAPTURE_DURATION_MINUTES),
      m_collectionCount(0),
      m_runTicks(0),
      m_lastTelemetryTick(0),
      m_nextRequestId(1),
      m_pendingDurationConfig{},
      m_verifiedCaptureCount(0),
      m_verifiedCaptures{},
      m_activeCaptureRequestId(0),
      m_activeCaptureGeneration(0),
      m_activeCaptureScienceRequested(false),
      m_pendingCommitProductId(0),
      m_downlinkIntents{} {}

ScienceApp::~ScienceApp() {}

void ScienceApp::preamble() {
    if (this->isConnected_durationConfigOut_OutputPort(0)) {
        this->m_captureDurationMinutes = 0U;
    } else {
        this->seedCaptureDurationFromParam();
    }
    this->writeTelemetry();
}

void ScienceApp::parameterUpdated(FwPrmIdType id) {
    if (id == PARAMID_CAPTURE_DURATION_MINUTES && !this->isConnected_durationConfigOut_OutputPort(0)) {
        this->seedCaptureDurationFromParam();
    }
}

void ScienceApp::seedCaptureDurationFromParam() {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const U32 durationMinutes = this->paramGet_CAPTURE_DURATION_MINUTES(valid);
    if (valid == Fw::ParamValid::UNINIT) {
        return;
    }
    if (!this->isValidCaptureDuration(durationMinutes)) {
        this->m_captureDurationMinutes = DEFAULT_CAPTURE_DURATION_MINUTES;
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_MINUTES, durationMinutes);
    } else {
        this->m_captureDurationMinutes = durationMinutes;
    }
    this->writeTelemetry();
}

void ScienceApp::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void ScienceApp::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_runTicks += 1;

    if (this->m_pendingDurationConfig.requestId != 0U &&
        this->m_runTicks - this->m_pendingDurationConfig.issuedTick >= DURATION_CONFIG_TIMEOUT_TICKS) {
        this->finishDurationConfig(Components::PayloadOperationState::TIMED_OUT);
    }

    bool countdownChanged = false;
    if (this->m_pendingDelaySeconds > 0) {
        this->m_pendingDelaySeconds -= 1;
        countdownChanged = true;
        if (this->m_pendingDelaySeconds == 0 && !this->startConfiguredCapture() &&
            this->m_activeCaptureRequestId == 0U && this->m_pendingCommitProductId == 0U &&
            this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::BASE, 0U);
        }
    }

    constexpr U32 IDLE_TELEMETRY_PERIOD_TICKS = 30U;
    if (countdownChanged || ((this->m_runTicks - this->m_lastTelemetryTick) >= IDLE_TELEMETRY_PERIOD_TICKS)) {
        this->writeTelemetry();
        this->m_lastTelemetryTick = this->m_runTicks;
    }
}

void ScienceApp::requestIn_handler(FwIndexType portNum, U32 delaySeconds) {
    static_cast<void>(portNum);
    if (delaySeconds == 0U) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_DELAY_SECONDS, delaySeconds);
        return;
    }
    this->m_pendingDelaySeconds = delaySeconds;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_CollectionTriggered(delaySeconds);
}

void ScienceApp::cancelRequestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    static_cast<void>(key);
    this->cancelPendingCollection();
}

void ScienceApp::durationConfigResultIn_handler(
    FwIndexType portNum, U32 requestId, const Components::PayloadOperationState& state
) {
    static_cast<void>(portNum);
    if (requestId != 0U && requestId == this->m_pendingDurationConfig.requestId &&
        state != Components::PayloadOperationState::ACCEPTED) {
        this->finishDurationConfig(state);
    }
}

void ScienceApp::operationNoticeIn_handler(
    FwIndexType portNum,
    U32 requestId,
    U32 generation,
    const Components::PayloadOperationKind& kind,
    const Components::PayloadOperationState& state,
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const Fw::StringBase& sourcePath,
    U32 sourceCrc,
    bool scienceRequested
) {
    static_cast<void>(portNum);
    if (kind != Components::PayloadOperationKind::CAPTURE) {
        return;
    }

    VerifiedCaptureRecord* const verified = this->findVerifiedCaptureByRequestId(requestId);

    // A request ID is only a useful token mapping when this is the science
    // command's own scienceRequested operation. Ignore unrelated/stale notices
    // that happen to reuse the request ID; in particular, a generation-zero
    // BUSY rejection must not terminalize an already admitted request.
    if ((verified != nullptr) &&
        (verified->terminal || !scienceRequested ||
         ((verified->generation != 0U) && (verified->generation != generation)))) {
        return;
    }

    if (state == Components::PayloadOperationState::ACCEPTED) {
        if ((this->m_activeCaptureRequestId != 0U) &&
            ((this->m_activeCaptureRequestId != requestId) ||
             (this->m_activeCaptureGeneration != generation) ||
             (this->m_activeCaptureScienceRequested != scienceRequested))) {
            return;
        }
        this->m_activeCaptureRequestId = requestId;
        this->m_activeCaptureGeneration = generation;
        this->m_activeCaptureScienceRequested = scienceRequested;
        // A result from the previous collection must not change this mode.
        this->m_pendingCommitProductId = 0U;
        if ((verified != nullptr) && (verified->generation == 0U) && scienceRequested) {
            verified->generation = generation;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        if (scienceRequested) {
            this->setDownlinkIntentGeneration(requestId, generation);
        }
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::COLLECTING, this->m_captureDurationMinutes);
        }
        return;
    }

    if ((generation == 0U) &&
        ((state == Components::PayloadOperationState::BUSY) ||
         (state == Components::PayloadOperationState::INVALID) ||
         (state == Components::PayloadOperationState::DRIVER_UNAVAILABLE) ||
         (state == Components::PayloadOperationState::FAILED) ||
         (state == Components::PayloadOperationState::TIMED_OUT))) {
        this->log_WARNING_HI_ScienceOperationRejected(requestId, state);
        if ((verified != nullptr) && (verified->generation == 0U) && scienceRequested) {
            verified->terminal = true;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        if (scienceRequested) {
            this->clearDownlinkIntent(requestId, generation);
        }
        if ((this->m_activeCaptureRequestId == 0U) && (this->m_pendingCommitProductId == 0U) &&
            this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::BASE, 0U);
        }
        return;
    }

    const bool requestMatches = (this->m_activeCaptureRequestId == requestId);
    const bool generationMatches = (generation == this->m_activeCaptureGeneration);
    const bool originMatches = (scienceRequested == this->m_activeCaptureScienceRequested);
    if (!requestMatches || !generationMatches || !originMatches) {
        return;
    }

    if (state == Components::PayloadOperationState::COMPLETED) {
        if (!this->isValidProduct(productId, productBytes, sourceKind, sourcePath, sourceCrc)) {
            this->log_WARNING_HI_ScienceOperationRejected(requestId, Components::PayloadOperationState::FAILED);
            if ((verified != nullptr) && !verified->terminal) {
                verified->terminal = true;
                this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation,
                                               Components::PayloadOperationState::FAILED, productId,
                                               productBytes, sourceCrc, VERIFIED_REJECT_INVALID_PRODUCT);
            }
            this->m_activeCaptureRequestId = 0U;
            this->m_activeCaptureGeneration = 0U;
            if (scienceRequested) {
                this->clearDownlinkIntent(requestId, generation);
            }
            this->m_activeCaptureScienceRequested = false;
            if (this->isConnected_missionModeOut_OutputPort(0)) {
                this->missionModeOut_out(0, Components::MissionMode::BASE, 0U);
            }
            return;
        }
        if ((verified != nullptr) && !verified->terminal) {
            verified->terminal = true;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        if (this->isConnected_scienceProductOut_OutputPort(0)) {
            // This actor initiates the storage write after correlating the
            // capture result. Arm the commit match before submitting it.
            this->m_pendingCommitProductId = productId;
            if (scienceRequested) {
                this->setDownlinkIntentProduct(requestId, generation, productId);
            }
            this->publishScienceProduct(productId, productBytes, sourceKind, sourcePath, sourceCrc);
        } else {
            if (scienceRequested) {
                this->clearDownlinkIntent(requestId, generation);
            }
            if (this->isConnected_missionModeOut_OutputPort(0)) {
                this->missionModeOut_out(0, Components::MissionMode::BASE, 0U);
            }
        }
        this->m_activeCaptureRequestId = 0U;
        this->m_activeCaptureGeneration = 0U;
        this->m_activeCaptureScienceRequested = false;
        return;
    }

    if ((state == Components::PayloadOperationState::BUSY) ||
        (state == Components::PayloadOperationState::INVALID) ||
        (state == Components::PayloadOperationState::DRIVER_UNAVAILABLE) ||
        (state == Components::PayloadOperationState::FAILED) ||
        (state == Components::PayloadOperationState::TIMED_OUT)) {
        this->log_WARNING_HI_ScienceOperationRejected(requestId, state);
        if ((verified != nullptr) && !verified->terminal) {
            verified->terminal = true;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        if (scienceRequested) {
            this->clearDownlinkIntent(requestId, generation);
        }
        this->m_activeCaptureRequestId = 0U;
        this->m_activeCaptureGeneration = 0U;
        this->m_activeCaptureScienceRequested = false;
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::BASE, 0U);
        }
    }
}

void ScienceApp::productCommitIn_handler(FwIndexType portNum, U32 productId, U32 productBytes, bool committed) {
    static_cast<void>(portNum);
    if (productId == 0U) {
        return;
    }
    if (productId == this->m_pendingCommitProductId) {
        this->m_pendingCommitProductId = 0U;
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            const auto mode = committed ? Components::MissionMode::SCIENCE_READY : Components::MissionMode::BASE;
            this->missionModeOut_out(0, mode, committed ? productBytes : 0U);
        }
    }
    for (DownlinkIntent& intent : this->m_downlinkIntents) {
        if (intent.productId == productId) {
            intent = {};
            if (committed && this->isConnected_deliveryRequestOut_OutputPort(0)) {
                // Queue the mode update before allowing CommsApp to start delivery.
                this->deliveryRequestOut_out(0, productId);
            }
            return;
        }
    }
}

void ScienceApp::START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    if (!this->isConnected_durationConfigOut_OutputPort(0) &&
        !this->isValidCaptureDuration(this->m_captureDurationMinutes)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_MINUTES,
                                                    this->m_captureDurationMinutes);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    const bool submitted = this->startConfiguredCapture();
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void ScienceApp::CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationMinutes) {
    if (!this->isValidCaptureDuration(durationMinutes)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_MINUTES, durationMinutes);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (this->isConnected_durationConfigOut_OutputPort(0)) {
        if (this->m_pendingDurationConfig.requestId != 0U) {
            this->log_WARNING_LO_ScienceCommandRejected(REJECT_DURATION_CONFIG_BUSY, durationMinutes);
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
            return;
        }
        const U32 requestId = this->allocateRequestId();
        this->m_pendingDurationConfig = {requestId, durationMinutes, opCode, cmdSeq, this->m_runTicks};
        this->durationConfigOut_out(0, requestId, durationMinutes);
        return;
    }
    if (this->isConnected_configuredCaptureOut_OutputPort(0)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_CONFIGURED_CAPTURE_PORT_MISSING, durationMinutes);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->applyCaptureDuration(durationMinutes);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

#ifdef NEUTRON2_CONFIGURED_CAPTURE
void ScienceApp::SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->START_COLLECTION_cmdHandler(opCode, cmdSeq);
}
#else
void ScienceApp::SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationMinutes) {
    if (!this->isValidCaptureDuration(durationMinutes)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_MINUTES, durationMinutes);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    const bool submitted = this->startCapture(durationMinutes);
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}
#endif

void ScienceApp::COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationMinutes) {
#ifdef NEUTRON2_CDR_TCP
    this->log_WARNING_LO_ScienceCommandRejected(REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED, opCode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
    return;
#endif
    if (!this->isValidCaptureDuration(durationMinutes)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_MINUTES, durationMinutes);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    const bool submitted = this->startCapture(durationMinutes, true);
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void ScienceApp::VERIFIED_COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq,
                                                           U32 verificationToken, U32 durationMinutes) {
#ifdef NEUTRON2_CDR_TCP
    this->log_WARNING_LO_ScienceCommandRejected(REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED, opCode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
    return;
#endif
    if (verificationToken == 0U) {
        this->logVerifiedCaptureStatus(verificationToken, 0U, 0U,
                                       Components::PayloadOperationState::INVALID, 0U, 0U, 0U,
                                       VERIFIED_REJECT_ZERO_TOKEN);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (this->findVerifiedCaptureByToken(verificationToken) != nullptr) {
        this->logVerifiedCaptureStatus(verificationToken, 0U, 0U,
                                       Components::PayloadOperationState::INVALID, 0U, 0U, 0U,
                                       VERIFIED_REJECT_REUSED_TOKEN);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (this->m_verifiedCaptureCount >= VERIFIED_CAPTURE_CAPACITY) {
        this->logVerifiedCaptureStatus(verificationToken, 0U, 0U,
                                       Components::PayloadOperationState::INVALID, 0U, 0U, 0U,
                                       VERIFIED_REJECT_TABLE_FULL);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    VerifiedCaptureRecord& record = this->m_verifiedCaptures[this->m_verifiedCaptureCount++];
    record = VerifiedCaptureRecord{verificationToken, 0U, 0U, false};
    if (!this->isValidCaptureDuration(durationMinutes)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_MINUTES, durationMinutes);
        record.terminal = true;
        this->logVerifiedCaptureStatus(verificationToken, 0U, 0U,
                                       Components::PayloadOperationState::INVALID, 0U, 0U, 0U,
                                       VERIFIED_REJECT_INVALID_DURATION);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }

    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    U32 requestId = 0U;
    const bool submitted = this->startCapture(durationMinutes, true, &requestId);
    record.requestId = requestId;
    if (!submitted) {
        record.terminal = true;
        this->logVerifiedCaptureStatus(verificationToken, requestId, 0U,
                                       Components::PayloadOperationState::DRIVER_UNAVAILABLE, 0U, 0U, 0U,
                                       VERIFIED_REJECT_NO_REQUEST_PORT);
    }
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

bool ScienceApp::isValidCaptureDuration(U32 durationMinutes) {
    const U32 maximum = this->isConnected_durationConfigOut_OutputPort(0)
        ? MAX_CAPTURE_DURATION_MINUTES : MAX_SIMULATED_CAPTURE_DURATION_MINUTES;
    return (durationMinutes > 0U) && (durationMinutes <= maximum);
}

void ScienceApp::applyCaptureDuration(U32 durationMinutes) {
    this->m_captureDurationMinutes = durationMinutes;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_CaptureDurationConfigured(durationMinutes);
}

void ScienceApp::finishDurationConfig(const Components::PayloadOperationState& state) {
    const PendingDurationConfig pending = this->m_pendingDurationConfig;
    this->m_pendingDurationConfig = {};
    Fw::CmdResponse response = Fw::CmdResponse::EXECUTION_ERROR;
    if (state == Components::PayloadOperationState::COMPLETED) {
        this->applyCaptureDuration(pending.durationMinutes);
        response = Fw::CmdResponse::OK;
    } else {
        this->m_captureDurationMinutes = 0U;
        this->writeTelemetry();
        this->log_WARNING_HI_ScienceOperationRejected(pending.requestId, state);
        if (state == Components::PayloadOperationState::INVALID) {
            response = Fw::CmdResponse::VALIDATION_ERROR;
        }
    }
    this->cmdResponse_out(pending.opCode, pending.cmdSeq, response);
}

void ScienceApp::cancelPendingCollection() {
    const U32 remainingSeconds = this->m_pendingDelaySeconds;
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_CollectionCancelled(remainingSeconds);
}

U32 ScienceApp::allocateRequestId() {
    const U32 requestId = this->m_nextRequestId++;
    if (this->m_nextRequestId == 0U) {
        this->m_nextRequestId = 1U;
    }
    return requestId;
}

bool ScienceApp::startConfiguredCapture() {
    const bool configConnected = this->isConnected_durationConfigOut_OutputPort(0);
    const bool captureConnected = this->isConnected_configuredCaptureOut_OutputPort(0);
    if (!configConnected && !captureConnected) {
        return this->startCapture(this->m_captureDurationMinutes);
    }
    if (!configConnected || !captureConnected) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_CONFIGURED_CAPTURE_PORT_MISSING, 0U);
        return false;
    }
    if (this->m_pendingDurationConfig.requestId != 0U) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_DURATION_CONFIG_BUSY,
                                                    this->m_pendingDurationConfig.durationMinutes);
        return false;
    }
    if (!this->isValidCaptureDuration(this->m_captureDurationMinutes)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_DURATION_CONFIG_REQUIRED, 0U);
        return false;
    }
    this->configuredCaptureOut_out(0, this->allocateRequestId());
    return true;
}

bool ScienceApp::startCapture(U32 durationMinutes, bool requestDownlink, U32* requestIdOut) {
    const U32 requestId = this->allocateRequestId();
    if (requestIdOut != nullptr) {
        *requestIdOut = requestId;
    }
    if (!this->isConnected_payloadRequestOut_OutputPort(0)) {
        this->log_WARNING_HI_ScienceOperationRejected(requestId, Components::PayloadOperationState::DRIVER_UNAVAILABLE);
        return false;
    }
    if (requestDownlink && !this->rememberDownlinkIntent(requestId)) {
        this->log_WARNING_HI_ScienceOperationRejected(requestId, Components::PayloadOperationState::BUSY);
        return false;
    }
    this->payloadRequestOut_out(0, requestId, durationMinutes);
    return true;
}

ScienceApp::VerifiedCaptureRecord* ScienceApp::findVerifiedCaptureByToken(U32 verificationToken) {
    for (U32 index = 0U; index < this->m_verifiedCaptureCount; ++index) {
        if (this->m_verifiedCaptures[index].verificationToken == verificationToken) {
            return &this->m_verifiedCaptures[index];
        }
    }
    return nullptr;
}

ScienceApp::VerifiedCaptureRecord* ScienceApp::findVerifiedCaptureByRequestId(U32 requestId) {
    for (U32 index = 0U; index < this->m_verifiedCaptureCount; ++index) {
        if ((requestId != 0U) && (this->m_verifiedCaptures[index].requestId == requestId)) {
            return &this->m_verifiedCaptures[index];
        }
    }
    return nullptr;
}

void ScienceApp::logVerifiedCaptureStatus(U32 verificationToken, U32 requestId, U32 generation,
                                          const Components::PayloadOperationState& state, U32 productId,
                                          U32 productBytes, U32 sourceCrc, U32 reasonCode) {
    this->log_ACTIVITY_HI_VerifiedCaptureStatus(verificationToken, requestId, generation, state,
                                                productId, productBytes, sourceCrc, reasonCode);
}

bool ScienceApp::rememberDownlinkIntent(U32 requestId) {
    for (DownlinkIntent& intent : this->m_downlinkIntents) {
        if (intent.requestId == 0U) {
            intent = {requestId, 0U, 0U};
            return true;
        }
    }
    return false;
}

void ScienceApp::setDownlinkIntentGeneration(U32 requestId, U32 generation) {
    for (DownlinkIntent& intent : this->m_downlinkIntents) {
        if ((intent.requestId == requestId) && (intent.generation == 0U)) {
            intent.generation = generation;
            return;
        }
    }
}

void ScienceApp::setDownlinkIntentProduct(U32 requestId, U32 generation, U32 productId) {
    for (DownlinkIntent& intent : this->m_downlinkIntents) {
        if ((intent.requestId == requestId) && (intent.generation == generation)) {
            intent.productId = productId;
            return;
        }
    }
}

void ScienceApp::clearDownlinkIntent(U32 requestId, U32 generation) {
    for (DownlinkIntent& intent : this->m_downlinkIntents) {
        if ((intent.requestId == requestId) && (intent.generation == generation)) {
            intent = {};
            return;
        }
    }
}

void ScienceApp::publishScienceProduct(
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const Fw::StringBase& sourcePath,
    U32 sourceCrc
) {
    this->m_collectionCount += 1U;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_ScienceProductReady(productBytes);
    this->scienceProductOut_out(0, productId, productBytes, sourceKind, sourcePath, sourceCrc);
}

bool ScienceApp::isValidProduct(
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const Fw::StringBase& sourcePath,
    U32 sourceCrc
) const {
    if ((productId == 0U) || (productBytes == 0U) ||
        (sourceKind == Components::ScienceProductSource::UNKNOWN) || (sourcePath.length() == 0U)) {
        return false;
    }
    struct stat info {};
    if ((::stat(sourcePath.toChar(), &info) != 0) || !S_ISREG(info.st_mode) ||
        (info.st_size <= 0) || (static_cast<U64>(info.st_size) != productBytes)) {
        return false;
    }
    std::FILE* file = std::fopen(sourcePath.toChar(), "rb");
    if (file == nullptr) {
        return false;
    }
    U16 crc = 0xFFFFU;
    int value = 0;
    while ((value = std::fgetc(file)) != EOF) {
        crc ^= static_cast<U16>(static_cast<U8>(value)) << 8U;
        for (U8 bit = 0; bit < 8U; bit++) {
            crc = ((crc & 0x8000U) != 0U)
                ? static_cast<U16>((crc << 1U) ^ 0x1021U)
                : static_cast<U16>(crc << 1U);
        }
    }
    const bool readOk = !std::ferror(file);
    (void)std::fclose(file);
    return readOk && (static_cast<U32>(crc) == sourceCrc);
}

void ScienceApp::writeTelemetry() {
    this->tlmWrite_PendingDelaySeconds(this->m_pendingDelaySeconds);
    this->tlmWrite_CaptureDurationMinutes(this->m_captureDurationMinutes);
    this->tlmWrite_CollectionCount(this->m_collectionCount);
}

}  // namespace Components
