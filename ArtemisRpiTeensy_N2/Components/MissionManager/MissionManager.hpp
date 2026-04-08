#ifndef Components_MissionManager_HPP
#define Components_MissionManager_HPP

#include "Components/MissionManager/MissionManagerComponentAc.hpp"

namespace Components {

class MissionManager final : public MissionManagerComponentBase {
  public:
    MissionManager(const char* const compName);
    ~MissionManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void ENTER_BASE_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SCHEDULE_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 delaySeconds) override;

    U32 m_currentMode;
    U32 m_lastScheduledDelaySeconds;
    U32 m_modeHeartbeat;
};

}  // namespace Components

#endif
