#ifndef Components_EpsService_HPP
#define Components_EpsService_HPP

#include "Components/EpsService/EpsServiceComponentAc.hpp"

namespace Components {

class EpsService final : public EpsServiceComponentBase {
  public:
    EpsService(const char* const compName);
    ~EpsService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_EPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_state;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
