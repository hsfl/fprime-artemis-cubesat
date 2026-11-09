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

  @ Readable PDU output identities. Values match the existing PDU v2 wire IDs.
  enum PduRail : U8 {
    UNKNOWN = 0
    RAIL_3V3_1 = 1
    RAIL_3V3_2 = 2
    RAIL_5V_1 = 3
    RAIL_5V_2 = 4
    RAIL_5V_3 = 5
    RAIL_12V = 7
    RAIL_VBATT = 8
    RAIL_BURN1 = 9
    RAIL_BURN2 = 10
    ALL = 255
  }

  @ Only the seven public switched rails accepted by the existing SET guard.
  enum PduSwitchableRail : U8 {
    RAIL_3V3_1 = 1
    RAIL_3V3_2 = 2
    RAIL_5V_1 = 3
    RAIL_5V_2 = 4
    RAIL_5V_3 = 5
    RAIL_12V = 7
    RAIL_VBATT = 8
  }

  @ Requested enable state; this is not a command result code.
  enum PduDesiredState : U8 {
    OFF = 0
    ON = 1
  }

  @ Enable-pin readback, never a voltage measurement. STALE retains no current-state claim.
  enum PduRailState : U8 {
    UNKNOWN = 0
    OFF = 1
    ON = 2
    STALE = 3
  }

  @ Explicit confirmation for a named power command; never supplied by a default.
  enum PduConfirmation : U8 {
    CANCEL = 0
    CONFIRM = 1
  }

  @ Numeric errors are meaningful only together with the layer that produced them.
  enum PduErrorOrigin : U8 {
    DRIVER = 0
    TEENSY_RPC = 1
    PDU = 2
  }

  @ Rail validity from the hardware driver, independent of request admission/completion.
  @ Masks use the existing nine-output summary bitmap order. Ages count driver scheduler ticks.
  port EpsRailSnapshot(railStateBitmap: U16, knownMask: U16, freshMask: U16, maximumAgeTicks: U32)

  @ Mission collection request from MissionApp to ScienceApp.
  port CollectionRequest(delaySeconds: U32)

  @ Default capture duration in minutes for profiles with local duration configuration.
  constant DEFAULT_CAPTURE_DURATION_MINUTES = 1

  @ Longest supported finite hardware acquisition, in minutes.
  constant MAX_CAPTURE_DURATION_MINUTES = 216

  @ Keep simulated products within the existing bounded 120-second exposure.
  constant MAX_SIMULATED_CAPTURE_DURATION_MINUTES = 2

  @ Payload capture request with caller correlation, in minutes.
  port PayloadCaptureRequest(requestId: U32, durationMinutes: U32)

  @ Configure the payload's duration independently of starting a capture.
  port PayloadDurationConfigRequest(requestId: U32, durationMinutes: U32)

  @ Start a capture using the configuration already applied to the payload.
  port PayloadConfiguredCaptureRequest(requestId: U32)

  @ Maximum printable ASCII literal size in bytes for the board-free synthetic pass-through contract.
  @ Embedded NUL and non-ASCII input are outside this demo contract and are not validated here.
  constant PAYLOAD_PASSTHROUGH_LITERAL_MAX = 128

  @ Result of the synthetic payload command facade; this does not represent an Aris protocol status.
  enum PayloadPassthroughStatus : U8 {
    SIMULATED_ECHOED = 0
  }

  @ Source class for a produced science dataset.
  enum ScienceProductSource : U8 {
    UNKNOWN = 0
    NEUTRON_SIM = 1
    REAL_PAYLOAD = 2
    TEST = 3
  }

  @ Opaque printable ASCII command sent through PayloadManager to the synthetic payload facade.
  port PayloadPassthroughRequest(
    requestId: U32,
    literalCommand: string size PAYLOAD_PASSTHROUGH_LITERAL_MAX
  )

  @ Correlated synthetic response that echoes the original command literally.
  port PayloadPassthroughResponse(
    requestId: U32,
    generation: U32,
    status: PayloadPassthroughStatus,
    literalEcho: string size PAYLOAD_PASSTHROUGH_LITERAL_MAX
  )

  @ Kind of operation admitted by PayloadManager.
  enum PayloadOperationKind : U8 {
    PASSTHROUGH = 0
    CAPTURE = 1
  }

  @ Terminal or admission state for the serialized payload operation actor.
  enum PayloadOperationState : U8 {
    ACCEPTED = 0
    COMPLETED = 1
    BUSY = 2
    INVALID = 3
    DRIVER_UNAVAILABLE = 4
    FAILED = 5
    TIMED_OUT = 6
  }

  @ Terminal configuration outcome correlated to a PayloadDurationConfigRequest.
  port PayloadDurationConfigResult(requestId: U32, $state: PayloadOperationState)

  @ One-shot local simulator behavior; never a real payload protocol status.
  enum PayloadSimulationFault : U8 {
    NORMAL = 0
    FAIL = 1
    NO_RESPONSE = 2
    DELAYED_RESPONSE = 3
    DUPLICATE_RESPONSE = 4
    HANG_CAPTURE = 5
    DRIVER_UNAVAILABLE = 6
  }

  @ Request to the payload driver to capture one science dataset.
  port PayloadDriverCaptureRequest(
    requestId: U32,
    generation: U32,
    durationMinutes: U32,
    timeoutMs: U32
  )

  @ One-shot fault injection for the matching simulator capture generation.
  port PayloadSimulationCaptureOptions(
    generation: U32,
    faultMode: PayloadSimulationFault,
    delayMs: U32
  )

  @ Request to the simulated driver to echo one opaque literal.
  port PayloadDriverPassthroughRequest(
    requestId: U32,
    generation: U32,
    literalCommand: string size PAYLOAD_PASSTHROUGH_LITERAL_MAX,
    faultMode: PayloadSimulationFault,
    delayMs: U32,
    timeoutMs: U32
  )

  @ Correlated response from the payload driver for either operation kind.
  port PayloadDriverOperationResult(
    requestId: U32,
    generation: U32,
    kind: PayloadOperationKind,
    $state: PayloadOperationState,
    literalEcho: string size PAYLOAD_PASSTHROUGH_LITERAL_MAX,
    productId: U32,
    productBytes: U32,
    sourceKind: ScienceProductSource,
    sourcePath: string size 192,
    sourceCrc: U32,
    errorCode: U32
  )

  @ Correlated operation progress from PayloadManager to ScienceApp.
  port PayloadOperationNotice(
    requestId: U32,
    generation: U32,
    kind: PayloadOperationKind,
    $state: PayloadOperationState,
    productId: U32,
    productBytes: U32,
    sourceKind: ScienceProductSource,
    sourcePath: string size 192,
    sourceCrc: U32,
    scienceRequested: bool
  )

  @ Whether the payload driver is available for admission.
  port PayloadDriverAvailability(available: bool)

  @ Science product descriptor handoff. The source path is a driver-owned local file path.
  port ScienceProductDescriptor(
    productId: U32,
    productBytes: U32,
    sourceKind: ScienceProductSource,
    sourcePath: string size 192,
    sourceCrc: U32
  )

  @ Terminal outcome of a new product submission to durable storage.
  port ScienceProductCommitResult(productId: U32, productBytes: U32, committed: bool)

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

  @ Request automatic delivery of a product already handed to StorageManager.
  port ScienceProductDeliveryRequest(productId: U32)

  @ Validated source descriptor returned for an explicit delivery request.
  port ScienceProductDeliveryDescriptor(
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
    lastAdapterOpcode: U8,
    epsRequest: EpsRequest,
    firmwareMajor: U8,
    firmwareMinor: U8,
    firmwarePatch: U8,
    buildFlags: U8,
    gitHash: string size 12
  )
}
