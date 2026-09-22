@ Contract between ImuManager and any IMU driver.
@
@ Nothing here names a chip, bus, or register: a new IMU driver implements these
@ ports and ImuManager does not change.
module Components {

  # ----------------------------------------------------------------------
  # Data types
  # ----------------------------------------------------------------------

  @ Three-axis vector
  struct Vector3 {
    x: F32 @< X component
    y: F32 @< Y component
    z: F32 @< Z component
  }

  @ One IMU sample, in SI units
  struct ImuReading {
    acceleration: Vector3 @< Linear acceleration in m/s^2
    angularRate: Vector3 @< Angular rate in rad/s
    temperature: F32 @< Sensor die temperature in degrees C
  }

  @ Result of a driver read
  enum ImuReadStatus: U8 {
    @ The reading is valid
    OK = 0
    @ The IMU is off; the bus was not touched
    POWERED_OFF = 1
    @ The bus transfer failed; the reading is not valid
    BUS_ERROR = 2
  }

  @ IMU state as reported by ImuManager
  enum ImuState: U8 {
    @ Power-down; no readings
    OFF = 0
    @ Sampling; readings are current
    ON = 1
    @ Requested on but not responding; no readings
    FAULT = 2
  }

  # ----------------------------------------------------------------------
  # Port types
  # ----------------------------------------------------------------------

  @ Turn the IMU on or off.
  @ Returns SUCCESS if the request was carried out.
  port ImuPowerRequest(
                        $state: Fw.On @< ON starts sampling, OFF enters power-down
                      ) -> Fw.Success

  @ Read the latest IMU sample into reading.
  @ reading is valid only when the return value is OK.
  port ImuReadingGet(
                      ref reading: Components.ImuReading @< Filled with the latest sample
                    ) -> Components.ImuReadStatus

}
