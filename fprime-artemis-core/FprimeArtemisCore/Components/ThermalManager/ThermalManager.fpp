module Components {

  @ Manager tier for the flight controller's board temperatures.
  @
  @ Owns the hardware-independent contract: reading every sensor, deciding
  @ whether the spacecraft is NOMINAL, COLD, or HOT, and publishing the
  @ temperatures. Every read goes through a thermal driver (Types/Thermal.fpp),
  @ so swapping the sensors means swapping the driver instance only.
  @
  @ No heater is driven yet. SET_THERMAL_MODE records intent only.
  @
  @ Inputs and commands are guarded: commands run on the dispatcher's thread
  @ and run on the rate group's.
  passive component ThermalManager {

    # ----------------------------------------------------------------------
    # Driver interface
    # ----------------------------------------------------------------------

    @ Read every sensor
    output port driverReadingGet: Components.ThermalReadingGet

    # ----------------------------------------------------------------------
    # Scheduling
    # ----------------------------------------------------------------------

    @ Rate group input: read the sensors and update the state
    guarded input port run: Svc.Sched

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Read the sensors now and report the state as an event
    guarded command REQUEST_THERMAL_STATUS

    @ Record the requested thermal control mode.
    @ Records intent only: no heater is driven.
    guarded command SET_THERMAL_MODE(
                                      mode: Components.ThermalMode @< the requested mode
                                    )

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ NO_DATA, NOMINAL, COLD, or HOT
    telemetry ThermalState: Components.ThermalState update on change

    @ Degrees C per sensor, indexed by ThermalSensor. Only entries set in
    @ ValidSensorMask are real readings.
    telemetry Temperatures: Components.ThermalTemperatures

    @ Bit i set when sensor i read inside its rated range
    telemetry ValidSensorMask: U8 update on change

    @ Requested control mode (intent only)
    telemetry ThermalMode: Components.ThermalMode update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The state changed
    event ThermalStateChanged(
                               $state: Components.ThermalState @< the new state
                             ) \
      severity activity high \
      format "Thermal state is now {}"

    @ A sensor started or stopped reading inside its rated range
    event SensorValidityChanged(
                                 sensor: Components.ThermalSensor @< the sensor
                                 valid: bool @< whether it now reads inside its rated range
                               ) \
      severity warning low \
      format "Thermal sensor {} valid: {}"

    @ The requested control mode changed
    event ThermalModeUpdated(
                              mode: Components.ThermalMode @< the new mode
                            ) \
      severity activity high \
      format "Thermal mode set to {} (intent only, no heater is driven)"

    @ Response to REQUEST_THERMAL_STATUS
    event ThermalStatusReport(
                               $state: Components.ThermalState @< the current state
                               validMask: U8 @< bit i set when sensor i is valid
                               minTemp: F32 @< coldest valid sensor, degrees C
                               maxTemp: F32 @< hottest valid sensor, degrees C
                             ) \
      severity activity low \
      format "Thermal state: {}, valid sensors: 0x{x}, min {.1f} C, max {.1f} C"

    ##########################################################
    # Standard AC ports
    ##########################################################
    @ Port for requesting the current time
    time get port timeCaller

    @ Port for sending command registrations
    command reg port cmdRegOut

    @ Port for receiving commands
    command recv port cmdIn

    @ Port for sending command responses
    command resp port cmdResponseOut

    @ Port for emitting telemetry
    telemetry port tlmOut

    @ Port for sending textual representation of events
    text event port logTextOut

    @ Port for sending events to downlink
    event port logOut

  }

}
