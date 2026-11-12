module Components {
  @ Direct UART v2 PDU manager. GPIO readback is distinct from requested state.
  passive component EpsManager {
    guarded input port run: Svc.Sched
    guarded input port pingIn: Svc.Ping
    output port pingOut: Svc.Ping
    guarded input port drvReceiveIn: Drv.ByteStreamData
    output port drvReceiveReturnOut: Fw.BufferSend
    guarded input port drvConnected: Drv.ByteStreamReady
    output port drvSendOut: Drv.ByteStreamSend
    guarded input port readingGet: Components.EpsStatusGet

    guarded command PING
    guarded command GET_SUMMARY_STATUS
    @ Normal logical rails only. OK requires matching post-command GPIO readback.
    guarded command SET_OUTPUT_STATE(outputId: U8, $state: Fw.On)
    @ OK requires observed OFF then observed restored ON, not only acceptance.
    guarded command POWER_CYCLE_OUTPUT(outputId: U8, offTimeMs: U16)

    telemetry LinkState: EpsLinkState update on change
    telemetry KnownOutputs: U16 update on change
    telemetry OutputBitmap: U16 update on change
    telemetry ResetCause: U8 update on change
    telemetry FaultBitmap: U8 update on change
    telemetry UptimeSeconds: U32 update on change
    telemetry Capabilities: U8 update on change
    telemetry Busy: bool update on change
    telemetry CrcErrors: U32 update on change
    telemetry FramingErrors: U32 update on change
    telemetry Timeouts: U32 update on change
    telemetry LastProtocolStatus: U8 update on change

    event PduResponse($opcode: U8, status: U8) severity activity low format "PDU opcode={} status={}"
    event OutputReadback(outputId: U8, $state: EpsOutputState) severity activity high format "PDU output={} readback={}"
    event CycleStarted(outputId: U8, offTimeMs: U16) severity activity high format "PDU output={} off for {} ms"
    event CycleRestored(outputId: U8) severity activity high format "PDU output={} restored ON"
    event OperationFailed($opcode: U8, reason: U32) severity warning high format "PDU opcode={} failed reason={}"
    time get port timeCaller
    import Fw.Command
    import Fw.Event
    import Fw.Channel
  }
}
