module Components {
  @ Communication state, independent of whether a requested rail state was achieved.
  enum EpsLinkState: U8 { UNKNOWN, RESPONDING, FAULT }
  @ Logical PDU GPIO readback, not an analog voltage measurement.
  enum EpsOutputState: U8 { UNKNOWN, OFF, ON }
  struct EpsStatus {
    linkState: EpsLinkState
    knownOutputs: U16
    outputBitmap: U16
    resetCause: U8
    faults: U8
    uptimeSeconds: U32
    capabilities: U8
    busy: bool
  }
  port EpsStatusGet(ref status: EpsStatus)
}
