module Components {

  @ Driver tier for the Adafruit Mini GPS PA1010D, wired over UART.
  @
  @ Implements the GpsManager contract (Types/Gps.fpp) against the module's
  @ NMEA output. Bytes arrive from a byte stream driver, so the same component
  @ runs against any driver that speaks Drv.ByteStreamDriver.
  @
  @ The module has no enable line. "Off" is its PMTK standby mode, entered and
  @ left with sentences sent over the same UART. The module does not
  @ acknowledge either, so whether it is talking is still observed: no
  @ sentences within the staleness window means it is treated as off.
  passive component GpsDriver_AdafruitMiniGps {

    # ----------------------------------------------------------------------
    # Byte stream interface
    # ----------------------------------------------------------------------

    @ NMEA bytes in from the UART driver, buffers back out to it
    import Drv.ByteStreamDriverClient

    # ----------------------------------------------------------------------
    # Manager interface (GpsManager)
    # ----------------------------------------------------------------------

    @ Put the module in standby or wake it
    sync input port powerRequestIn: Components.GpsPowerRequest

    @ Read the latest fix
    sync input port readingGet: Components.GpsReadingGet

    # ----------------------------------------------------------------------
    # Scheduling
    # ----------------------------------------------------------------------

    @ Rate group input: ages the last sentence so staleness is detected
    sync input port run: Svc.Sched

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ GGA sentences accepted since boot
    telemetry SentencesParsed: U32

    @ Sentences dropped for a bad checksum or a bad field since boot
    telemetry SentenceErrors: U32 update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ A sentence was longer than the line buffer and was dropped.
    @ Persistent overflow means the wrong baud rate.
    event SentenceOverflow \
      severity warning low \
      format "GPS sentence exceeded the line buffer and was dropped" \
      throttle 5

    @ The UART driver reported a receive error
    event ReceiveError(
                        status: Drv.ByteStreamStatus @< status from the byte stream driver
                      ) \
      severity warning low \
      format "GPS byte stream receive error: {}" \
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
