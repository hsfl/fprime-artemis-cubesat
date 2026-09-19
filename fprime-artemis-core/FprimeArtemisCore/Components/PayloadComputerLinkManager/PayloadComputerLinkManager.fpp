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

    # ----------------------------------------------------------------------
    # Local consumers
    # ----------------------------------------------------------------------

    @ Forwards each payload computer heartbeat to RpiPowerManager, which
    @ promotes its state from BOOT to READY
    output port peerAliveOut: FcPcLink.Heartbeat

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Heartbeats received from the payload computer since boot
    telemetry HeartbeatsReceived: U32 update on change

    @ Key of the most recent heartbeat: the payload computer's own heartbeat
    @ count. A reset to a small value means the payload computer restarted.
    telemetry LastHeartbeatKey: U32 update on change

    ##########################################################
    # Standard AC ports
    ##########################################################
    @ Port for requesting the current time
    time get port timeCaller

    @ Port for emitting telemetry
    telemetry port tlmOut

  }

}
