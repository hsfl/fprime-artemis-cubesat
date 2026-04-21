#include "Components/CommsAdapter_TeensyRfm23/CommsAdapter_TeensyRfm23.hpp"

namespace Components {

CommsAdapter_TeensyRfm23::CommsAdapter_TeensyRfm23(const char* const compName)
    : CommsAdapter_TeensyRfm23ComponentBase(compName),
      m_lastRequestKey(0),
      m_requestCount(0),
      m_linkState(LinkState::DOWN),
      m_rssiDbm(-120),
      m_rfRxPackets(0),
      m_rfTxPackets(0),
      m_rfTxDrops(0) {}

CommsAdapter_TeensyRfm23::~CommsAdapter_TeensyRfm23() {}

void CommsAdapter_TeensyRfm23::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void CommsAdapter_TeensyRfm23::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastRequestKey = key;
    this->m_requestCount += 1;

    const LinkState previousState = this->m_linkState;
    this->applyTransportPoll(key);

    const U32 linkStatus = this->toStatusKey();

    this->tlmWrite_LastRequestKey(this->m_lastRequestKey);
    this->tlmWrite_RequestCount(this->m_requestCount);
    this->tlmWrite_LinkState(linkStatus);
    this->tlmWrite_RssiDbm(this->m_rssiDbm);
    this->tlmWrite_RfRxPackets(this->m_rfRxPackets);
    this->tlmWrite_RfTxPackets(this->m_rfTxPackets);
    this->tlmWrite_RfTxDrops(this->m_rfTxDrops);

    if (previousState != this->m_linkState) {
        this->log_ACTIVITY_HI_LinkStateChanged(static_cast<U32>(previousState), linkStatus);
    }

    this->log_ACTIVITY_LO_RequestHandled(this->m_lastRequestKey, linkStatus);

    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, linkStatus);
    }

    if (this->isConnected_statusOut_OutputPort(1)) {
        // Port 1 is consumed by TeensyTransportService as a downlink-frame counter snapshot.
        this->statusOut_out(1, this->m_rfRxPackets);
    }
}

void CommsAdapter_TeensyRfm23::applyTransportPoll(U32 pollKey) {
    this->m_rfTxPackets += 1U;

    switch (pollKey) {
        case 1U:
            this->m_linkState = LinkState::DOWN;
            break;
        case 2U:
            this->m_linkState = LinkState::ACQUIRING;
            break;
        case 3U:
            this->m_linkState = LinkState::LOCKED;
            break;
        case 4U:
            this->m_linkState = LinkState::DEGRADED;
            break;
        default:
            // Unknown poll keys are treated as degraded to make framing/config issues visible.
            this->m_linkState = LinkState::DEGRADED;
            this->m_rfTxDrops += 1U;
            break;
    }

    if (this->m_linkState == LinkState::DOWN) {
        this->m_rssiDbm = -120;
        if ((this->m_requestCount % 6U) == 0U) {
            this->m_rfTxDrops += 1U;
        }
        return;
    }

    if (this->m_linkState == LinkState::ACQUIRING) {
        this->m_rssiDbm = -111 + static_cast<I32>(this->m_requestCount % 4U);
        if ((this->m_requestCount % 5U) == 0U) {
            this->m_rfRxPackets += 1U;
        }
        return;
    }

    if (this->m_linkState == LinkState::DEGRADED) {
        this->m_rssiDbm = -97 + static_cast<I32>(this->m_requestCount % 3U);
        this->m_rfRxPackets += 1U;
        if ((this->m_requestCount % 3U) == 0U) {
            this->m_rfTxDrops += 1U;
        }
        return;
    }

    this->m_rssiDbm = -76 + static_cast<I32>(this->m_requestCount % 6U);
    this->m_rfRxPackets += 3U;
}

U32 CommsAdapter_TeensyRfm23::toStatusKey() const {
    return static_cast<U32>(this->m_linkState);
}

}  // namespace Components
