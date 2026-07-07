#ifndef Components_TeensyTransportManager_HPP
#define Components_TeensyTransportManager_HPP

#include "Components/TeensyTransportManager/TeensyTransportManagerComponentAc.hpp"

namespace Components {

class TeensyTransportManager final : public TeensyTransportManagerComponentBase {
  public:
    TeensyTransportManager(const char* const compName);
    ~TeensyTransportManager();

  private:
    enum class LinkState : U32 {
        DOWN = 0,
        ACQUIRING = 1,
        LOCKED = 2,
        DEGRADED = 3,
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void driverStatusIn_handler(FwIndexType portNum, U32 key) override;
    void LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void RESET_COUNTERS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void updateLinkState();
    void writeTelemetry();
    bool shouldWriteTelemetry() const;

    U32 m_linkHeartbeat;
    U32 m_uplinkFrames;
    U32 m_downlinkFrames;
    U32 m_lastDownlinkFrames;
    U32 m_lastProgressHeartbeat;
    U32 m_lastTelemetryHeartbeat;
    LinkState m_lastReportedLinkState;
    LinkState m_linkState;
};

}  // namespace Components

#endif
