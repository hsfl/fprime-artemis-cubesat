module Components {

  @ Manager tier for the flight controller's GPS.
  @
  @ Owns the hardware-independent contract: reporting whether the module is
  @ powered and talking, whether it has a satellite lock, and publishing its
  @ fixes. Every hardware action goes through a GPS driver (Types/Gps.fpp), so
  @ swapping the GPS means swapping the driver instance only.
  @
  @ The Adafruit Mini GPS has no enable line and no software power-down, so
  @ "powered" is observed rather than commanded: sentences arriving means
  @ powered, silence means not.
  @
  @ Inputs and commands are guarded: commands run on the dispatcher's thread
  @ and run on the rate group's.
  passive component GpsManager {

    # ----------------------------------------------------------------------
    # Driver interface
    # ----------------------------------------------------------------------

    @ Read the latest fix
    output port driverReadingGet: Components.GpsReadingGet

    # ----------------------------------------------------------------------
    # Scheduling
    # ----------------------------------------------------------------------

    @ Rate group input: read the fix and update the state
    guarded input port run: Svc.Sched

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Report the current state, satellite count, and last fix as events
    guarded command GET_GPS_STATUS

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ OFF, ACQUIRING, or READY. The fix is current only while READY.
    telemetry GpsState: Components.GpsState update on change

    @ Whether the module is powered and talking
    telemetry GpsPowered: Fw.On update on change

    @ Degrees, positive north, updated each tick while READY
    telemetry Latitude: F64

    @ Degrees, positive east, updated each tick while READY
    telemetry Longitude: F64

    @ Meters above mean sea level, updated each tick while READY
    telemetry Altitude: F32

    @ Satellites used in the fix, updated each tick while READY
    telemetry Satellites: U8

    @ Seconds since UTC midnight, from the fix itself, updated while READY
    telemetry GpsUtcSeconds: U32

    @ Fixes lost since boot
    telemetry FixLostCount: U32 update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The state changed
    event GpsStateChanged(
                           $state: Components.GpsState @< the new state
                         ) \
      severity activity high \
      format "GPS state is now {}"

    @ The GPS locked onto satellites: fixes are now valid
    event GpsReady(
                    satellites: U8 @< satellites used in the fix
                  ) \
      severity activity high \
      format "GPS ready: fix acquired with {} satellites"

    @ The GPS lost its lock, or went silent: fixes are no longer valid
    event GpsNotReady(
                       $state: Components.GpsState @< OFF if silent, ACQUIRING if still talking
                     ) \
      severity warning low \
      format "GPS not ready: state is now {}"

    @ Response to GET_GPS_STATUS
    event GpsStatusReport(
                           $state: Components.GpsState @< the current state
                           satellites: U8 @< satellites in the last fix
                           fixesLost: U32 @< fixes lost since boot
                         ) \
      severity activity low \
      format "GPS state: {}, satellites: {}, fixes lost since boot: {}"

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
