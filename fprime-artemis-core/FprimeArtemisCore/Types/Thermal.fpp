@ Contract between ThermalManager and any thermal sensor driver.
@
@ Nothing here names a sensor part, bus, or ADC: a new thermal driver
@ implements these ports and ThermalManager does not change.
module Components {

  # ----------------------------------------------------------------------
  # Constants
  # ----------------------------------------------------------------------

  @ Number of board temperature sensors. Indexes follow ThermalSensor.
  constant THERMAL_SENSOR_COUNT = 7

  # ----------------------------------------------------------------------
  # Data types
  # ----------------------------------------------------------------------

  @ Where each sensor sits. The value is the sensor's index in every
  @ per-sensor array, and matches the temp_sensors io-channels order in
  @ boards/teensy41.overlay.
  enum ThermalSensor: U8 {
    OBC = 0 @< Flight computer board
    PDU = 1 @< Power distribution unit
    BATTERY = 2 @< Battery board
    SOLAR_1 = 3 @< Solar panel 1
    SOLAR_2 = 4 @< Solar panel 2
    SOLAR_3 = 5 @< Solar panel 3
    SOLAR_4 = 6 @< Solar panel 4
  }

  @ One temperature per sensor, in degrees C, indexed by ThermalSensor
  array ThermalTemperatures = [THERMAL_SENSOR_COUNT] F32

  @ One read of every sensor
  struct ThermalReading {
    temperatures: ThermalTemperatures @< Degrees C; meaningful only where validMask is set
    validMask: U8 @< Bit i set when sensor i read inside its rated range
  }

  @ Result of a driver read
  enum ThermalReadStatus: U8 {
    @ At least one sensor is valid; check validMask for which
    OK = 0
    @ No sensor read inside its rated range: unpowered, unplugged, or miswired
    NO_DATA = 1
  }

  @ Thermal state as reported by ThermalManager
  enum ThermalState: U8 {
    @ No valid sensor readings
    NO_DATA = 0
    @ Every valid sensor is inside the limits
    NOMINAL = 1
    @ At least one valid sensor is below the cold limit, none above the hot limit
    COLD = 2
    @ At least one valid sensor is above the hot limit
    HOT = 3
  }

  @ Requested thermal control mode. No heater is driven yet: the mode is
  @ recorded intent only.
  enum ThermalMode: U8 {
    OFF = 0 @< No thermal control
    OBSERVE = 1 @< Monitor only
    HEATER_AUTO = 2 @< Automatic heater control (not implemented)
  }

  # ----------------------------------------------------------------------
  # Port types
  # ----------------------------------------------------------------------

  @ Read every thermal sensor into reading.
  @ reading is meaningful only when the return value is OK.
  port ThermalReadingGet(
                          ref reading: Components.ThermalReading @< Filled with the latest temperatures
                        ) -> Components.ThermalReadStatus

}
