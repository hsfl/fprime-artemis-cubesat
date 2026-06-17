#ifndef Components_ThermalService_HPP
#define Components_ThermalService_HPP

#include "Components/ThermalService/ThermalServiceComponentAc.hpp"

namespace Components {

class ThermalService final : public ThermalServiceComponentBase {
  public:
    ThermalService(const char* const compName);
    ~ThermalService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_THERMAL_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_THERMAL_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 mode) override;

    U32 m_state;
    U32 m_mode;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
