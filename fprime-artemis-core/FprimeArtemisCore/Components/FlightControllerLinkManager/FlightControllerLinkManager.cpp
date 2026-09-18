// ======================================================================
// \title  FlightControllerLinkManager.cpp
// \brief  cpp file for FlightControllerLinkManager component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/FlightControllerLinkManager/FlightControllerLinkManager.hpp"

namespace Components {

FlightControllerLinkManager::FlightControllerLinkManager(const char* const compName)
    : FlightControllerLinkManagerComponentBase(compName) {}

FlightControllerLinkManager::~FlightControllerLinkManager() {}

void FlightControllerLinkManager::run_handler(FwIndexType portNum, U32 context) {
    this->sendHeartbeat();
}

void FlightControllerLinkManager::SEND_HEARTBEAT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->sendHeartbeat();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void FlightControllerLinkManager::sendHeartbeat() {
    if (this->m_heartbeats == 0) {
        this->log_ACTIVITY_HI_LinkHeartbeatStarted();
    }
    this->m_heartbeats++;
    if (this->isConnected_peerAliveOut_OutputPort(0)) {
        // The key is the heartbeat count, so the peer can observe the sequence.
        this->peerAliveOut_out(0, this->m_heartbeats);
    }
    this->tlmWrite_HeartbeatsSent(this->m_heartbeats);
}

}  // namespace Components
