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
    //! Emit one heartbeat and the latest payload state toward the flight controller
    void run_handler(FwIndexType portNum, U32 context) override;

    //! Cache the latest payload state; run sends it
    void payloadStateIn_handler(FwIndexType portNum, const Components::PayloadState& payloadState) override;

    //! Emit a heartbeat on command
    void SEND_HEARTBEAT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Emit a heartbeat and update telemetry
    void sendHeartbeat();

    //! Heartbeats emitted since boot. Doubles as the Svc.Ping key, so the
    //! flight controller can see the sequence advance.
    U32 m_heartbeats = 0;

    //! Latest payload state, resent every tick so a lost message heals itself
    Components::PayloadState m_payloadState = Components::PayloadState::UNKNOWN;
};

}  // namespace Components

#endif
