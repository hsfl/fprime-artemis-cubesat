module Components {
    @ EPS service exposing stable mission-facing behavior.
    active component EpsService {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Adapter status input
        sync input port adapterStatusIn: Components.EpsStatus

        @ Adapter request output
        output port adapterRequestOut: Components.EpsCommand

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Request latest EPS status from adapter
        async command REQUEST_EPS_STATUS

        @ Ping the PDU protocol endpoint
        async command PING_PDU

        @ Request PDU protocol version/capabilities
        async command REQUEST_PDU_PROTOCOL

        @ Request one PDU output state by output ID
        async command REQUEST_PDU_OUTPUT(outputId: U32)

        @ Set one safe PDU output state. confirm must be 1.
        async command SET_PDU_OUTPUT(outputId: U32, desiredState: U32, confirm: U32)

        @ Power-cycle one safe PDU output. confirm must be 1.
        async command POWER_CYCLE_PDU_OUTPUT(outputId: U32, offMs: U32, confirm: U32)

        @ Request PDU charger status
        async command REQUEST_CHARGER_STATUS

        @ Set PDU charger state. confirm must be 1.
        async command SET_CHARGER_STATE(enable: U32, confirm: U32)

        @ Current EPS state
        telemetry EpsHealthState: Components.HealthState

        @ Latest adapter link state. 0=not configured, 1=queued to Teensy, 2=protocol OK, 3=error, 4=busy.
        telemetry PduLinkState: U32

        @ Latest PDU protocol version
        telemetry PduProtocolVersion: U32

        @ Latest switched output bitmap from summary status
        telemetry PduOutputBitmap: U32

        @ Latest PDU reset cause
        telemetry PduResetCause: U32

        @ Latest PDU fault bitmap
        telemetry PduFaultBitmap: U32

        @ Latest PDU uptime in seconds
        telemetry PduUptimeSeconds: U32

        @ Latest PDU protocol status code
        telemetry LastPduStatus: U32

        @ Latest PDU opcode handled by adapter
        telemetry LastPduOpcode: U32

        @ Service heartbeat
        telemetry ServiceHeartbeat: U32

        @ EPS status update event
        event EpsStatusUpdated(healthState: Components.HealthState, outputBitmap: U32, faultBitmap: U32) severity activity low format "EPS status health={} outputs={} faults={}"

        @ EPS command rejected by mission-facing guard
        event EpsCommandRejected(reason: U32, value: U32) severity warning low format "EPS command rejected reason={} value={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
