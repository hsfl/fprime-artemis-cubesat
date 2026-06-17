#include "Components/ScienceManager/ScienceManager.hpp"

namespace Components {

ScienceManager::ScienceManager(const char* const compName)
    : ScienceManagerComponentBase(compName),
      m_pendingDelaySeconds(0),
      m_captureDurationSeconds(600),
      m_collectionCount(0) {}

ScienceManager::~ScienceManager() {}

void ScienceManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void ScienceManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);

    if (this->m_pendingDelaySeconds > 0) {
        this->m_pendingDelaySeconds -= 1;
        if (this->m_pendingDelaySeconds == 0) {
            if (this->isConnected_payloadRequestOut_OutputPort(0)) {
                this->payloadRequestOut_out(0, this->m_captureDurationSeconds);
            }
        }
    }

    this->tlmWrite_PendingDelaySeconds(this->m_pendingDelaySeconds);
    this->tlmWrite_CaptureDurationSeconds(this->m_captureDurationSeconds);
    this->tlmWrite_CollectionCount(this->m_collectionCount);
}

void ScienceManager::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_pendingDelaySeconds = key;
    this->log_ACTIVITY_HI_CollectionTriggered(key);
}

void ScienceManager::payloadStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_collectionCount += 1;
    this->log_ACTIVITY_HI_ScienceProductReady(key);
    if (this->isConnected_scienceProductOut_OutputPort(0)) {
        this->scienceProductOut_out(0, key);
    }
}

void ScienceManager::START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_pendingDelaySeconds = 0;
    if (this->isConnected_payloadRequestOut_OutputPort(0)) {
        this->payloadRequestOut_out(0, this->m_captureDurationSeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ScienceManager::CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) {
    this->m_captureDurationSeconds = durationSeconds;
    this->log_ACTIVITY_HI_CaptureDurationConfigured(durationSeconds);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ScienceManager::SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) {
    this->m_pendingDelaySeconds = 0;
    this->m_captureDurationSeconds = durationSeconds;
    this->log_ACTIVITY_HI_CaptureDurationConfigured(durationSeconds);
    if (this->isConnected_payloadRequestOut_OutputPort(0)) {
        this->payloadRequestOut_out(0, durationSeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
