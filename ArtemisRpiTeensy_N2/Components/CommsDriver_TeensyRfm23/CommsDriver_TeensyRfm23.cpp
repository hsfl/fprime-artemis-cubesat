#include "Components/CommsDriver_TeensyRfm23/CommsDriver_TeensyRfm23.hpp"

#include <cstring>

namespace Components {

CommsDriver_TeensyRfm23::CommsDriver_TeensyRfm23(const char* const compName)
    : CommsDriver_TeensyRfm23ComponentBase(compName),
      m_lastRequestKey(0),
      m_requestCount(0),
      m_sequence(0),
      m_linkState(LinkState::DOWN),
      m_rssiDbm(-120),
      m_rfRxPackets(0),
      m_rfTxPackets(0),
      m_rfTxDrops(0) {
    std::memset(this->m_localPacket, 0, sizeof(this->m_localPacket));
}

CommsDriver_TeensyRfm23::~CommsDriver_TeensyRfm23() {}

void CommsDriver_TeensyRfm23::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void CommsDriver_TeensyRfm23::requestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_lastRequestKey = key;
    this->m_requestCount += 1;

    if (this->sendRfStatsRequest()) {
        this->tlmWrite_LastRequestKey(this->m_lastRequestKey);
        this->tlmWrite_RequestCount(this->m_requestCount);
        return;
    }

    const LinkState previousState = this->m_linkState;
    this->applyTransportPoll(key);

    this->emitStatus();

    const U32 linkStatus = this->toStatusKey();

    if (previousState != this->m_linkState) {
        this->log_ACTIVITY_HI_LinkStateChanged(static_cast<U32>(previousState), linkStatus);
    }

    this->log_ACTIVITY_LO_RequestHandled(this->m_lastRequestKey, linkStatus);
}

void CommsDriver_TeensyRfm23::teensyResponseIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    const LinkState previousState = this->m_linkState;
    bool parsed = false;
    if (fwBuffer.isValid()) {
        parsed = this->parseRfStatsResponse(fwBuffer.getData(), fwBuffer.getSize());
    }

    if (!parsed) {
        this->m_linkState = LinkState::DEGRADED;
        this->m_rfTxDrops += 1U;
    }

    this->emitStatus();

    const U32 linkStatus = this->toStatusKey();
    if (previousState != this->m_linkState) {
        this->log_ACTIVITY_HI_LinkStateChanged(static_cast<U32>(previousState), linkStatus);
    }
    this->log_ACTIVITY_LO_RequestHandled(this->m_lastRequestKey, linkStatus);
}

void CommsDriver_TeensyRfm23::emitStatus() {
    const U32 linkStatus = this->toStatusKey();

    this->tlmWrite_LastRequestKey(this->m_lastRequestKey);
    this->tlmWrite_RequestCount(this->m_requestCount);
    this->tlmWrite_LinkState(linkStatus);
    this->tlmWrite_RssiDbm(this->m_rssiDbm);
    this->tlmWrite_RfRxPackets(this->m_rfRxPackets);
    this->tlmWrite_RfTxPackets(this->m_rfTxPackets);
    this->tlmWrite_RfTxDrops(this->m_rfTxDrops);

    if (this->isConnected_rssiStatusOut_OutputPort(0)) {
        this->rssiStatusOut_out(0, this->m_rssiDbm);
    }

    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0, linkStatus);
    }

    if (this->isConnected_statusOut_OutputPort(1)) {
        // Port 1 is consumed by TeensyTransportManager as a downlink-frame counter snapshot.
        this->statusOut_out(1, this->m_rfRxPackets);
    }
}

bool CommsDriver_TeensyRfm23::sendRfStatsRequest() {
    if (!this->isConnected_teensyRequestOut_OutputPort(0)) {
        return false;
    }

    const U8 seq = ++this->m_sequence;
    this->m_localPacket[0] = LinkCfg::TEENSY_TARGET_RF_STATUS;
    this->m_localPacket[1] = seq;
    this->m_localPacket[2] = 1U;
    this->m_localPacket[3] = 0U;
    this->m_localPacket[4] = LinkCfg::TEENSY_RF_OP_LINK_STATS;

    Fw::Buffer localRequest(this->m_localPacket, sizeof(this->m_localPacket));
    this->teensyRequestOut_out(0, localRequest);
    return true;
}

bool CommsDriver_TeensyRfm23::parseRfStatsResponse(const U8* data, FwSizeType size) {
    static constexpr FwSizeType LOCAL_HEADER_LEN = 4;
    static constexpr U8 RF_STATS_PAYLOAD_LEN = 21;

    if (data == nullptr || size < LOCAL_HEADER_LEN) {
        return false;
    }
    if (data[0] != LinkCfg::TEENSY_TARGET_RF_STATUS ||
        data[2] != LinkCfg::TEENSY_STATUS_OK || data[3] != RF_STATS_PAYLOAD_LEN ||
        size < static_cast<FwSizeType>(LOCAL_HEADER_LEN + RF_STATS_PAYLOAD_LEN) ||
        data[4] != LinkCfg::TEENSY_RF_OP_LINK_STATS) {
        return false;
    }

    this->m_rssiDbm = static_cast<I16>(readLe16(&data[5]));
    const U32 rxGood = readLe16(&data[7]);
    const U32 rxBad = readLe16(&data[9]);
    static_cast<void>(rxBad);
    this->m_rfRxPackets = readLe32(&data[13]);
    this->m_rfTxPackets = readLe32(&data[17]);
    this->m_rfTxDrops = readLe32(&data[21]);

    if (this->m_rfRxPackets > 0U || rxGood > 0U) {
        this->m_linkState = LinkState::LOCKED;
    } else if (this->m_rfTxPackets > 0U) {
        this->m_linkState = LinkState::ACQUIRING;
    } else {
        this->m_linkState = LinkState::DOWN;
    }
    return true;
}

void CommsDriver_TeensyRfm23::applyTransportPoll(U32 pollKey) {
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

U32 CommsDriver_TeensyRfm23::toStatusKey() const {
    return static_cast<U32>(this->m_linkState);
}

U16 CommsDriver_TeensyRfm23::readLe16(const U8* data) {
    return static_cast<U16>(data[0]) | static_cast<U16>(data[1] << 8U);
}

U32 CommsDriver_TeensyRfm23::readLe32(const U8* data) {
    return static_cast<U32>(data[0]) |
           (static_cast<U32>(data[1]) << 8U) |
           (static_cast<U32>(data[2]) << 16U) |
           (static_cast<U32>(data[3]) << 24U);
}

}  // namespace Components
