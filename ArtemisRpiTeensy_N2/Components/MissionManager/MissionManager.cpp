#include "Components/MissionManager/MissionManager.hpp"

namespace Components {

enum MissionModes {
    BASE_MODE = 0,
    DATA_COLLECTION_PENDING = 1,
};

MissionManager::MissionManager(const char* const compName)
    : MissionManagerComponentBase(compName),
      m_currentMode(BASE_MODE),
      m_lastScheduledDelaySeconds(0),
      m_modeHeartbeat(0) {}

MissionManager::~MissionManager() {}

void MissionManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void MissionManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_modeHeartbeat += 1;
    this->tlmWrite_CurrentMode(this->m_currentMode);
    this->tlmWrite_LastScheduledDelaySeconds(this->m_lastScheduledDelaySeconds);
    this->tlmWrite_ModeHeartbeat(this->m_modeHeartbeat);
}

void MissionManager::ENTER_BASE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_currentMode = BASE_MODE;
    this->log_ACTIVITY_HI_ModeChanged(this->m_currentMode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void MissionManager::SCHEDULE_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 delaySeconds) {
    this->m_currentMode = DATA_COLLECTION_PENDING;
    this->m_lastScheduledDelaySeconds = delaySeconds;
    this->log_ACTIVITY_HI_ModeChanged(this->m_currentMode);
    this->log_ACTIVITY_HI_CollectionScheduled(delaySeconds);
    if (this->isConnected_collectionRequestOut_OutputPort(0)) {
        this->collectionRequestOut_out(0, delaySeconds);
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
