// ======================================================================
// \title  PayloadComputerLinkManager.hpp
// \brief  hpp file for PayloadComputerLinkManager component implementation class
// ======================================================================

#ifndef Components_PayloadComputerLinkManager_HPP
#define Components_PayloadComputerLinkManager_HPP

#include "FprimeArtemisCore/Components/PayloadComputerLinkManager/PayloadComputerLinkManagerComponentAc.hpp"

namespace Components {

class PayloadComputerLinkManager final : public PayloadComputerLinkManagerComponentBase {
  public:
    explicit PayloadComputerLinkManager(const char* const compName);
    ~PayloadComputerLinkManager();

  private:
    //! Payload computer heartbeat arrived over the link
    void peerAliveIn_handler(FwIndexType portNum, U32 key) override;

    //! Payload readiness arrived over the link
    void payloadStateIn_handler(FwIndexType portNum, const Components::PayloadState& payloadState) override;

    //! Drop the payload state to UNKNOWN once reports stop arriving
    void run_handler(FwIndexType portNum, U32 context) override;

    //! Record a new payload state, emitting an event on change
    void setPayloadState(Components::PayloadState payloadState);

    //! Heartbeats received since boot
    U32 m_heartbeatsReceived = 0;

    //! Latest payload state from the payload computer
    Components::PayloadState m_payloadState = Components::PayloadState::UNKNOWN;

    //! run ticks since the last payload state report
    U32 m_ticksSinceStateReport = 0;
};

}  // namespace Components

#endif
