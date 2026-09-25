// ======================================================================
// \title  ThermalManager.hpp
// \brief  hpp file for ThermalManager component implementation class
// ======================================================================

#ifndef Components_ThermalManager_HPP
#define Components_ThermalManager_HPP

#include "FprimeArtemisCore/Components/ThermalManager/ThermalManagerComponentAc.hpp"
#include "FprimeArtemisCore/Types/FppConstantsAc.hpp"

namespace Components {

class ThermalManager final : public ThermalManagerComponentBase {
  public:
    explicit ThermalManager(const char* const compName);
    ~ThermalManager();

    //! Placeholder limits in degrees C, applied to every valid sensor.
    //! TODO(TCS owner): set per-sensor limits from the thermal analysis.
    static constexpr F32 COLD_LIMIT_C = -10.0f;
    static constexpr F32 HOT_LIMIT_C = 60.0f;

    //! run ticks per read. run is on the 10Hz group, so 5 reads every 0.5 s.
    static constexpr U32 RUN_TICKS_PER_READ = 5;

  private:
    // ----------------------------------------------------------------------
    // Handler implementations for typed input ports
    // ----------------------------------------------------------------------

    //! Read the sensors and update the state every RUN_TICKS_PER_READ ticks
    void run_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Handler implementations for commands
    // ----------------------------------------------------------------------

    //! Read now and report the state as an event
    void REQUEST_THERMAL_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    //! Enable or disable one sensor
    void SET_THERMAL_SENSOR_cmdHandler(FwOpcodeType opCode,
                                       U32 cmdSeq,
                                       const ThermalSensor& sensor,
                                       const Fw::On& state) override;

    //! Record the requested mode (intent only)
    void SET_THERMAL_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, const ThermalMode& mode) override;

    // ----------------------------------------------------------------------
    // Helpers
    // ----------------------------------------------------------------------

    //! Read the driver, publish telemetry, and update the state
    void readOnce();

    //! Set the state, emitting ThermalStateChanged on a change
    void setState(ThermalState state);

    //! Current state. Nothing has been read at boot.
    ThermalState m_state = ThermalState::NO_DATA;

    //! Requested mode (intent only)
    ThermalMode m_mode = ThermalMode::OBSERVE;

    //! Last reading, for the status report
    ThermalReading m_reading;

    //! Bit i set when sensor i is enabled. Every sensor is enabled at boot.
    U8 m_enabledMask = static_cast<U8>((1U << THERMAL_SENSOR_COUNT) - 1U);

    //! Valid sensors on the previous read, for SensorValidityChanged.
    //! Seeded all-valid so the first read reports only missing sensors.
    U8 m_lastValidMask = static_cast<U8>((1U << THERMAL_SENSOR_COUNT) - 1U);

    //! run ticks since the last read. Starts due so the first tick reads.
    U32 m_ticksSinceRead = RUN_TICKS_PER_READ;
};

}  // namespace Components

#endif
