// ======================================================================
// \title  GpsManager.cpp
// \brief  cpp file for GpsManager component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/GpsManager/GpsManager.hpp"

namespace Components {

GpsManager::GpsManager(const char* const compName) : GpsManagerComponentBase(compName) {}

GpsManager::~GpsManager() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void GpsManager::run_handler(FwIndexType portNum, U32 context) {
    // The GPS is on by default. Standby survives a Teensy reset, so a module
    // put to sleep before the reset must be woken, not assumed awake.
    if (!this->m_bootPowerOnDone) {
        this->m_bootPowerOnDone = true;
        (void)this->applyPower(Fw::On::ON);
    }

    // Read whatever the commanded state: a module that ignored a standby
    // request shows up as GpsPowered OFF with sentences still arriving.
    GpsFix fix;
    const GpsReadStatus status = this->driverReadingGet_out(0, fix);

    switch (status.e) {
        case GpsReadStatus::OK:
            this->m_fix = fix;
            this->m_hasEverFixed = true;
            this->setState(GpsState::READY);
            this->tlmWrite_Latitude(fix.get_latitude());
            this->tlmWrite_Longitude(fix.get_longitude());
            this->tlmWrite_Altitude(fix.get_altitude());
            this->tlmWrite_Satellites(fix.get_satellites());
            this->tlmWrite_GpsUtcSeconds(fix.get_utcSeconds());
            break;
        case GpsReadStatus::NO_FIX:
            // Talking but not locked. Position channels are left alone: the
            // last values stay on the ground display with an old timestamp
            // rather than being replaced by zeros.
            this->setState(GpsState::ACQUIRING);
            break;
        default:
            // NO_DATA: nothing is driving the line, so the module is unpowered,
            // unplugged, or miswired.
            this->setState(GpsState::OFF);
            break;
    }

    this->tlmWrite_GpsState(this->m_state);
    this->tlmWrite_GpsPowered(this->m_power);
    this->tlmWrite_FixLostCount(this->m_fixLostCount);
}

Fw::Success GpsManager::powerRequestIn_handler(FwIndexType portNum, const Fw::On& state) {
    return this->applyPower(state);
}

// ----------------------------------------------------------------------
// Command handlers
// ----------------------------------------------------------------------

void GpsManager::SET_GPS_POWER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const Fw::On& state) {
    const Fw::Success status = this->applyPower(state);
    this->cmdResponse_out(opCode, cmdSeq,
                          (status == Fw::Success::SUCCESS) ? Fw::CmdResponse::OK
                                                           : Fw::CmdResponse::EXECUTION_ERROR);
}

void GpsManager::GET_GPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    const U8 satellites = this->m_hasEverFixed ? this->m_fix.get_satellites() : 0;
    this->log_ACTIVITY_LO_GpsStatusReport(this->m_state, this->m_power, satellites, this->m_fixLostCount);
    this->tlmWrite_GpsState(this->m_state);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

Fw::Success GpsManager::applyPower(const Fw::On& state) {
    // No redundant-request check: resending a wake is harmless, and it is the
    // retry when GpsPowered is ON but the module is silent.
    const Fw::Success status = this->driverPowerOut_out(0, state);
    if (status != Fw::Success::SUCCESS) {
        this->log_WARNING_HI_GpsPowerFailed(state);
        return Fw::Success::FAILURE;
    }
    this->m_power = state;
    this->tlmWrite_GpsPowered(state);
    return Fw::Success::SUCCESS;
}

void GpsManager::setState(GpsState state) {
    if (state == this->m_state) {
        return;
    }

    const bool wasReady = (this->m_state == GpsState::READY);
    this->m_state = state;
    this->log_ACTIVITY_HI_GpsStateChanged(state);
    this->tlmWrite_GpsState(state);

    // The ready/not-ready pair is the notification the rest of the flight
    // software watches; the state change above is the audit trail.
    if (state == GpsState::READY) {
        this->log_ACTIVITY_HI_GpsReady(this->m_fix.get_satellites());
    } else if (wasReady && (this->m_power == Fw::On::ON)) {
        // A fix dropped by a commanded standby was not lost
        this->m_fixLostCount++;
        this->log_WARNING_LO_GpsNotReady(state);
        this->tlmWrite_FixLostCount(this->m_fixLostCount);
    }
}

}  // namespace Components
