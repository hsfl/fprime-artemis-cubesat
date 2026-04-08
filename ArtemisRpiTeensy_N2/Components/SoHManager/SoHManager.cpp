#include "Components/SoHManager/SoHManager.hpp"

namespace Components {

SoHManager::SoHManager(const char* const compName)
    : SoHManagerComponentBase(compName),
      m_status{} {}

SoHManager::~SoHManager() {}

void SoHManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void SoHManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);

    const U32 overall = this->m_status[SLOT_EPS] + this->m_status[SLOT_PAYLOAD] + this->m_status[SLOT_ADCS] +
                        this->m_status[SLOT_GPS] + this->m_status[SLOT_STORAGE] + this->m_status[SLOT_COMMS] +
                        this->m_status[SLOT_TRANSPORT];

    this->tlmWrite_OverallHealth(overall);
    this->tlmWrite_EpsHealth(this->m_status[SLOT_EPS]);
    this->tlmWrite_PayloadHealth(this->m_status[SLOT_PAYLOAD]);
    this->tlmWrite_AdcsHealth(this->m_status[SLOT_ADCS]);
    this->tlmWrite_GpsHealth(this->m_status[SLOT_GPS]);
    this->tlmWrite_StorageHealth(this->m_status[SLOT_STORAGE]);
    this->tlmWrite_CommsHealth(this->m_status[SLOT_COMMS]);
    this->tlmWrite_TransportHealth(this->m_status[SLOT_TRANSPORT]);
}

void SoHManager::statusIn_handler(FwIndexType portNum, U32 key) {
    if (portNum < SLOT_COUNT) {
        this->m_status[portNum] = key;
    }
}

void SoHManager::EMIT_SOH_SNAPSHOT_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    const U32 overall = this->m_status[SLOT_EPS] + this->m_status[SLOT_PAYLOAD] + this->m_status[SLOT_ADCS] +
                        this->m_status[SLOT_GPS] + this->m_status[SLOT_STORAGE] + this->m_status[SLOT_COMMS] +
                        this->m_status[SLOT_TRANSPORT];
    this->log_ACTIVITY_HI_Snapshot(
        overall,
        this->m_status[SLOT_EPS],
        this->m_status[SLOT_PAYLOAD],
        this->m_status[SLOT_ADCS],
        this->m_status[SLOT_GPS],
        this->m_status[SLOT_STORAGE],
        this->m_status[SLOT_COMMS],
        this->m_status[SLOT_TRANSPORT]);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Components
