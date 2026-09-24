// ======================================================================
// \title  ThermalDriver_TMP36.cpp
// \brief  cpp file for ThermalDriver_TMP36 component implementation class
// ======================================================================

#include "FprimeArtemisCore/Components/ThermalDriver_TMP36/ThermalDriver_TMP36.hpp"

namespace Components {

ThermalDriver_TMP36::ThermalDriver_TMP36(const char* const compName) : ThermalDriver_TMP36ComponentBase(compName) {}

ThermalDriver_TMP36::~ThermalDriver_TMP36() {}

// ----------------------------------------------------------------------
// Handler implementations for typed input ports
// ----------------------------------------------------------------------

ThermalReadStatus ThermalDriver_TMP36::readingGet_handler(FwIndexType portNum, ThermalReading& reading) {
    ThermalTemperatures temperatures;
    Tmp36Millivolts millivolts;
    U8 validMask = 0;

    for (FwSizeType i = 0; i < SENSOR_COUNT; i++) {
        const FwIndexType port = static_cast<FwIndexType>(i);

        // The ADC answers through adcMvIn before adcRead returns. Clearing the
        // flag first stops a missing answer from reusing last cycle's value.
        this->m_received[i] = false;
        if (this->isConnected_adcRead_OutputPort(port)) {
            this->adcRead_out(port);
        }
        if (!this->m_received[i]) {
            this->m_millivolts[i] = 0;
            this->log_WARNING_HI_AdcNoResponse(static_cast<ThermalSensor::T>(i));
        }

        const U32 mv = this->m_millivolts[i];
        millivolts[i] = mv;
        temperatures[i] = (static_cast<F32>(mv) - OFFSET_MV) / MV_PER_DEGREE_C;
        if ((mv >= MIN_VALID_MV) && (mv <= MAX_VALID_MV)) {
            validMask = static_cast<U8>(validMask | (1U << i));
        }
    }

    this->tlmWrite_SensorMillivolts(millivolts);

    reading.set_temperatures(temperatures);
    reading.set_validMask(validMask);
    return (validMask != 0) ? ThermalReadStatus::OK : ThermalReadStatus::NO_DATA;
}

void ThermalDriver_TMP36::adcMvIn_handler(FwIndexType portNum, U32 value) {
    FW_ASSERT((portNum >= 0) && (static_cast<FwSizeType>(portNum) < SENSOR_COUNT), portNum);
    this->m_millivolts[portNum] = value;
    this->m_received[portNum] = true;
}

}  // namespace Components
