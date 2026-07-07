#include "Components/ScienceApp/ScienceApp.hpp"

namespace Components {

namespace {
constexpr U32 DEFAULT_CAPTURE_DURATION_SECONDS = 30U;
constexpr U32 MAX_CAPTURE_DURATION_SECONDS = 120U;
constexpr U32 REJECT_INVALID_DELAY_SECONDS = 1U;
constexpr U32 REJECT_INVALID_CAPTURE_DURATION_SECONDS = 2U;
}  // namespace

ScienceApp::ScienceApp(const char* const compName)
    : ScienceAppComponentBase(compName),
      m_pendingDelaySeconds(0),
      m_captureDurationSeconds(DEFAULT_CAPTURE_DURATION_SECONDS),
      m_collectionCount(0),
      m_runTicks(0),
      m_lastTelemetryTick(0) {}

ScienceApp::~ScienceApp() {}

void ScienceApp::preamble() {
    this->seedCaptureDurationFromParam();
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
            if (this->isConnected_payloadRequestOut_OutputPort(0)) {
                this->payloadRequestOut_out(0, this->m_captureDurationSeconds);
            }
            if (this->isConnected_missionModeOut_OutputPort(0)) {
                this->missionModeOut_out(0, Components::MissionMode::COLLECTING, this->m_captureDurationSeconds);
            }
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

void ScienceApp::payloadStatusIn_handler(FwIndexType portNum,
                                             U32 productId,
                                             U32 productBytes,
                                             const Components::ScienceProductSource& sourceKind,
                                             const Fw::StringBase& sourcePath,
                                             U32 sourceCrc) {
    static_cast<void>(portNum);
    this->m_collectionCount += 1;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_ScienceProductReady(productBytes);
    if (this->isConnected_scienceProductOut_OutputPort(0)) {
        this->scienceProductOut_out(0, productId, productBytes, sourceKind, sourcePath, sourceCrc);
    }
    if (this->isConnected_missionModeOut_OutputPort(0)) {
        this->missionModeOut_out(0, Components::MissionMode::SCIENCE_READY, productBytes);
    }
}

void ScienceApp::START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_pendingDelaySeconds = 0;
    this->writeTelemetry();
    if (this->isConnected_payloadRequestOut_OutputPort(0)) {
        this->payloadRequestOut_out(0, this->m_captureDurationSeconds);
    }
    if (this->isConnected_missionModeOut_OutputPort(0)) {
        this->missionModeOut_out(0, Components::MissionMode::COLLECTING, this->m_captureDurationSeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
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
    this->m_pendingDelaySeconds = 0;
    this->writeTelemetry();
    if (this->isConnected_payloadRequestOut_OutputPort(0)) {
        this->payloadRequestOut_out(0, durationSeconds);
    }
    if (this->isConnected_missionModeOut_OutputPort(0)) {
        this->missionModeOut_out(0, Components::MissionMode::COLLECTING, durationSeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
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

void ScienceApp::writeTelemetry() {
    this->tlmWrite_PendingDelaySeconds(this->m_pendingDelaySeconds);
    this->tlmWrite_CaptureDurationSeconds(this->m_captureDurationSeconds);
    this->tlmWrite_CollectionCount(this->m_collectionCount);
}

}  // namespace Components
