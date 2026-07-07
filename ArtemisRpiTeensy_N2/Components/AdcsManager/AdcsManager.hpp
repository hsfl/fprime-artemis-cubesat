#ifndef Components_AdcsManager_HPP
#define Components_AdcsManager_HPP

#include "Components/AdcsManager/AdcsManagerComponentAc.hpp"

namespace Components {

class AdcsManager final : public AdcsManagerComponentBase {
  public:
    AdcsManager(const char* const compName);
    ~AdcsManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void driverStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_ADCS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_ADCS_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 mode) override;
    void REQUEST_ATTITUDE_UPDATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 requestKey) override;

    U32 m_state;
    U32 m_mode;
    U32 m_attitudeRequestKey;
    U32 m_managerHeartbeat;
};

}  // namespace Components

#endif
