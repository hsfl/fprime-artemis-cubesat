// ======================================================================
// \title  ThermalManager.cpp
// \brief  cpp file for ThermalManager component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/ThermalManager/ThermalManager.hpp"

namespace Components {

ThermalManager::ThermalManager(const char* const compName) : ThermalManagerComponentBase(compName) {}

ThermalManager::~ThermalManager() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

void ThermalManager::run_handler(FwIndexType portNum, U32 context) {
    this->m_ticksSinceRead++;
    if (this->m_ticksSinceRead < RUN_TICKS_PER_READ) {
        return;
    }
    this->m_ticksSinceRead = 0;
    this->readOnce();
}

// ----------------------------------------------------------------------
// Command handlers
// ----------------------------------------------------------------------

void ThermalManager::REQUEST_THERMAL_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->readOnce();

    const U8 validMask = this->m_reading.get_validMask();
    const ThermalTemperatures& temperatures = this->m_reading.get_temperatures();
    F32 minTemp = 0.0f;
    F32 maxTemp = 0.0f;
    bool any = false;
    for (FwSizeType i = 0; i < THERMAL_SENSOR_COUNT; i++) {
        if ((validMask & (1U << i)) == 0) {
            continue;
        }
        const F32 t = temperatures[i];
        minTemp = (!any || (t < minTemp)) ? t : minTemp;
        maxTemp = (!any || (t > maxTemp)) ? t : maxTemp;
        any = true;
    }

    this->log_ACTIVITY_LO_ThermalStatusReport(this->m_state, validMask, minTemp, maxTemp);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ThermalManager::SET_THERMAL_SENSOR_cmdHandler(FwOpcodeType opCode,
                                                   U32 cmdSeq,
                                                   const ThermalSensor& sensor,
                                                   const Fw::On& state) {
    const U8 bit = static_cast<U8>(1U << static_cast<U8>(sensor.e));
    if (state == Fw::On::ON) {
        this->m_enabledMask = static_cast<U8>(this->m_enabledMask | bit);
        // Seed it valid, as at boot, so the next read reports it only if broken
        this->m_lastValidMask = static_cast<U8>(this->m_lastValidMask | bit);
    } else {
        this->m_enabledMask = static_cast<U8>(this->m_enabledMask & ~bit);
        // Disabling is not a sensor failure: forget its validity so the next
        // read does not report it as SensorValidityChanged(false)
        this->m_lastValidMask = static_cast<U8>(this->m_lastValidMask & ~bit);
    }
    this->log_ACTIVITY_HI_SensorEnableChanged(sensor, state);
    this->tlmWrite_EnabledSensorMask(this->m_enabledMask);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void ThermalManager::SET_THERMAL_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const ThermalMode& mode) {
    // Intent only: nothing downstream acts on the mode until a heater driver exists
    this->m_mode = mode;
    this->log_ACTIVITY_HI_ThermalModeUpdated(mode);
    this->tlmWrite_ThermalMode(mode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------

void ThermalManager::readOnce() {
    ThermalReading reading;
    const ThermalReadStatus status = this->driverReadingGet_out(0, reading);
    // NO_DATA still carries a valid (empty) mask, so the per-sensor
    // bookkeeping below is the same either way.
    // A disabled sensor is treated as not there: out of the mask, out of the
    // state decision, and zeroed so no stale-looking value is downlinked.
    const U8 validMask =
        (status == ThermalReadStatus::OK) ? static_cast<U8>(reading.get_validMask() & this->m_enabledMask) : 0;
    reading.set_validMask(validMask);
    ThermalTemperatures temperatures = reading.get_temperatures();
    for (FwSizeType i = 0; i < THERMAL_SENSOR_COUNT; i++) {
        if ((this->m_enabledMask & (1U << i)) == 0) {
            temperatures[i] = 0.0f;
        }
    }
    reading.set_temperatures(temperatures);
    this->m_reading = reading;

    const U8 changed = static_cast<U8>(validMask ^ this->m_lastValidMask);
    for (FwSizeType i = 0; i < THERMAL_SENSOR_COUNT; i++) {
        if ((changed & (1U << i)) != 0) {
            this->log_WARNING_LO_SensorValidityChanged(static_cast<ThermalSensor::T>(i),
                                                       (validMask & (1U << i)) != 0);
        }
    }
    this->m_lastValidMask = validMask;

    ThermalState state = ThermalState::NO_DATA;
    if (validMask != 0) {
        bool cold = false;
        bool hot = false;
        for (FwSizeType i = 0; i < THERMAL_SENSOR_COUNT; i++) {
            if ((validMask & (1U << i)) != 0) {
                cold = cold || (temperatures[i] < COLD_LIMIT_C);
                hot = hot || (temperatures[i] > HOT_LIMIT_C);
            }
        }
        state = hot ? ThermalState::HOT : (cold ? ThermalState::COLD : ThermalState::NOMINAL);
        // Temperatures are published only when something is real. With no
        // valid sensor the last values stay on the ground display with an old
        // timestamp; ThermalState says they are stale.
        this->tlmWrite_Temperatures(temperatures);
    }

    this->setState(state);
    this->tlmWrite_ThermalState(this->m_state);
    this->tlmWrite_ValidSensorMask(validMask);
    this->tlmWrite_EnabledSensorMask(this->m_enabledMask);
    this->tlmWrite_ThermalMode(this->m_mode);
}

void ThermalManager::setState(ThermalState state) {
    if (state != this->m_state) {
        this->m_state = state;
        this->log_ACTIVITY_HI_ThermalStateChanged(state);
    }
}

}  // namespace Components
