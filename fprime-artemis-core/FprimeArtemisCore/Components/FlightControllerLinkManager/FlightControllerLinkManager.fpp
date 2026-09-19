module Components {

  @ Manager tier for the payload computer's side of the FC<->PC link.
  @
  @ Reports this computer's liveness to the flight controller by emitting a
  @ periodic heartbeat into the GenericHub. The flight controller's
  @ RpiPowerManager consumes it to promote its reported state from BOOT to
  @ READY.
  @
  @ This is the minimum payload the link carries. Richer payload-computer
  @ status is added here as the link matures; the transport underneath does
  @ not change.
  passive component FlightControllerLinkManager {

    # ----------------------------------------------------------------------
    # Peer interface
    # ----------------------------------------------------------------------

    @ Heartbeat to the flight controller. Connect to a GenericHub serialIn
    @ port; the matching serialOut index on the peer hub delivers it.
    output port peerAliveOut: FcPcLink.Heartbeat

    # ----------------------------------------------------------------------
    # Scheduling
    # ----------------------------------------------------------------------

    @ Rate group input: emits one heartbeat per invocation
    sync input port run: Svc.Sched

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Emit a heartbeat immediately rather than waiting for the next tick
    sync command SEND_HEARTBEAT

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Heartbeats emitted toward the flight controller since boot
    telemetry HeartbeatsSent: U32 update on change

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ First heartbeat since boot was emitted
    event LinkHeartbeatStarted \
      severity activity high \
      format "Payload computer heartbeat started toward the flight controller"

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
