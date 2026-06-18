module Components {

  @ Mission mode for the short FlatSat FSR story.
  enum MissionMode : U8 {
    BASE = 0
    COLLECTION_PENDING = 1
    COLLECTING = 2
    SCIENCE_READY = 3
    DOWNLINKING = 4
  }

  @ Normalized subsystem health state for SOH aggregation.
  enum HealthState : U8 {
    UNKNOWN = 0
    OK = 1
    WARN = 2
    FAIL = 3
  }

  @ EPS/PDU adapter request opcode.
  enum EpsRequest : U8 {
    GET_SUMMARY_STATUS = 0
    PING = 1
    GET_PROTOCOL_INFO = 2
    GET_OUTPUT_STATE = 3
    SET_OUTPUT_STATE = 4
    POWER_CYCLE_OUTPUT = 5
    GET_CHARGER_STATUS = 6
    SET_CHARGER_STATE = 7
  }

  @ Mission collection request from MissionManager to ScienceManager.
  port CollectionRequest(delaySeconds: U32)

  @ Payload capture request in seconds.
  port PayloadCaptureRequest(durationSeconds: U32)

  @ Science product handoff, represented by product size in bytes for the MVP.
  port ScienceProduct(productBytes: U32)

  @ Storage-to-comms science availability notification.
  port ScienceDownlinkReady(productBytes: U32)

  @ Comms-to-storage science downlink request.
  port ScienceDownlinkRequest(productBytes: U32)

  @ Normalized subsystem health report with subsystem-specific detail.
  port HealthStatus(healthState: HealthState, detail: U32)

  @ Mission mode update from services that complete visible story transitions.
  port MissionModeUpdate(mode: MissionMode, detail: U32)

  @ EPS/PDU command from mission-facing EPS service to hardware adapter.
  port EpsCommand(epsRequest: EpsRequest, outputId: U8, commandedState: U8, durationMs: U16)

  @ EPS/PDU status from hardware adapter to mission-facing EPS service.
  port EpsStatus(
    healthState: HealthState,
    linkState: U8,
    protocolVersion: U8,
    outputBitmap: U16,
    resetCause: U8,
    faultBitmap: U8,
    uptimeSeconds: U32,
    capabilities: U8,
    pduStatus: U8,
    lastOpcode: U8
  )
}
