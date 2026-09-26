module Components {

  @ Manager tier for the flight controller's side of the FC<->PC link.
  @
  @ Owns the link to the payload computer. Peer traffic arrives here from the
  @ GenericHub and is forwarded to the components that act on it, so link
  @ state lives in one place. Mirrors FlightControllerLinkManager on the Pi.
  @
  @ MissionApp drives link capabilities through this component's ports; the
  @ engineering commands added with each capability are for HIL bring-up and
  @ anomaly response, not routine operations.
  passive component PayloadComputerLinkManager {

    # ----------------------------------------------------------------------
    # Peer interface (GenericHub)
    # ----------------------------------------------------------------------

    @ Payload computer heartbeat. Connect from the hub's
    @ serialOut[FcPcLink.HEARTBEAT].
    sync input port peerAliveIn: FcPcLink.Heartbeat

    @ Payload readiness, resent by the payload computer at 1 Hz. Connect from
    @ the hub's serialOut[FcPcLink.PAYLOAD_STATUS].
    sync input port payloadStateIn: Components.PayloadStateReport

    # ----------------------------------------------------------------------
    # Scheduling
    # ----------------------------------------------------------------------

    @ Staleness check for the payload state. Connect to the same rate group
    @ that drives the link's UART receive, so it runs on the same thread as
    @ payloadStateIn.
    sync input port run: Svc.Sched

    # ----------------------------------------------------------------------
    # Local consumers
    # ----------------------------------------------------------------------

    @ Forwards each payload computer heartbeat to RpiPowerManager, which
    @ promotes its state from BOOT to READY
    output port peerAliveOut: FcPcLink.Heartbeat

    @ Payload readiness for MissionApp. Emitted on every received report and
    @ when the state goes stale.
    output port payloadStateOut: Components.PayloadStateReport

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Heartbeats received from the payload computer since boot
    telemetry HeartbeatsReceived: U32 update on change

    @ Key of the most recent heartbeat: the payload computer's own heartbeat
    @ count. A reset to a small value means the payload computer restarted.
    telemetry LastHeartbeatKey: U32 update on change

    @ Whether the payload can take a capture now, as last reported by the
    @ payload computer. UNKNOWN until the first report, and again when reports
    @ stop arriving.
    telemetry PayloadState: Components.PayloadState update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ The payload computer reported a new payload state
    event PayloadStateChanged(payloadState: Components.PayloadState) \
      severity activity high \
      format "Payload state changed to {}"

    @ No payload state report arrived within the timeout
    event PayloadStateStale(timeoutMs: U32) \
      severity warning low \
      format "No payload state report for {} ms; payload state is UNKNOWN"

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
