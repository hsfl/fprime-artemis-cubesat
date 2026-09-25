module Components {

  @ Millivolts per sensor, indexed by ThermalSensor
  array Tmp36Millivolts = [THERMAL_SENSOR_COUNT] U32

  @ Driver tier for the board's TMP36 analog temperature sensors.
  @
  @ Implements the ThermalManager contract (Types/Thermal.fpp). Each sensor
  @ is one ADC channel, read through its own Zephyr.ZephyrADCDriver: port
  @ index i is sensor i, in ThermalSensor order.
  @
  @ A read is synchronous: adcRead[i] makes the ADC driver convert and call
  @ adcMvIn[i] back before adcRead[i] returns. adcMvIn is sync, not guarded,
  @ because it runs inside readingGet on the same thread.
  passive component ThermalDriver_TMP36 {

    # ----------------------------------------------------------------------
    # Manager interface (ThermalManager)
    # ----------------------------------------------------------------------

    @ Read every sensor
    sync input port readingGet: Components.ThermalReadingGet

    # ----------------------------------------------------------------------
    # ADC interface (one Zephyr.ZephyrADCDriver per sensor)
    # ----------------------------------------------------------------------

    @ Ask ADC driver i for one conversion
    output port adcRead: [THERMAL_SENSOR_COUNT] Fw.Signal

    @ Millivolts back from ADC driver i
    sync input port adcMvIn: [THERMAL_SENSOR_COUNT] Zephyr.ADCMvValue

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ ADC pin voltage in mV, after the 45.3k/10k divider and before
    @ conversion. Bring-up diagnostic: ~136 mV at room temperature (750 mV
    @ at the TMP36), 0 mV for a failed conversion.
    telemetry SensorMillivolts: Components.Tmp36Millivolts

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ ADC driver i did not answer a read. This is a topology wiring fault,
    @ not a sensor fault.
    event AdcNoResponse(
                         sensor: Components.ThermalSensor @< the sensor whose ADC did not answer
                       ) \
      severity warning high \
      format "No ADC response for thermal sensor {}" \
      throttle 5

    ##########################################################
    # Standard AC ports
    ##########################################################
    @ Port for requesting the current time
    time get port timeCaller

    @ Port for emitting telemetry
    telemetry port tlmOut

    @ Port for sending textual representation of events
    text event port logTextOut

    @ Port for sending events to downlink
    event port logOut

  }

}
