#ifndef Components_ThermalManager_HPP
#define Components_ThermalManager_HPP

#include "Components/ThermalManager/ThermalManagerComponentAc.hpp"

namespace Components {

class ThermalManager final : public ThermalManagerComponentBase {
  public:
    ThermalManager(const char* const compName);
    ~ThermalManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void driverStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_THERMAL_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_THERMAL_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 mode) override;

    U32 m_state;
    U32 m_mode;
    U32 m_managerHeartbeat;
};

}  // namespace Components

#endif
