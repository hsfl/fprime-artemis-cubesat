module Components {
    @ Aggregates status-of-health from subsystem and mission services.
    active component SoHManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Status inputs from subsystem and transport services
        sync input port statusIn: [8] Components.HealthStatus

        @ Emit a health snapshot event
        async command EMIT_SOH_SNAPSHOT

        @ Aggregated SOH score
        telemetry OverallHealth: Components.HealthState

        @ Last EPS health key
        telemetry EpsHealth: Components.HealthState

        @ Last payload health key
        telemetry PayloadHealth: Components.HealthState

        @ Last ADCS health key
        telemetry AdcsHealth: Components.HealthState

        @ Last GPS health key
        telemetry GpsHealth: Components.HealthState

        @ Last storage health key
        telemetry StorageHealth: Components.HealthState

        @ Last thermal health key
        telemetry ThermalHealth: Components.HealthState

        @ Last comms health key
        telemetry CommsHealth: Components.HealthState

        @ Last transport health key
        telemetry TransportHealth: Components.HealthState

        @ SOH snapshot event
        event Snapshot(
            overall: Components.HealthState,
            eps: Components.HealthState,
            payload: Components.HealthState,
            adcs: Components.HealthState,
            gps: Components.HealthState,
            storage: Components.HealthState,
            thermal: Components.HealthState,
            comms: Components.HealthState,
            transport: Components.HealthState
        ) severity activity high format "SOH overall={} eps={} payload={} adcs={} gps={} storage={} thermal={} comms={} transport={}"

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
