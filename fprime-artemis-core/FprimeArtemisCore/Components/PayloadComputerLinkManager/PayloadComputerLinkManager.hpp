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

    //! Heartbeats received since boot
    U32 m_heartbeatsReceived = 0;
};

}  // namespace Components

#endif
