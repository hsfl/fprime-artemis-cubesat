#ifndef Components_TeensyTransportService_HPP
#define Components_TeensyTransportService_HPP

#include "Components/TeensyTransportService/TeensyTransportServiceComponentAc.hpp"

namespace Components {

class TeensyTransportService final : public TeensyTransportServiceComponentBase {
  public:
    TeensyTransportService(const char* const compName);
    ~TeensyTransportService();

  private:
    enum class LinkState : U32 {
        DOWN = 0,
        ACQUIRING = 1,
        LOCKED = 2,
        DEGRADED = 3,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(FwIndexType portNum, U32 key) override;
    void LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void RESET_COUNTERS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void updateLinkState();

    U32 m_linkHeartbeat;
    U32 m_uplinkFrames;
    U32 m_downlinkFrames;
    U32 m_lastDownlinkFrames;
    U32 m_lastProgressHeartbeat;
    LinkState m_linkState;
};

}  // namespace Components

#endif
