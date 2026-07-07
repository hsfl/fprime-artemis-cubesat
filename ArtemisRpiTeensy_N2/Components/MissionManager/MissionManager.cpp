#include "Components/MissionManager/MissionManager.hpp"

namespace Components {

namespace {
constexpr U32 MAX_SCHEDULE_DELAY_SECONDS = 300U;
constexpr U32 REJECT_INVALID_SCHEDULE_DELAY_SECONDS = 1U;
constexpr U32 REJECT_INVALID_MODE_TRANSITION = 2U;
}  // namespace

MissionManager::MissionManager(const char* const compName)
    : MissionManagerComponentBase(compName),
      m_currentMode(Components::MissionMode::BASE),
      m_lastScheduledDelaySeconds(0),
      m_pingCount(0),
      m_modeHeartbeat(0),
      m_lastTelemetryHeartbeat(0) {}

MissionManager::~MissionManager() {}

void MissionManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void MissionManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_modeHeartbeat += 1;
    constexpr U32 TELEMETRY_PERIOD_TICKS = 30U;
    if ((this->m_modeHeartbeat - this->m_lastTelemetryHeartbeat) >= TELEMETRY_PERIOD_TICKS) {
        this->writeTelemetry();
        this->m_lastTelemetryHeartbeat = this->m_modeHeartbeat;
    }
}

void MissionManager::modeUpdateIn_handler(FwIndexType portNum, const Components::MissionMode& mode, U32 detail) {
    static_cast<void>(portNum);
    if (!this->isAllowedTransition(mode)) {
        this->log_WARNING_LO_ModeUpdateRejected(mode, this->m_currentMode, detail);
        return;
    }
    this->m_currentMode = mode;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_ModeChanged(this->m_currentMode);
}

void MissionManager::ENTER_BASE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!this->transitionToMode(Components::MissionMode::BASE, 0U)) {
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_MODE_TRANSITION, 0U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->cancelCollection();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionManager::PING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 token) {
    this->m_pingCount += 1;
    this->writeTelemetry();
    this->log_ACTIVITY_LO_Pong(token, this->m_pingCount);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionManager::SCHEDULE_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 delaySeconds) {
    if ((delaySeconds == 0U) || (delaySeconds > MAX_SCHEDULE_DELAY_SECONDS)) {
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_SCHEDULE_DELAY_SECONDS, delaySeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!this->isAllowedTransition(Components::MissionMode::COLLECTION_PENDING)) {
        this->log_WARNING_LO_ModeUpdateRejected(
            Components::MissionMode::COLLECTION_PENDING,
            this->m_currentMode,
            delaySeconds);
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_MODE_TRANSITION, delaySeconds);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_currentMode = Components::MissionMode::COLLECTION_PENDING;
    this->m_lastScheduledDelaySeconds = delaySeconds;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_ModeChanged(this->m_currentMode);
    this->log_ACTIVITY_HI_CollectionScheduled(delaySeconds);
    if (this->isConnected_collectionRequestOut_OutputPort(0)) {
        this->collectionRequestOut_out(0, delaySeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionManager::CANCEL_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    if (!this->transitionToMode(Components::MissionMode::BASE, 0U)) {
        this->log_WARNING_LO_MissionCommandRejected(REJECT_INVALID_MODE_TRANSITION, 0U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->cancelCollection();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

bool MissionManager::isAllowedTransition(Components::MissionMode requested) const {
    if (requested == this->m_currentMode) {
        return true;
    }
    if (requested == Components::MissionMode::BASE) {
        return true;
    }

    switch (this->m_currentMode) {
        case Components::MissionMode::BASE:
            return (requested == Components::MissionMode::COLLECTION_PENDING) ||
                   (requested == Components::MissionMode::COLLECTING) ||
                   (requested == Components::MissionMode::DOWNLINKING);
        case Components::MissionMode::COLLECTION_PENDING:
            return requested == Components::MissionMode::COLLECTING;
        case Components::MissionMode::COLLECTING:
            return requested == Components::MissionMode::SCIENCE_READY;
        case Components::MissionMode::SCIENCE_READY:
            return requested == Components::MissionMode::DOWNLINKING;
        case Components::MissionMode::DOWNLINKING:
            return requested == Components::MissionMode::BASE;
        default:
            return false;
    }
}

bool MissionManager::transitionToMode(Components::MissionMode requested, U32 detail) {
    if (!this->isAllowedTransition(requested)) {
        this->log_WARNING_LO_ModeUpdateRejected(requested, this->m_currentMode, detail);
        return false;
    }
    this->m_currentMode = requested;
    this->writeTelemetry();
    this->log_ACTIVITY_HI_ModeChanged(this->m_currentMode);
    return true;
}

void MissionManager::cancelCollection() {
    this->m_lastScheduledDelaySeconds = 0U;
    this->writeTelemetry();
    if (this->isConnected_cancelRequestOut_OutputPort(0)) {
        this->cancelRequestOut_out(0, 0U);
    }
}

void MissionManager::writeTelemetry() {
    this->tlmWrite_CurrentMode(this->m_currentMode);
    this->tlmWrite_LastScheduledDelaySeconds(this->m_lastScheduledDelaySeconds);
    this->tlmWrite_PingCount(this->m_pingCount);
    this->tlmWrite_ModeHeartbeat(this->m_modeHeartbeat);
}

}  // namespace Components
