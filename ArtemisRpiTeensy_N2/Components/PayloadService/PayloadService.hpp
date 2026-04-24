#ifndef Components_PayloadService_HPP
#define Components_PayloadService_HPP

#include "Components/PayloadService/PayloadServiceComponentAc.hpp"

namespace Components {

class PayloadService final : public PayloadServiceComponentBase {
  public:
    PayloadService(const char* const compName);
    ~PayloadService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 key) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void REQUEST_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_lastPayloadValue;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
