#include "Components/ScienceApp/ScienceApp.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace Components {

namespace {
constexpr U32 DEFAULT_CAPTURE_DURATION_SECONDS = 30U;
constexpr U32 MAX_CAPTURE_DURATION_SECONDS = 120U;
constexpr U32 REJECT_INVALID_DELAY_SECONDS = 1U;
constexpr U32 REJECT_INVALID_CAPTURE_DURATION_SECONDS = 2U;
constexpr U32 REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED = 3U;
constexpr U32 VERIFIED_REJECT_ZERO_TOKEN = 1U;
constexpr U32 VERIFIED_REJECT_REUSED_TOKEN = 2U;
constexpr U32 VERIFIED_REJECT_TABLE_FULL = 3U;
constexpr U32 VERIFIED_REJECT_INVALID_DURATION = 4U;
constexpr U32 VERIFIED_REJECT_NO_REQUEST_PORT = 5U;
constexpr U32 VERIFIED_REJECT_INVALID_PRODUCT = 6U;
// SAFE inhibits science (SDD v4.0 Table 5): the command arrived in SAFE (7), or the capture ran
// during SAFE and its product was withheld (8).
constexpr U32 VERIFIED_REJECT_SAFE_MODE = 7U;
constexpr U32 VERIFIED_REJECT_SAFE_INTERRUPTED = 8U;
}  // namespace

ScienceApp::ScienceApp(const char* const compName)
    : ScienceAppComponentBase(compName),
      m_pendingDelaySeconds(0),
      m_captureDurationSeconds(DEFAULT_CAPTURE_DURATION_SECONDS),
      m_collectionCount(0),
      m_runTicks(0),
      m_lastTelemetryTick(0),
      m_nextRequestId(1),
      m_verifiedCaptureCount(0),
      m_verifiedCaptures{},
      m_activeCaptureRequestId(0),
      m_activeCaptureGeneration(0),
      m_activeCaptureScienceRequested(false),
      m_activeCaptureInterrupted(false),
      m_downlinkRequestIds{},
      m_downlinkRequestGenerations{},
      m_scienceInhibited(false) {}

ScienceApp::~ScienceApp() {}

void ScienceApp::preamble() {
    this->seedCaptureDurationFromParam();
    this->writeTelemetry();
}

void ScienceApp::parameterUpdated(FwPrmIdType id) {
    if (id == PARAMID_CAPTURE_DURATION_SECONDS) {
        this->seedCaptureDurationFromParam();
    }
}

void ScienceApp::seedCaptureDurationFromParam() {
    Fw::ParamValid valid = Fw::ParamValid::INVALID;
    const U32 durationSeconds = this->paramGet_CAPTURE_DURATION_SECONDS(valid);
    if (valid == Fw::ParamValid::UNINIT) {
        return;
    }
    if (!this->isValidCaptureDuration(durationSeconds)) {
        this->m_captureDurationSeconds = DEFAULT_CAPTURE_DURATION_SECONDS;
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS, durationSeconds);
    } else {
        this->m_captureDurationSeconds = durationSeconds;
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

    bool countdownChanged = false;
    if (this->m_pendingDelaySeconds > 0) {
        this->m_pendingDelaySeconds -= 1;
        countdownChanged = true;
        if (this->m_pendingDelaySeconds == 0) {
            this->startCapture(this->m_captureDurationSeconds, Components::ScienceInhibitSource::SCHEDULED_COUNTDOWN);
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
    // MissionApp already refuses SCHEDULE_COLLECTION in SAFE. This keeps ScienceApp safe on its own.
    if (!this->scienceAllowed(Components::ScienceInhibitSource::SCHEDULED_REQUEST)) {
        return;
    }
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
        if ((verified != nullptr) && (verified->generation == 0U) && scienceRequested) {
            verified->generation = generation;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        if (scienceRequested) {
            this->setDownlinkIntentGeneration(requestId, generation);
        }
        if (this->m_scienceInhibited) {
            // Accepted during SAFE: PayloadManager's own capture command started it, or a request sent
            // just before SAFE was accepted after. It cannot be stopped from here, so its product is withheld.
            this->m_activeCaptureInterrupted = true;
            this->log_WARNING_HI_CaptureRunningInSafe(requestId);
            return;
        }
        this->reportMissionMode(Components::MissionMode::COLLECTING, this->m_captureDurationSeconds);
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
        if (this->m_activeCaptureRequestId == 0U) {
            this->reportMissionMode(Components::MissionMode::BASE, 0U);
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
        if (this->m_activeCaptureInterrupted) {
            // The capture ran during SAFE, where science is inhibited (SDD v4.0 Table 5), so its product is
            // withheld even if SAFE has ended since: not handed to StorageManager, not reported as
            // SCIENCE_READY, not delivered. The attempt stays partial (SDD v4.0 §5.2) and the payload's
            // file stays where the payload wrote it, for the ground to decide on (OD-04).
            this->log_WARNING_HI_CaptureWithheld(requestId, productId, productBytes);
            if ((verified != nullptr) && !verified->terminal) {
                verified->terminal = true;
                this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation,
                                               Components::PayloadOperationState::FAILED, productId,
                                               productBytes, sourceCrc, VERIFIED_REJECT_SAFE_INTERRUPTED);
            }
            if (scienceRequested) {
                this->clearDownlinkIntent(requestId, generation);
            }
            this->clearActiveCapture();
            return;
        }
        if (!this->isValidProduct(productId, productBytes, sourceKind, sourcePath, sourceCrc)) {
            this->log_WARNING_HI_ScienceOperationRejected(requestId, Components::PayloadOperationState::FAILED);
            if ((verified != nullptr) && !verified->terminal) {
                verified->terminal = true;
                this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation,
                                               Components::PayloadOperationState::FAILED, productId,
                                               productBytes, sourceCrc, VERIFIED_REJECT_INVALID_PRODUCT);
            }
            if (scienceRequested) {
                this->clearDownlinkIntent(requestId, generation);
            }
            this->clearActiveCapture();
            this->reportMissionMode(Components::MissionMode::BASE, 0U);
            return;
        }
        this->publishScienceProduct(productId, productBytes, sourceKind, sourcePath, sourceCrc);
        if ((verified != nullptr) && !verified->terminal) {
            verified->terminal = true;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        if (scienceRequested && this->hasDownlinkIntent(requestId, generation) &&
            this->isConnected_deliveryRequestOut_OutputPort(0)) {
            // The two outputs target the same StorageManager actor. F' queues
            // these in call order, so Storage stores/validates before delivery.
            this->deliveryRequestOut_out(0, productId);
        }
        if (scienceRequested) {
            this->clearDownlinkIntent(requestId, generation);
        }
        this->clearActiveCapture();
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
        this->clearActiveCapture();
        this->reportMissionMode(Components::MissionMode::BASE, 0U);
    }
}

// MissionApp sends its mode on every change and at start-up. SAFE inhibits science (SDD v4.0 Table 5),
// so while it reads SAFE every capture path here refuses. Leaving SAFE opens them again.
void ScienceApp::missionModeIn_handler(FwIndexType portNum, const Components::MissionMode& mode, U32 detail) {
    static_cast<void>(portNum);
    static_cast<void>(detail);
    const bool inhibit = (mode == Components::MissionMode::SAFE);
    if (inhibit == this->m_scienceInhibited) {
        return;
    }
    this->m_scienceInhibited = inhibit;
    this->log_ACTIVITY_HI_ScienceInhibitChanged(static_cast<U8>(inhibit ? 1U : 0U), mode);
    // A capture already running cannot be stopped from here: PayloadManager owns it and the EPS rail
    // owns payload power. Mark it, so its product is withheld when it ends.
    if (inhibit && (this->m_activeCaptureRequestId != 0U)) {
        this->m_activeCaptureInterrupted = true;
        this->log_WARNING_HI_CaptureRunningInSafe(this->m_activeCaptureRequestId);
    }
}

void ScienceApp::START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!this->scienceAllowed(Components::ScienceInhibitSource::START_COLLECTION)) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    if (!this->isValidCaptureDuration(this->m_captureDurationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS,
                                                    this->m_captureDurationSeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    const bool submitted = this->startCapture(this->m_captureDurationSeconds,
                                              Components::ScienceInhibitSource::START_COLLECTION);
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void ScienceApp::CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) {
    if (!this->isValidCaptureDuration(durationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS, durationSeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_captureDurationSeconds = durationSeconds;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_CaptureDurationConfigured(durationSeconds);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ScienceApp::SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) {
    if (!this->scienceAllowed(Components::ScienceInhibitSource::SCIENCE_CAPTURE)) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!this->isValidCaptureDuration(durationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS, durationSeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    const bool submitted = this->startCapture(durationSeconds, Components::ScienceInhibitSource::SCIENCE_CAPTURE);
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void ScienceApp::COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) {
#ifdef NEUTRON2_CDR_TCP
    this->log_WARNING_LO_ScienceCommandRejected(REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED, opCode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
    return;
#endif
    if (!this->scienceAllowed(Components::ScienceInhibitSource::COLLECT_AND_DOWNLINK)) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!this->isValidCaptureDuration(durationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS, durationSeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    const bool submitted =
        this->startCapture(durationSeconds, Components::ScienceInhibitSource::COLLECT_AND_DOWNLINK, true);
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void ScienceApp::VERIFIED_COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq,
                                                           U32 verificationToken, U32 durationSeconds) {
#ifdef NEUTRON2_CDR_TCP
    this->log_WARNING_LO_ScienceCommandRejected(REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED, opCode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
    return;
#endif
    if (!this->scienceAllowed(Components::ScienceInhibitSource::VERIFIED_COLLECT_AND_DOWNLINK)) {
        // Refused before a token-table entry is used, so the same token can be sent again after SAFE.
        this->logVerifiedCaptureStatus(verificationToken, 0U, 0U,
                                       Components::PayloadOperationState::INVALID, 0U, 0U, 0U,
                                       VERIFIED_REJECT_SAFE_MODE);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
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
    if (!this->isValidCaptureDuration(durationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS, durationSeconds);
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
    const bool submitted = this->startCapture(
        durationSeconds, Components::ScienceInhibitSource::VERIFIED_COLLECT_AND_DOWNLINK, true, &requestId);
    record.requestId = requestId;
    if (!submitted) {
        record.terminal = true;
        this->logVerifiedCaptureStatus(verificationToken, requestId, 0U,
                                       Components::PayloadOperationState::DRIVER_UNAVAILABLE, 0U, 0U, 0U,
                                       VERIFIED_REJECT_NO_REQUEST_PORT);
    }
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

bool ScienceApp::isValidCaptureDuration(U32 durationSeconds) const {
    return (durationSeconds > 0U) && (durationSeconds <= MAX_CAPTURE_DURATION_SECONDS);
}

void ScienceApp::cancelPendingCollection() {
    const U32 remainingSeconds = this->m_pendingDelaySeconds;
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_CollectionCancelled(remainingSeconds);
}

// SAFE inhibits science (SDD v4.0 Table 5). ScienceApp owns the capture commands, so the mode check sits
// here (SDD v4.0 §4.2). A refusal is reported with ScienceInhibited and names where the capture came from.
bool ScienceApp::scienceAllowed(Components::ScienceInhibitSource source) {
    if (!this->m_scienceInhibited) {
        return true;
    }
    this->log_WARNING_LO_ScienceInhibited(source);
    return false;
}

// The command handlers ask scienceAllowed() first so they can answer VALIDATION_ERROR. The scheduled
// countdown relies on the check below. Either way, no capture request leaves ScienceApp in SAFE.
bool ScienceApp::startCapture(U32 durationSeconds, Components::ScienceInhibitSource source,
                              bool requestDownlink, U32* requestIdOut) {
    if (!this->scienceAllowed(source)) {
        return false;
    }
    U32 requestId = this->m_nextRequestId++;
    if (requestId == 0U) {
        requestId = this->m_nextRequestId++;
    }
    if (this->m_nextRequestId == 0U) {
        this->m_nextRequestId = 1U;
    }
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
    this->payloadRequestOut_out(0, requestId, durationSeconds);
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
    for (U32 index = 0; index < DOWNLINK_INTENT_CAPACITY; ++index) {
        if (this->m_downlinkRequestIds[index] == 0U) {
            this->m_downlinkRequestIds[index] = requestId;
            this->m_downlinkRequestGenerations[index] = 0U;
            return true;
        }
    }
    return false;
}

void ScienceApp::setDownlinkIntentGeneration(U32 requestId, U32 generation) {
    for (U32 index = 0; index < DOWNLINK_INTENT_CAPACITY; ++index) {
        if (this->m_downlinkRequestIds[index] == requestId) {
            this->m_downlinkRequestGenerations[index] = generation;
            return;
        }
    }
}

bool ScienceApp::hasDownlinkIntent(U32 requestId, U32 generation) const {
    for (U32 index = 0; index < DOWNLINK_INTENT_CAPACITY; ++index) {
        if ((this->m_downlinkRequestIds[index] == requestId) &&
            (this->m_downlinkRequestGenerations[index] == generation)) {
            return true;
        }
    }
    return false;
}

void ScienceApp::clearDownlinkIntent(U32 requestId, U32 generation) {
    for (U32 index = 0; index < DOWNLINK_INTENT_CAPACITY; ++index) {
        if ((this->m_downlinkRequestIds[index] == requestId) &&
            (this->m_downlinkRequestGenerations[index] == generation)) {
            this->m_downlinkRequestIds[index] = 0U;
            this->m_downlinkRequestGenerations[index] = 0U;
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
    if (this->isConnected_scienceProductOut_OutputPort(0)) {
        this->scienceProductOut_out(0, productId, productBytes, sourceKind, sourcePath, sourceCrc);
    }
    this->reportMissionMode(Components::MissionMode::SCIENCE_READY, productBytes);
}

// In SAFE, MissionApp holds against every subsystem report, so ScienceApp sends none there.
void ScienceApp::reportMissionMode(Components::MissionMode mode, U32 detail) {
    if (this->m_scienceInhibited || !this->isConnected_missionModeOut_OutputPort(0)) {
        return;
    }
    this->missionModeOut_out(0, mode, detail);
}

void ScienceApp::clearActiveCapture() {
    this->m_activeCaptureRequestId = 0U;
    this->m_activeCaptureGeneration = 0U;
    this->m_activeCaptureScienceRequested = false;
    this->m_activeCaptureInterrupted = false;
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
    this->tlmWrite_CaptureDurationSeconds(this->m_captureDurationSeconds);
    this->tlmWrite_CollectionCount(this->m_collectionCount);
}

}  // namespace Components
