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
        sync input port statusIn: [8] Svc.Ping

        @ Emit a health snapshot event
        async command EMIT_SOH_SNAPSHOT

        @ Aggregated SOH score
        telemetry OverallHealth: U32

        @ Last EPS health key
        telemetry EpsHealth: U32

        @ Last payload health key
        telemetry PayloadHealth: U32

        @ Last ADCS health key
        telemetry AdcsHealth: U32

        @ Last GPS health key
        telemetry GpsHealth: U32

        @ Last storage health key
        telemetry StorageHealth: U32

        @ Last thermal health key
        telemetry ThermalHealth: U32

        @ Last comms health key
        telemetry CommsHealth: U32

        @ Last transport health key
        telemetry TransportHealth: U32

        @ SOH snapshot event
        event Snapshot(
            overall: U32,
            eps: U32,
            payload: U32,
            adcs: U32,
            gps: U32,
            storage: U32,
            thermal: U32,
            comms: U32,
            transport: U32
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
