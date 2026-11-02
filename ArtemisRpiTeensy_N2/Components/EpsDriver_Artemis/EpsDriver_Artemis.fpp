module Components {
    @ Artemis EPS hardware driver shim.
    passive component EpsDriver_Artemis {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input for pending request timeout recovery
        sync input port run: Svc.Sched

        @ Driver request input
        sync input port requestIn: Components.EpsCommand

        @ Teensy-local RPC response input from UART channel 2.
        sync input port teensyResponseIn: Fw.BufferSend

        @ Driver status output
        output port statusOut: Components.EpsStatus

        @ Last observed rails and freshness; ages are scheduler ticks, fresh for at most 3 ticks.
        output port railSnapshotOut: Components.EpsRailSnapshot

        @ Teensy-local RPC request output to UART channel 2.
        output port teensyRequestOut: Fw.BufferSend

        @ Last handled PDU request
        telemetry LastRequest: Components.EpsRequest

        @ Last PDU protocol status
        telemetry LastPduStatus: U32

        @ Last PDU opcode
        telemetry LastPduOpcode: U32

        @ Last PDU protocol version
        telemetry PduProtocolVersion: U32

        @ Last PDU output bitmap
        telemetry PduOutputBitmap: U32

        @ Last PDU fault bitmap
        telemetry PduFaultBitmap: U32

        @ Last PDU uptime in seconds
        telemetry PduUptimeSeconds: U32

        @ Teensy-local RPC failure count
        telemetry TransportFailureCount: U32

        @ Age of the pending Teensy-local PDU request in scheduler ticks
        telemetry PendingRequestTicks: U32

        @ PDU firmware semantic version fields from GET_PROTOCOL_INFO
        telemetry PduFirmwareMajor: U8
        telemetry PduFirmwareMinor: U8
        telemetry PduFirmwarePatch: U8

        @ PDU build flags: bit 0 hash known, bit 1 dirty build
        telemetry PduBuildFlags: U8

        @ 12-character lowercase Git hash, all zeroes when unknown
        telemetry PduGitHash: string size 12

        @ PDU request queued to the Teensy-local channel
        event PduRequestQueued(epsRequest: Components.EpsRequest, requestId: U32) severity activity low format "Artemis PDU request={} queued local request={}"

        @ PDU request handling event
        event PduRequestHandled(epsRequest: Components.EpsRequest, status: U32) severity activity low format "Artemis PDU request={} status={}"

        @ PDU request failure event; keep throttled because driver/transport paths can storm.
        event PduRequestFailed(epsRequest: Components.EpsRequest, status: U32) severity warning low format "Artemis PDU request={} failed status={}" throttle 5

        @ PDU request timeout event; keep throttled because driver/transport paths can storm.
        event PduRequestTimedOut(epsRequest: Components.EpsRequest, requestId: U32) severity warning low format "Artemis PDU request={} timed out local request={}" throttle 5

        @ Validated individual readback, including each rail in an explicit GET ALL.
        event PduRailReadback(epsRequest: Components.EpsRequest, rail: Components.PduRail, readback: Components.PduRailState, requestId: U32) severity activity high format "PDU {} rail={} readback={} result=OK local_request={}"

        @ Successful SET with validated readback.
        event PduRailSetResult(rail: Components.PduRail, desired: Components.PduDesiredState, readback: Components.PduRailState, requestId: U32) severity activity high format "PDU SET rail={} desired={} readback={} result=OK local_request={}"

        @ A valid SET reply differed from the requested state.
        event PduRailSetMismatch(rail: Components.PduRail, desired: Components.PduDesiredState, readback: Components.PduRailState, requestId: U32) severity warning high format "PDU SET rail={} desired={} readback={} result=READBACK_MISMATCH local_request={}"

        @ A cycle reply confirms only the initial OFF phase.
        event PduCycleStarted(rail: Components.PduRail, readback: Components.PduRailState, requestId: U32) severity activity high format "PDU CYCLE rail={} initial_readback={} result=STARTED local_request={}"

        @ Decoded failure; local request ID wraps and is diagnostic only.
        event PduOperationError(epsRequest: Components.EpsRequest, rail: Components.PduRail, origin: Components.PduErrorOrigin, reason: string size 32, code: U32, requestId: U32) severity warning high format "PDU {} rail={} origin={} result={} code={} local_request={}"

        @ Validated firmware identity response.
        event PduInfoReadback(protocol: U32, major: U32, minor: U32, patch: U32, flags: U32, gitHash: string size 12, requestId: U32) severity activity high format "PDU INFO protocol={} firmware={}.{}.{} build_flags={} git={} result=OK local_request={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
