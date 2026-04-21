module Components {
    @ Artemis GPS hardware adapter shim.
    passive component GpsAdapter_Artemis {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Adapter request input
        sync input port requestIn: Svc.Ping

        @ Adapter status output
        output port statusOut: Svc.Ping

        @ Last handled request key
        telemetry LastRequestKey: U32

        @ Number of processed adapter requests
        telemetry RequestCount: U32

        @ Current GPS fix state (0=no-fix, 1=acquiring, 2=2D, 3=3D)
        telemetry FixState: U32

        @ Estimated visible satellites in current model sample
        telemetry SatellitesTracked: U32

        @ 0-100 synthetic fix quality score for demo observability
        telemetry FixQualityScore: U32

        @ Request handling event
        event RequestHandled(requestKey: U32, statusKey: U32) severity activity low format "Artemis GPS request={} status={}"

        @ GPS fix state transition event
        event FixStateChanged(previousState: U32, currentState: U32) severity activity high format "GPS fix state {} -> {}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
