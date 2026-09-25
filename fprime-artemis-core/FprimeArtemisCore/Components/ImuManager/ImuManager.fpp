module Components {

  @ Manager tier for the flight controller's IMU.
  @
  @ Owns the hardware-independent contract: turning the IMU on and off,
  @ reporting whether it is on, off, or faulted, and publishing its readings.
  @ Every hardware action goes through an IMU driver (Types/Imu.fpp), so
  @ swapping the IMU means swapping the driver instance only.
  @
  @ Inputs and commands are guarded: commands run on the dispatcher's thread
  @ and run on the rate group's, and the driver's multi-transfer power-up must
  @ not interleave with a reading.
  passive component ImuManager {

    # ----------------------------------------------------------------------
    # Application interface (MissionApp)
    # ----------------------------------------------------------------------

    @ Turn the IMU on or off. This is the operator path once MissionApp drives
    @ it; SET_IMU_POWER is the engineering equivalent.
    guarded input port powerRequestIn: Components.ImuPowerRequest

    # ----------------------------------------------------------------------
    # Driver interface
    # ----------------------------------------------------------------------

    @ Power the IMU up or down
    output port driverPowerOut: Components.ImuPowerRequest

    @ Read the latest sample
    output port driverReadingGet: Components.ImuReadingGet

    # ----------------------------------------------------------------------
    # Scheduling
    # ----------------------------------------------------------------------

    @ Rate group input: boot power-on, reading, and fault detection
    guarded input port run: Svc.Sched

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Engineering command: turn the IMU on or off.
    @ Not part of routine operations once MissionApp drives the IMU.
    guarded command SET_IMU_POWER(
                                   $state: Fw.On @< ON starts sampling, OFF enters power-down
                                 )

    @ Report the current state and read error count as an event
    guarded command GET_IMU_STATUS

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ OFF, ON, or FAULT. Readings are current only while ON.
    telemetry ImuState: Components.ImuState update on change

    @ Linear acceleration in m/s^2, updated each tick while ON
    telemetry Acceleration: Components.Vector3

    @ Angular rate in rad/s, updated each tick while ON
    telemetry AngularRate: Components.Vector3

    @ Sensor die temperature in degrees C (not the board), updated each tick while ON
    telemetry ImuTemperature: F32

    @ Failed reads since boot
    telemetry ReadErrors: U32 update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The state changed
    event ImuStateChanged(
                           $state: Components.ImuState @< the new state
                         ) \
      severity activity high \
      format "IMU state is now {}"

    @ The driver could not carry out a power request
    event ImuPowerFailed(
                          $state: Fw.On @< the requested state
                        ) \
      severity warning high \
      format "IMU power {} request failed"

    @ Reads kept failing; the IMU is now FAULT
    event ImuFaulted(
                      consecutiveErrors: U32 @< failed reads in a row
                    ) \
      severity warning high \
      format "IMU faulted after {} consecutive failed reads"

    @ Response to GET_IMU_STATUS
    event ImuStatusReport(
                           $state: Components.ImuState @< the current state
                           readErrors: U32 @< failed reads since boot
                         ) \
      severity activity low \
      format "IMU state: {}, read errors since boot: {}"

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
