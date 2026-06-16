#ifndef Components_PduService_HPP
#define Components_PduService_HPP

#include "Components/PduService/PduServiceComponentAc.hpp"

namespace Components {

class PduService final : public PduServiceComponentBase {
  public:
    PduService(const char* const compName);
    ~PduService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_PDU_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_PAYLOAD_28V_REQUEST_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 requestState) override;
    void SET_SATNOGS_POWER_REQUEST_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 requestState) override;

    U32 m_state;
    U32 m_payload28VRequestState;
    U32 m_satnogsPowerRequestState;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
