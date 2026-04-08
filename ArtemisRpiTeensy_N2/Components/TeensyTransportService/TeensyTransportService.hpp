#ifndef Components_TeensyTransportService_HPP
#define Components_TeensyTransportService_HPP

#include "Components/TeensyTransportService/TeensyTransportServiceComponentAc.hpp"

namespace Components {

class TeensyTransportService final : public TeensyTransportServiceComponentBase {
  public:
    TeensyTransportService(const char* const compName);
    ~TeensyTransportService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void RESET_COUNTERS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    U32 m_linkHeartbeat;
    U32 m_uplinkFrames;
    U32 m_downlinkFrames;
};

}  // namespace Components

#endif
