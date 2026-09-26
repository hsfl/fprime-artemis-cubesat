@ Shared definitions for the link between the flight controller (Teensy 4.1)
@ and the payload computer (Raspberry Pi).
@
@ Both deployments are built separately but must agree on everything here:
@ GenericHub serializes the serial port index into every message, so a
@ serialIn index on one side is delivered on the same serialOut index on the
@ other. Defining the indices once prevents the two binaries from drifting.
module FcPcLink {

  # ----------------------------------------------------------------------
  # Port types
  # ----------------------------------------------------------------------

  @ Payload computer heartbeat. The key is the payload computer's own
  @ heartbeat count, so a reset to a small value means it restarted.
  @
  @ Deliberately not Svc.Ping: Svc.Ping means "health ping" in F Prime, and a
  @ topology's `health connections` pattern auto-wires any component with a
  @ Svc.Ping input and output into the health subsystem. A link manager that
  @ both receives and forwards heartbeats would be captured by it.
  port Heartbeat(key: U32)

  # ----------------------------------------------------------------------
  # GenericHub serial port allocation
  # ----------------------------------------------------------------------
  #
  # Each index carries one logical flow in one direction. GenericHubCfg
  # provides 10 serial ports each way by default.

  @ Pi -> Teensy: payload computer heartbeat (FcPcLink.Heartbeat, key = heartbeat count)
  constant HEARTBEAT = 0

  @ Teensy -> Pi: link check request
  constant ECHO_REQUEST = 1

  @ Pi -> Teensy: link check reply
  constant ECHO_REPLY = 2

  @ Teensy -> Pi: restart the payload computer's F Prime deployment
  constant RESTART_REQUEST = 3

  @ Pi -> Teensy: payload computer lifecycle report (exiting / started)
  constant LIFECYCLE = 4

  @ Pi -> Teensy: payload readiness (Components.PayloadStateReport), resent at
  @ 1 Hz with the heartbeat so a missed message heals itself
  constant PAYLOAD_STATUS = 5

}
