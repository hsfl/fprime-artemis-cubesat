module Components {

  @ Mission mode for the short FlatSat FSR story.
  @ Values are append-only: tools and ground displays decode the numbers.
  enum MissionMode : U8 {
    BASE = 0
    COLLECTION_PENDING = 1
    COLLECTING = 2
    SCIENCE_READY = 3
    DOWNLINKING = 4
    @ Safe mode (MOP v2.0 §8.8): science inhibited. Only a guarded ground command leaves it.
    SAFE = 5
  }

  @ Operations mode by its MOP v2.0 §8 name. MissionApp derives it from MissionMode.
  enum OperationsMode : U8 {
    BASE = 0
    SCIENCE_COLLECTION = 1
    SCIENCE_PROCESSING = 2
    SCIENCE_DOWNLINK = 3
    SAFE = 4
  }

  @ Why MissionApp entered SAFE (SDD v4.0 Table 5, FSW-007, FSW-013). NONE outside SAFE.
  enum SafeCause : U8 {
    NONE = 0
    GROUND_COMMAND = 1
    LOW_POWER = 2
    WATCHDOG = 3
    ADCS_RATE = 4
    CONTACT_TIMEOUT = 5
    OTHER_FAULT = 6
    BOOT = 7
  }

  @ Why MissionApp refused a mode change. ALLOWED means the transition table accepted it.
  enum ModeRejectReason : U8 {
    ALLOWED = 0
    @ The move is not in the transition table.
    NOT_IN_TABLE = 1
    @ In SAFE, only a ground command to BASE can leave.
    SAFE_LOCKED = 2
    @ A latched fault holds SAFE until CLEAR_SAFE_LATCH.
    SAFE_LATCHED = 3
    @ The last fault is more recent than the stability interval.
    SAFE_NOT_STABLE = 4
    @ The mode starts on its own when the payload hands over a product.
    AUTOMATIC_ONLY = 5
    @ Collections start through SCHEDULE_COLLECTION so the payload confirms the start.
    USE_SCHEDULE_COLLECTION = 6
    @ A collection is queued or running.
    COLLECTION_ACTIVE = 7
    @ A component has not yet reported that it applied its SAFE control.
    SAFE_ENTRY_UNCONFIRMED = 8
  }

  @ State an operations mode asks a subsystem to be in (MOP v2.0 §8 hardware status).
  enum SubsystemRequest : U8 {
    OFF = 0
    ON = 1
    MINIMAL = 2
    TBC = 3
    FLAG_DEPENDENT = 4
    LVLH = 5
    SUN_TRACKING = 6
    TARGET_TRACKING = 7
    STANDBY = 8
    ACQUISITION = 9
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

  @ Payload capture request with caller correlation, in seconds.
  port PayloadCaptureRequest(requestId: U32, durationSeconds: U32)

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
    @ MissionApp's capture control is inhibiting new operations.
    INHIBITED = 7
  }

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

  @ Request to the simulated driver to capture one science dataset.
  port PayloadDriverCaptureRequest(
    requestId: U32,
    generation: U32,
    durationSeconds: U32,
    faultMode: PayloadSimulationFault,
    delayMs: U32,
    timeoutMs: U32
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

  @ Correlated response from the simulated driver for either operation kind.
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

  @ Runtime control from MissionApp: ENABLED lets an operation start, DISABLED inhibits it.
  @ MissionApp decides. The receiving component enforces it where the operation starts.
  port OperationControl(control: Fw.Enabled)

  @ Report back to MissionApp that a control is applied. activeOperation = one was already running.
  port OperationControlReport(control: Fw.Enabled, activeOperation: bool)

  @ StorageManager's answer for one science product: stored = committed, false = rejected.
  port ScienceProductStored(productId: U32, stored: bool)

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

  @ Whether the simulated driver's physical capture slot is idle and reusable.
  port PayloadDriverAvailability(available: bool)

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

  @ Fault indication that asks MissionApp for SAFE (FSW-007).
  @ latch = true holds SAFE until the ground sends CLEAR_SAFE_LATCH.
  port SafeModeRequest(cause: SafeCause, latch: bool, detail: U32)

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
