// ======================================================================
// \title  PayloadComputerLinkManager.cpp
// \brief  cpp file for PayloadComputerLinkManager component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/PayloadComputerLinkManager/PayloadComputerLinkManager.hpp"

namespace Components {

PayloadComputerLinkManager::PayloadComputerLinkManager(const char* const compName)
    : PayloadComputerLinkManagerComponentBase(compName) {}

PayloadComputerLinkManager::~PayloadComputerLinkManager() {}

void PayloadComputerLinkManager::peerAliveIn_handler(FwIndexType portNum, U32 key) {
    this->m_heartbeatsReceived++;
    this->tlmWrite_HeartbeatsReceived(this->m_heartbeatsReceived);
    this->tlmWrite_LastHeartbeatKey(key);

    if (this->isConnected_peerAliveOut_OutputPort(0)) {
        this->peerAliveOut_out(0, key);
    }
}

}  // namespace Components
