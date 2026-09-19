module Components {

  @ Mission mode of the two-computer spacecraft
  enum MissionMode: U8 {
    @ Flight controller up, payload computer off. The mode at boot.
    STANDBY = 0
    @ Powering the payload computer and waiting for it to report READY
    ENTERING_BASE = 1
    @ Payload computer powered and reporting in
    BASE = 2
  }

  @ Why an ENTER_BASE_MODE attempt failed
  enum BaseModeFailure: U8 {
    @ RpiPowerManager rejected the power-on request
    POWER_REQUEST_FAILED = 0
    @ The payload computer did not report READY in time
    TIMEOUT = 1
    @ The payload computer was powered off during entry
    POWERED_OFF = 2
  }

  @ Application tier: the mission operator's interface.
  @
  @ A mission operator executes functions only through MissionApp. It owns the
  @ mission mode and drives managers through their port contracts; it never
  @ touches hardware directly.
  @
  @ Mode rules:
  @ - STANDBY means the payload computer is off. MissionApp enforces this by
  @   requesting power-off on entry, including at boot.
  @ - A downgrade that only makes the mode match reality is automatic: if the
  @   payload computer is powered off while in BASE, the mode drops to STANDBY.
  @ - An upgrade always goes through the ENTER_BASE_MODE sequence. Powering the
  @   payload computer on with an engineering command does not enter BASE.
  active component MissionApp {

    # ----------------------------------------------------------------------
    # Operator commands
    # ----------------------------------------------------------------------

    @ Power the payload computer and bring the spacecraft to BASE.
    @ Responds when the transition starts; progress is reported through
    @ ModeChanged events and CurrentMode telemetry.
    async command ENTER_BASE_MODE

    @ Power the payload computer off and return to STANDBY
    async command ENTER_STANDBY_MODE

    # ----------------------------------------------------------------------
    # Manager interface: RpiPowerManager
    # ----------------------------------------------------------------------

    @ Request payload computer power on or off
    output port rpiPowerRequestOut: Components.RpiPowerRequest

    @ Payload computer power state changes
    async input port rpiStateIn: Components.RpiPowerStateUpdate

    # ----------------------------------------------------------------------
    # Scheduling and health
    # ----------------------------------------------------------------------

    @ 1Hz rate group input: base-mode entry timeout. Must be on a 1Hz group;
    @ BASE_ENTRY_TIMEOUT_TICKS counts ticks as seconds.
    async input port run: Svc.Sched

    @ Health ping input
    async input port pingIn: Svc.Ping

    @ Health ping output
    output port pingOut: Svc.Ping

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Current mission mode
    telemetry CurrentMode: Components.MissionMode update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The mission mode changed
    event ModeChanged(
                       mode: Components.MissionMode @< the new mode
                     ) \
      severity activity high \
      format "Mission mode is now {}"

    @ An ENTER_BASE_MODE attempt failed and the mode returned to STANDBY
    event BaseModeFailed(
                          reason: Components.BaseModeFailure @< why entry failed
                        ) \
      severity warning high \
      format "Base mode entry failed: {}. Returned to STANDBY"

    @ A mode command was rejected because a transition is in progress
    event ModeCommandRejected(
                               mode: Components.MissionMode @< the mode at the time
                             ) \
      severity warning low \
      format "Mode command rejected: transition in progress from {}"

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
