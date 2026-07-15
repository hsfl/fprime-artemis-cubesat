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

  @ EPS driver request opcode. Specific drivers map these logical requests to hardware protocols.
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

  @ Mission collection request from MissionApp to ScienceApp.
  port CollectionRequest(delaySeconds: U32)

  @ Payload capture request in seconds.
  port PayloadCaptureRequest(durationSeconds: U32)

  @ Source class for a produced science dataset.
  enum ScienceProductSource : U8 {
    UNKNOWN = 0
    NEUTRON_SIM = 1
    REAL_PAYLOAD = 2
    TEST = 3
  }

  @ Science product descriptor handoff. The source path is a driver-owned local file path.
  port ScienceProductDescriptor(
    productId: U32,
    productBytes: U32,
    sourceKind: ScienceProductSource,
    sourcePath: string size 192,
    sourceCrc: U32
  )

  @ Storage-to-comms science availability notification.
  port ScienceDownlinkReady(
    productId: U32,
    productBytes: U32,
    sourceKind: ScienceProductSource,
    sourcePath: string size 192,
    sourceCrc: U32
  )

  @ Comms-to-storage science downlink request.
  port ScienceDownlinkRequest(
    productId: U32,
    productBytes: U32,
    sourceKind: ScienceProductSource,
    sourcePath: string size 192,
    sourceCrc: U32
  )

  @ Local acceptance result for a payload packet copied toward the Pi UART.
  @ This does not claim RF transmission or ground receipt. The caller retains
  @ ownership of the supplied buffer after the synchronous call returns.
  enum PayloadSendStatus : U8 {
    LOCAL_ACCEPTED = 0
    LOCAL_RETRY = 1
    LOCAL_ERROR = 2
  }

  @ Synchronous payload packet handoff with explicit local-UART acceptance.
  port PayloadPacketSend(
    ref sendBuffer: Fw.Buffer
  ) -> PayloadSendStatus

  @ Normalized subsystem health report with subsystem-specific detail.
  port HealthStatus(healthState: HealthState, detail: U32)

  @ Mission mode update from services that complete visible story transitions.
  port MissionModeUpdate(mode: MissionMode, detail: U32)

  @ RF link-strength status in dBm.
  port RssiStatus(rssiDbm: I32)

  @ Payload downlink transfer status from the channel-1 blob manager.
  port PayloadDownlinkStatus(
    stateValue: U32,
    transferId: U32,
    productId: U32,
    totalBytes: U32,
    packetsSent: U32,
    totalPackets: U32,
    lastError: U32
  )

  @ EPS command from mission-facing EPS manager to hardware driver.
  port EpsCommand(epsRequest: EpsRequest, outputId: U8, commandedState: U8, durationMs: U16)

  @ EPS status from hardware driver to mission-facing EPS manager.
  port EpsStatus(
    healthState: HealthState,
    linkState: U8,
    adapterProtocolVersion: U8,
    railStateBitmap: U16,
    resetCause: U8,
    faultBitmap: U8,
    uptimeSeconds: U32,
    capabilities: U8,
    adapterStatus: U8,
    lastAdapterOpcode: U8
  )
}
