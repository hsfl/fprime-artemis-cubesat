#include "Components/TeensyTransportManager/TeensyTransportManager.hpp"

namespace Components {

TeensyTransportManager::TeensyTransportManager(const char* const compName)
    : TeensyTransportManagerComponentBase(compName),
      m_linkHeartbeat(0),
      m_uplinkFrames(0),
      m_downlinkFrames(0),
      m_lastDownlinkFrames(0),
      m_lastProgressHeartbeat(0),
      m_lastTelemetryHeartbeat(0),
      m_lastReportedLinkState(LinkState::DOWN),
      m_linkState(LinkState::DOWN) {}

TeensyTransportManager::~TeensyTransportManager() {}

void TeensyTransportManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void TeensyTransportManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_linkHeartbeat += 1;
    this->updateLinkState();
    const bool linkStateChanged = this->m_linkState != this->m_lastReportedLinkState;

    if (linkStateChanged || this->shouldWriteTelemetry()) {
        this->writeTelemetry();
        this->m_lastTelemetryHeartbeat = this->m_linkHeartbeat;
    }

    if (linkStateChanged && this->isConnected_linkStatusOut_OutputPort(0)) {
        this->linkStatusOut_out(0, static_cast<U32>(this->m_linkState));
    }
    if (linkStateChanged && this->isConnected_sohStatusOut_OutputPort(0)) {
        Components::HealthState health = Components::HealthState::UNKNOWN;
        if (this->m_linkState == LinkState::LOCKED) {
            health = Components::HealthState::OK;
        } else if ((this->m_linkState == LinkState::ACQUIRING) || (this->m_linkState == LinkState::DEGRADED)) {
            health = Components::HealthState::WARN;
        } else if (this->m_linkState == LinkState::DOWN) {
            health = Components::HealthState::FAIL;
        }
        this->sohStatusOut_out(0, health, static_cast<U32>(this->m_linkState));
    }
    if (linkStateChanged) {
        this->m_lastReportedLinkState = this->m_linkState;
    }
}

void TeensyTransportManager::driverStatusIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->m_uplinkFrames += 1;
    this->m_downlinkFrames = key;
    if (this->m_downlinkFrames > this->m_lastDownlinkFrames) {
        this->m_lastDownlinkFrames = this->m_downlinkFrames;
        this->m_lastProgressHeartbeat = this->m_linkHeartbeat;
    }
}

void TeensyTransportManager::LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->log_ACTIVITY_HI_LinkStatus(this->m_linkHeartbeat, this->m_uplinkFrames, this->m_downlinkFrames);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void TeensyTransportManager::RESET_COUNTERS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_linkHeartbeat = 0;
    this->m_uplinkFrames = 0;
    this->m_downlinkFrames = 0;
    this->m_lastDownlinkFrames = 0;
    this->m_lastProgressHeartbeat = 0;
    this->m_lastTelemetryHeartbeat = 0;
    this->m_lastReportedLinkState = LinkState::DOWN;
    this->m_linkState = LinkState::DOWN;
    this->log_ACTIVITY_LO_CountersReset();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void TeensyTransportManager::updateLinkState() {
    if (this->m_linkHeartbeat <= 2U) {
        this->m_linkState = LinkState::ACQUIRING;
        return;
    }

    if (this->m_downlinkFrames == 0U) {
        if (this->m_uplinkFrames <= 2U) {
            this->m_linkState = LinkState::ACQUIRING;
        } else {
            this->m_linkState = LinkState::DOWN;
        }
        return;
    }

    const U32 staleTicks = this->m_linkHeartbeat - this->m_lastProgressHeartbeat;
    if (staleTicks > 80U) {
        this->m_linkState = LinkState::DOWN;
    } else if (staleTicks > 20U) {
        this->m_linkState = LinkState::DEGRADED;
    } else {
        this->m_linkState = LinkState::LOCKED;
    }
}

void TeensyTransportManager::writeTelemetry() {
    this->tlmWrite_LinkHeartbeat(this->m_linkHeartbeat);
    this->tlmWrite_UplinkFrames(this->m_uplinkFrames);
    this->tlmWrite_DownlinkFrames(this->m_downlinkFrames);
}

bool TeensyTransportManager::shouldWriteTelemetry() const {
    constexpr U32 TELEMETRY_PERIOD_TICKS = 30U;
    return (this->m_linkHeartbeat - this->m_lastTelemetryHeartbeat) >= TELEMETRY_PERIOD_TICKS;
}

}  // namespace Components
