module Components {
    @ Aggregates status-of-health from subsystem and mission managers.
    @ Reports the spacecraft state of health (SOH) on command, and on a timer once the ground turns
    @ reporting on (MVP demo steps 2.1, 2.2, 4.1 and C&T PUS 3 housekeeping, TM 25 style).
    active component SoHApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input (1 Hz). Drives periodic SOH reports while reporting is on.
        @ Connected to rateGroup1 in both topologies. Silent until SET_SOH_REPORTING ENABLED.
        sync input port run: Svc.Sched

        @ Status inputs from subsystem and transport managers
        sync input port statusIn: [8] Components.HealthStatus

        @ Emit one SOH report now: the Snapshot event plus every health channel
        async command EMIT_SOH_SNAPSHOT

        @ Turn periodic SOH reports on or off (C&T PUS 3 TC 5 and TC 6). Off at start-up.
        @ Turning it on sends one report at once, then one every SOH_PERIOD_SECONDS.
        async command SET_SOH_REPORTING(reporting: Fw.Enabled)

        @ Seconds between periodic SOH reports, 1 to 3600 (C&T PUS 3 TC 31). PROVISIONAL.
        param SOH_PERIOD_SECONDS: U32 default 10

        @ Slots that must report for OverallHealth to be OK. Bit 0 = EPS, 1 payload, 2 ADCS, 3 GPS,
        @ 4 storage, 5 thermal, 6 comms, 7 transport. Default 0x3F: the six managers in every build.
        @ Comms and transport still count when they report WARN or FAIL. PROVISIONAL.
        param SOH_REQUIRED_SLOTS: U8 default 0x3F

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

        @ Periodic SOH reporting on or off
        telemetry SohReporting: Fw.Enabled update on change

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

        @ Periodic SOH reporting switched on or off
        event SohReportingChanged(reporting: Fw.Enabled, periodSeconds: U32) severity activity high format "SOH reporting {} every {} s"

        @ An SOH parameter was out of range. The default is used instead.
        event SohParameterRejected(paramId: U32, value: U32) severity warning low format "SOH parameter {} value {} out of range, default used"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel

        @ Parameter get port
        param get port prmGetOut

        @ Parameter set port
        param set port prmSetOut
    }
}
