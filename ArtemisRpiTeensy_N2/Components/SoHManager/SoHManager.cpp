#include "Components/SoHManager/SoHManager.hpp"

namespace Components {

SoHManager::SoHManager(const char* const compName)
    : SoHManagerComponentBase(compName),
      m_status{},
      m_detail{} {
    for (U32 slot = 0; slot < SLOT_COUNT; ++slot) {
        this->m_status[slot] = Components::HealthState::UNKNOWN;
    }
}

SoHManager::~SoHManager() {}

void SoHManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void SoHManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);

    const Components::HealthState overall = this->overallHealth();

    this->tlmWrite_OverallHealth(overall);
    this->tlmWrite_EpsHealth(this->m_status[SLOT_EPS]);
    this->tlmWrite_PayloadHealth(this->m_status[SLOT_PAYLOAD]);
    this->tlmWrite_AdcsHealth(this->m_status[SLOT_ADCS]);
    this->tlmWrite_GpsHealth(this->m_status[SLOT_GPS]);
    this->tlmWrite_StorageHealth(this->m_status[SLOT_STORAGE]);
    this->tlmWrite_ThermalHealth(this->m_status[SLOT_THERMAL]);
    this->tlmWrite_CommsHealth(this->m_status[SLOT_COMMS]);
    this->tlmWrite_TransportHealth(this->m_status[SLOT_TRANSPORT]);
}

void SoHManager::statusIn_handler(FwIndexType portNum, const Components::HealthState& health, U32 detail) {
    if (portNum < SLOT_COUNT) {
        this->m_status[portNum] = health;
        this->m_detail[portNum] = detail;
    }
}

void SoHManager::EMIT_SOH_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    const Components::HealthState overall = this->overallHealth();
    this->log_ACTIVITY_HI_Snapshot(
        overall,
        this->m_status[SLOT_EPS],
        this->m_status[SLOT_PAYLOAD],
        this->m_status[SLOT_ADCS],
        this->m_status[SLOT_GPS],
        this->m_status[SLOT_STORAGE],
        this->m_status[SLOT_THERMAL],
        this->m_status[SLOT_COMMS],
        this->m_status[SLOT_TRANSPORT]);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

Components::HealthState SoHManager::overallHealth() const {
    bool sawWarn = false;
    bool sawUnknown = false;
    for (U32 slot = 0; slot < SLOT_COUNT; ++slot) {
        const Components::HealthState health = this->m_status[slot];
        if (health == Components::HealthState::FAIL) {
            return Components::HealthState::FAIL;
        }
        if (health == Components::HealthState::WARN) {
            sawWarn = true;
        }
        if (health == Components::HealthState::UNKNOWN) {
            sawUnknown = true;
        }
    }
    if (sawWarn) {
        return Components::HealthState::WARN;
    }
    if (sawUnknown) {
        return Components::HealthState::UNKNOWN;
    }
    return Components::HealthState::OK;
}

}  // namespace Components
