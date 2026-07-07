#include "Components/PayloadManager/PayloadManager.hpp"

namespace Components {

PayloadManager::PayloadManager(const char* const compName)
    : PayloadManagerComponentBase(compName),
      m_lastPayloadValue(0),
      m_lastCollectionId(0),
      m_lastCaptureDurationSeconds(600),
      m_sampleCount(1),
      m_samplePeriodMs(1000),
      m_managerHeartbeat(0) {}

PayloadManager::~PayloadManager() {}

void PayloadManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_managerHeartbeat += 1;
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        const Components::HealthState health =
            (this->m_lastPayloadValue == 0U) ? Components::HealthState::UNKNOWN : Components::HealthState::OK;
        this->sohStatusOut_out(0, health, this->m_lastPayloadValue);
    }
    this->tlmWrite_LastPayloadValue(this->m_lastPayloadValue);
    this->tlmWrite_LastCollectionId(this->m_lastCollectionId);
    this->tlmWrite_LastCaptureDurationSeconds(this->m_lastCaptureDurationSeconds);
    this->tlmWrite_SampleCount(this->m_sampleCount);
    this->tlmWrite_SamplePeriodMs(this->m_samplePeriodMs);
    this->tlmWrite_ManagerHeartbeat(this->m_managerHeartbeat);
}

void PayloadManager::requestIn_handler(FwIndexType portNum, U32 durationSeconds) {
    static_cast<void>(portNum);
    this->m_lastCollectionId += 1U;
    this->m_lastCaptureDurationSeconds = durationSeconds;
    this->log_ACTIVITY_HI_PayloadCollectionForwarded(durationSeconds);
    if (this->isConnected_driverRequestOut_OutputPort(0)) {
        this->driverRequestOut_out(0, durationSeconds);
    }
}

void PayloadManager::driverStatusIn_handler(FwIndexType portNum,
                                             U32 productId,
                                             U32 productBytes,
                                             const Components::ScienceProductSource& sourceKind,
                                             const Fw::StringBase& sourcePath,
                                             U32 sourceCrc) {
    static_cast<void>(portNum);
    this->m_lastPayloadValue = productBytes;
    this->log_ACTIVITY_LO_PayloadStatusUpdated(productBytes);
    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, productId, productBytes, sourceKind, sourcePath, sourceCrc);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        const Components::HealthState health =
            (productBytes == 0U) ? Components::HealthState::FAIL : Components::HealthState::OK;
        this->sohStatusOut_out(0, health, productBytes);
    }
}

void PayloadManager::REQUEST_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->tlmWrite_LastPayloadValue(this->m_lastPayloadValue);
    this->tlmWrite_LastCollectionId(this->m_lastCollectionId);
    this->tlmWrite_LastCaptureDurationSeconds(this->m_lastCaptureDurationSeconds);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadManager::CONFIGURE_PAYLOAD_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    U32 sampleCount,
    U32 periodMs
) {
    this->m_sampleCount = sampleCount;
    this->m_samplePeriodMs = periodMs;
    this->log_ACTIVITY_HI_PayloadConfigured(this->m_sampleCount, this->m_samplePeriodMs);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadManager::START_PAYLOAD_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 collectionId) {
    this->m_lastCollectionId = collectionId;
    this->log_ACTIVITY_HI_PayloadCollectionForwarded(collectionId);
    if (this->isConnected_driverRequestOut_OutputPort(0)) {
        this->driverRequestOut_out(0, collectionId);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadManager::SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) {
    this->m_lastCaptureDurationSeconds = durationSeconds;
    this->log_ACTIVITY_HI_PayloadScienceCaptureRequested(durationSeconds);
    if (this->isConnected_driverRequestOut_OutputPort(0)) {
        this->driverRequestOut_out(0, durationSeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
