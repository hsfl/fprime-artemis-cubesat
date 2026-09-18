// ======================================================================
// \title  FlightControllerLinkManager.hpp
// \brief  hpp file for FlightControllerLinkManager component implementation class
// ======================================================================

#ifndef Components_FlightControllerLinkManager_HPP
#define Components_FlightControllerLinkManager_HPP

#include "FprimeArtemisCore/Components/FlightControllerLinkManager/FlightControllerLinkManagerComponentAc.hpp"

namespace Components {

class FlightControllerLinkManager final : public FlightControllerLinkManagerComponentBase {
  public:
    explicit FlightControllerLinkManager(const char* const compName);
    ~FlightControllerLinkManager();

  private:
    //! Emit one heartbeat toward the flight controller
    void run_handler(FwIndexType portNum, U32 context) override;

    //! Emit a heartbeat on command
    void SEND_HEARTBEAT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Emit a heartbeat and update telemetry
    void sendHeartbeat();

    //! Heartbeats emitted since boot. Doubles as the Svc.Ping key, so the
    //! flight controller can see the sequence advance.
    U32 m_heartbeats = 0;
};

}  // namespace Components

#endif
