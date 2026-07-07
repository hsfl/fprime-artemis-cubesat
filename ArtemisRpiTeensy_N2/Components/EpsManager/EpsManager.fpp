module Components {
    @ EPS manager exposing stable mission-facing behavior.
    active component EpsManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Driver status input
        sync input port driverStatusIn: Components.EpsStatus

        @ Driver request output
        output port driverRequestOut: Components.EpsCommand

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Request latest EPS status from driver
        async command REQUEST_EPS_STATUS

        @ Ping the EPS driver endpoint
        async command PING_EPS_DRIVER

        @ Request EPS driver protocol version/capabilities
        async command REQUEST_EPS_DRIVER_INFO

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

        @ Latest driver link state. 0=not configured, 1=queued to Teensy, 2=protocol OK, 3=error, 4=busy.
        telemetry DriverLinkState: U32

        @ Latest EPS driver protocol version
        telemetry DriverProtocolVersion: U32

        @ Latest switched rail bitmap from summary status
        telemetry RailStateBitmap: U32

        @ Latest driver reset cause
        telemetry DriverResetCause: U32

        @ Latest driver fault bitmap
        telemetry DriverFaultBitmap: U32

        @ Latest driver uptime in seconds
        telemetry DriverUptimeSeconds: U32

        @ Latest driver protocol status code
        telemetry LastDriverStatus: U32

        @ Latest driver opcode handled
        telemetry LastDriverOpcode: U32

        @ Manager heartbeat
        telemetry ManagerHeartbeat: U32

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
