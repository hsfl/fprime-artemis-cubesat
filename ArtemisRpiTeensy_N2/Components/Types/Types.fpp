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

  @ Teensy-local RFM23BP control operation.
  enum RadioOperation : U8 {
    STATUS = 1
    SET_ENABLED = 2
  }

  @ Factual RFM23BP hardware state. Initialization is transient, not a state.
  enum RadioState : U8 {
    OFF = 0
    READY = 1
  }

  @ Last factual local RFM23BP fault reported by the satellite Teensy.
  enum RadioFault : U8 {
    NONE = 0
    INIT_FAILED = 1
    WATCHDOG_RESET = 2
    LOCAL_TX_FAULT = 3
  }

  @ Result of the most recent Pi-to-Teensy radio RPC.
  enum RadioRpcResult : U8 {
    OK = 0
    BAD_REQUEST = 1
    BUSY = 2
    TIMEOUT = 3
    TARGET_ERROR = 4
    BAD_RESPONSE = 5
    NOT_CONFIGURED = 6
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

  @ Normalized subsystem health report with subsystem-specific detail.
  port HealthStatus(healthState: HealthState, detail: U32)

  @ Mission mode update from services that complete visible story transitions.
  port MissionModeUpdate(mode: MissionMode, detail: U32)

  @ RF link-strength status in dBm.
  port RssiStatus(rssiDbm: I32)

  @ Policy request from CommsApp to the Teensy/RFM23BP driver.
  port RadioControlRequest(operation: RadioOperation, enabled: U8)

  @ Correlated Teensy/RFM23BP RPC result and latest factual radio status.
  port RadioStatus(
    operation: RadioOperation,
    result: RadioRpcResult,
    radioState: RadioState,
    radioFault: RadioFault,
    bootFlags: U8,
    rssiValid: U8,
    rssiDbm: I32,
    rssiAgeMs: U32,
    initAttempts: U32,
    rfRxPackets: U32,
    rfTxPackets: U32,
    rfTxDrops: U32
  )

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
