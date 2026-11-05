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
      m_productAwaitingStorage(0),
      m_productAwaitingStorageBytes(0),
      m_deliverAfterStorage(false),
      m_downlinkRequestIds{},
      m_downlinkRequestGenerations{} {}

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
            this->startCapture(this->m_captureDurationSeconds);
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
    this->cancelPendingCollection();
    if (this->isConnected_cancelReportOut_OutputPort(0)) {
        this->cancelReportOut_out(0, key);
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
        if ((verified != nullptr) && (verified->generation == 0U) && scienceRequested) {
            verified->generation = generation;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        if (scienceRequested) {
            this->setDownlinkIntentGeneration(requestId, generation);
        }
        if (this->isConnected_missionModeOut_OutputPort(0)) {
            this->missionModeOut_out(0, Components::MissionMode::COLLECTING, this->m_captureDurationSeconds);
        }
        return;
    }

    if ((generation == 0U) &&
        ((state == Components::PayloadOperationState::BUSY) ||
         (state == Components::PayloadOperationState::INHIBITED) ||
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
        // An inhibited request never started, and MissionApp is the one inhibiting: nothing to report.
        if ((this->m_activeCaptureRequestId == 0U) && (state != Components::PayloadOperationState::INHIBITED) &&
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
        this->publishScienceProduct(productId, productBytes, sourceKind, sourcePath, sourceCrc);
        if ((verified != nullptr) && !verified->terminal) {
            verified->terminal = true;
            this->logVerifiedCaptureStatus(verified->verificationToken, requestId, generation, state,
                                           productId, productBytes, sourceCrc, 0U);
        }
        this->m_deliverAfterStorage = scienceRequested && this->hasDownlinkIntent(requestId, generation);
        if (scienceRequested) {
            this->clearDownlinkIntent(requestId, generation);
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

void ScienceApp::START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    if (!this->isValidCaptureDuration(this->m_captureDurationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS,
                                                    this->m_captureDurationSeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    const bool submitted = this->startCapture(this->m_captureDurationSeconds);
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
    if (!this->isValidCaptureDuration(durationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS, durationSeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    const bool submitted = this->startCapture(durationSeconds);
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void ScienceApp::COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) {
#ifdef NEUTRON2_CDR_TCP
    this->log_WARNING_LO_ScienceCommandRejected(REJECT_CDR_AUTO_DOWNLINK_UNSUPPORTED, opCode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
    return;
#endif
    if (!this->isValidCaptureDuration(durationSeconds)) {
        this->log_WARNING_LO_ScienceCommandRejected(REJECT_INVALID_CAPTURE_DURATION_SECONDS, durationSeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_pendingDelaySeconds = 0U;
    this->writeTelemetry();
    const bool submitted = this->startCapture(durationSeconds, true);
    this->cmdResponse_out(opCode, cmdSeq, submitted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void ScienceApp::VERIFIED_COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq,
                                                           U32 verificationToken, U32 durationSeconds) {
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
    const bool submitted = this->startCapture(durationSeconds, true, &requestId);
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

bool ScienceApp::startCapture(U32 durationSeconds, bool requestDownlink, U32* requestIdOut) {
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
        // SCIENCE_READY waits for StorageManager's answer on productStoredIn.
        this->m_productAwaitingStorage = productId;
        this->m_productAwaitingStorageBytes = productBytes;
        this->scienceProductOut_out(0, productId, productBytes, sourceKind, sourcePath, sourceCrc);
    } else if (this->isConnected_missionModeOut_OutputPort(0)) {
        this->missionModeOut_out(0, Components::MissionMode::SCIENCE_READY, productBytes);
    }
}

void ScienceApp::productStoredIn_handler(FwIndexType portNum, U32 productId, bool stored) {
    static_cast<void>(portNum);
    if ((productId == 0U) || (productId != this->m_productAwaitingStorage)) {
        return;
    }
    const U32 productBytes = this->m_productAwaitingStorageBytes;
    const bool deliver = this->m_deliverAfterStorage;
    this->m_productAwaitingStorage = 0U;
    this->m_productAwaitingStorageBytes = 0U;
    this->m_deliverAfterStorage = false;
    if (!stored) {
        this->log_WARNING_HI_ScienceProductNotStored(productId);
    }
    if (this->isConnected_missionModeOut_OutputPort(0)) {
        this->missionModeOut_out(0, stored ? Components::MissionMode::SCIENCE_READY : Components::MissionMode::BASE,
                                 stored ? productBytes : 0U);
    }
    // After the report above, so SCIENCE_READY reaches MissionApp ahead of CommsApp's DOWNLINKING.
    if (stored && deliver && this->isConnected_deliveryRequestOut_OutputPort(0)) {
        this->deliveryRequestOut_out(0, productId);
    }
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
