#ifndef Components_AdcsService_HPP
#define Components_AdcsService_HPP

#include "Components/AdcsService/AdcsServiceComponentAc.hpp"

namespace Components {

class AdcsService final : public AdcsServiceComponentBase {
  public:
    AdcsService(const char* const compName);
    ~AdcsService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_ADCS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_ADCS_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 mode) override;
    void REQUEST_ATTITUDE_UPDATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 requestKey) override;

    U32 m_state;
    U32 m_mode;
    U32 m_attitudeRequestKey;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
