// ======================================================================
// \title  PayloadComputerLinkManager.cpp
// \brief  cpp file for PayloadComputerLinkManager component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/PayloadComputerLinkManager/PayloadComputerLinkManager.hpp"

namespace Components {

namespace {

//! run is driven at 10 Hz and the payload computer reports at 1 Hz, so 30
//! ticks tolerates two lost reports before the state is declared stale.
constexpr U32 PAYLOAD_STATE_TIMEOUT_TICKS = 30U;
constexpr U32 RUN_PERIOD_MS = 100U;

}  // namespace

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

void PayloadComputerLinkManager::payloadStateIn_handler(FwIndexType portNum,
                                                        const Components::PayloadState& payloadState) {
    this->m_ticksSinceStateReport = 0;
    this->setPayloadState(payloadState);
}

void PayloadComputerLinkManager::run_handler(FwIndexType portNum, U32 context) {
    if (this->m_ticksSinceStateReport < PAYLOAD_STATE_TIMEOUT_TICKS) {
        this->m_ticksSinceStateReport++;
        return;
    }
    if (this->m_payloadState != Components::PayloadState::UNKNOWN) {
        this->log_WARNING_LO_PayloadStateStale(PAYLOAD_STATE_TIMEOUT_TICKS * RUN_PERIOD_MS);
        this->setPayloadState(Components::PayloadState::UNKNOWN);
    }
}

void PayloadComputerLinkManager::setPayloadState(Components::PayloadState payloadState) {
    if (payloadState != this->m_payloadState) {
        this->m_payloadState = payloadState;
        this->log_ACTIVITY_HI_PayloadStateChanged(payloadState);
    }
    this->tlmWrite_PayloadState(payloadState);
    if (this->isConnected_payloadStateOut_OutputPort(0)) {
        this->payloadStateOut_out(0, payloadState);
    }
}

}  // namespace Components
