module Components {

  @ Driver tier for the ST LSM6DSOX 6-axis IMU (Adafruit LSM6DSOX+LIS3MDL breakout).
  @
  @ Implements the ImuManager contract (Types/Imu.fpp) against the LSM6DSOX
  @ register map. Every bus transfer goes through the Drv.I2c ports, so the same
  @ component runs against any bus driver that speaks Drv.I2c.
  @
  @ "Off" is the chip's power-down mode (ODR = 0): the chip keeps power and still
  @ answers on I2C.
  passive component ImuDriver_LSM6DSOX {

    # ----------------------------------------------------------------------
    # Manager interface (ImuManager)
    # ----------------------------------------------------------------------

    @ Turn the IMU on (reset + configure) or off (power-down)
    sync input port powerRequestIn: Components.ImuPowerRequest

    @ Read the latest sample, converted to SI units
    sync input port readingGet: Components.ImuReadingGet

    # ----------------------------------------------------------------------
    # Bus interface
    # ----------------------------------------------------------------------

    @ Register reads: write the register address, then read
    output port busWriteRead: Drv.I2cWriteRead

    @ Register writes: register address followed by the value
    output port busWrite: Drv.I2c

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Whether the chip is sampling
    telemetry PowerState: Fw.On update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The chip was turned on or off
    event PowerStateChanged(
                             $state: Fw.On @< the new state
                           ) \
      severity activity high \
      format "LSM6DSOX power {}"

    @ WHO_AM_I did not identify an LSM6DSOX
    event ChipIdMismatch(
                          expected: U8 @< the LSM6DSOX chip ID
                          actual: U8 @< the value read
                        ) \
      severity warning high \
      format "LSM6DSOX WHO_AM_I mismatch: expected {x}, read {x}"

    @ The chip did not finish its software reset in time
    event ResetTimeout \
      severity warning high \
      format "LSM6DSOX software reset did not complete"

    @ A bus transfer failed
    event I2cError(
                    regAddr: U8 @< the register being accessed
                    status: Drv.I2cStatus @< status returned by the bus driver
                  ) \
      severity warning high \
      format "LSM6DSOX I2C error at register {x}: {}" \
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
