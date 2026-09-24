module Components {

  @ Driver tier for the Adafruit Mini GPS PA1010D, wired over UART.
  @
  @ Implements the GpsManager contract (Types/Gps.fpp) against the module's
  @ NMEA output. Bytes arrive from a byte stream driver, so the same component
  @ runs against any driver that speaks Drv.ByteStreamDriver.
  @
  @ The module has no enable line and no software power-down: it talks as soon
  @ as it has power. "Not powered" is therefore observed, not commanded --
  @ no sentences within the staleness window means the module is treated as off.
  passive component GpsDriver_AdafruitMiniGps {

    # ----------------------------------------------------------------------
    # Byte stream interface
    # ----------------------------------------------------------------------

    @ NMEA bytes in from the UART driver, buffers back out to it
    import Drv.ByteStreamDriverClient

    # ----------------------------------------------------------------------
    # Manager interface (GpsManager)
    # ----------------------------------------------------------------------

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
