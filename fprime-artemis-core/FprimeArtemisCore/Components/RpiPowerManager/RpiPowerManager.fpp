module Components {

  @ Reported state of the Raspberry Pi payload computer
  enum RpiPowerState: U8 {
    @ Power rail disabled
    OFF = 0
    @ Power rail enabled; the payload computer has not reported in yet
    BOOT = 1
    @ The payload computer has reported in over the FC<->PC link
    READY = 2
  }

  @ Manager tier for Raspberry Pi payload-computer power.
  @
  @ Owns the hardware-independent contract for powering the payload computer up
  @ and down, and reports its state. The actual pin is driven through a
  @ Drv.Gpio driver, so swapping the power hardware means swapping the driver
  @ instance only.
  @
  @ OFF and BOOT are known from the rail alone. READY additionally requires the
  @ payload computer to report in, which arrives on peerAliveIn once the
  @ FC<->PC link carries status.
  passive component RpiPowerManager {

    # ----------------------------------------------------------------------
    # Driver interface
    # ----------------------------------------------------------------------

    @ Sets the payload computer power-enable pin
    output port gpioSet: Drv.GpioWrite

    # ----------------------------------------------------------------------
    # Peer interface
    # ----------------------------------------------------------------------

    @ Heartbeat from the payload computer, arriving over the FC<->PC link.
    @ Receiving one while powered promotes BOOT to READY.
    sync input port peerAliveIn: Svc.Ping

    # ----------------------------------------------------------------------
    # Scheduling
    # ----------------------------------------------------------------------

    @ Rate group input: ages the peer heartbeat and emits telemetry
    sync input port run: Svc.Sched

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Enable or disable the payload computer power rail
    sync command SET_RPI_POWER(
                                $state: Fw.On @< ON enables the rail, OFF disables it
                              )

    @ Report the current payload computer power state as an event
    sync command GET_RPI_STATUS

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Current payload computer power state
    telemetry RpiState: Components.RpiPowerState update on change

    @ Number of times the rail has been commanded on since boot
    telemetry RpiPowerCycles: U32 update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The power rail was commanded
    event RpiPowerCommanded(
                             $state: Fw.On @< the commanded rail state
                           ) \
      severity activity high \
      format "Payload computer power commanded {}"

    @ The reported state changed
    event RpiStateChanged(
                           $state: Components.RpiPowerState @< the new state
                         ) \
      severity activity high \
      format "Payload computer state is now {}"

    @ Response to GET_RPI_STATUS
    event RpiStatusReport(
                           $state: Components.RpiPowerState @< the current state
                         ) \
      severity activity low \
      format "Payload computer state: {}"

    @ The GPIO driver rejected a write. The rail state is unknown.
    event RpiGpioError(
                        status: Drv.GpioStatus @< status returned by the driver
                      ) \
      severity warning high \
      format "Payload computer power pin write failed: {}"

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
