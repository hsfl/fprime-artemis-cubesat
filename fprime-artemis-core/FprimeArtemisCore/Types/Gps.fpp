@ Contract between GpsManager and any GPS driver.
@
@ Nothing here names a module, bus, or sentence format: a new GPS driver
@ implements these ports and GpsManager does not change.
module Components {

  # ----------------------------------------------------------------------
  # Data types
  # ----------------------------------------------------------------------

  @ One position fix
  struct GpsFix {
    latitude: F64 @< Degrees, positive north
    longitude: F64 @< Degrees, positive east
    altitude: F32 @< Meters above mean sea level
    satellites: U8 @< Satellites used in the fix
    utcSeconds: U32 @< Seconds since UTC midnight, from the fix itself
  }

  @ Result of a driver read
  enum GpsReadStatus: U8 {
    @ The fix is valid
    OK = 0
    @ The module is talking but has not locked onto satellites; fix is not valid
    NO_FIX = 1
    @ Nothing has arrived recently: unpowered, unplugged, or miswired
    NO_DATA = 2
  }

  @ GPS state as reported by GpsManager
  enum GpsState: U8 {
    @ No sentences arriving; treat the module as off
    OFF = 0
    @ Sentences arriving, no satellite lock yet
    ACQUIRING = 1
    @ Locked; the fix is current
    READY = 2
  }

  # ----------------------------------------------------------------------
  # Port types
  # ----------------------------------------------------------------------

  @ Put the GPS in standby or wake it.
  @ Returns SUCCESS if the request was sent. The module does not acknowledge
  @ it: whether it obeyed shows up as sentences stopping or resuming.
  port GpsPowerRequest(
                        $state: Fw.On @< ON wakes the module, OFF puts it in standby
                      ) -> Fw.Success

  @ Read the latest GPS fix into fix.
  @ fix is valid only when the return value is OK.
  port GpsReadingGet(
                      ref fix: Components.GpsFix @< Filled with the latest fix
                    ) -> Components.GpsReadStatus

}
