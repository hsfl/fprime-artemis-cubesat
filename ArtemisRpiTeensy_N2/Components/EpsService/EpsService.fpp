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

        @ Ping the EPS adapter endpoint
        async command PING_EPS_ADAPTER

        @ Request EPS adapter protocol version/capabilities
        async command REQUEST_EPS_ADAPTER_INFO

        @ Request one EPS rail state by rail/output ID
        async command REQUEST_EPS_RAIL(outputId: U32)

        @ Set one safe EPS rail state. confirm must be 1.
        async command SET_EPS_RAIL_STATE(outputId: U32, desiredState: U32, confirm: U32)

        @ Power-cycle one safe EPS rail. confirm must be 1.
        async command POWER_CYCLE_EPS_RAIL(outputId: U32, offMs: U32, confirm: U32)

        @ Request EPS charger status
        async command REQUEST_CHARGER_STATUS

        @ Set EPS charger state. confirm must be 1.
        async command SET_CHARGER_STATE(enable: U32, confirm: U32)

        @ Current EPS state
        telemetry EpsHealthState: Components.HealthState

        @ Latest adapter link state. 0=not configured, 1=queued to Teensy, 2=protocol OK, 3=error, 4=busy.
        telemetry AdapterLinkState: U32

        @ Latest EPS adapter protocol version
        telemetry AdapterProtocolVersion: U32

        @ Latest switched rail bitmap from summary status
        telemetry RailStateBitmap: U32

        @ Latest adapter reset cause
        telemetry AdapterResetCause: U32

        @ Latest adapter fault bitmap
        telemetry AdapterFaultBitmap: U32

        @ Latest adapter uptime in seconds
        telemetry AdapterUptimeSeconds: U32

        @ Latest adapter protocol status code
        telemetry LastAdapterStatus: U32

        @ Latest adapter opcode handled
        telemetry LastAdapterOpcode: U32

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
