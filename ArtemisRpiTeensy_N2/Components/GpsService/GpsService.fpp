module Components {
    @ GPS service exposing stable mission-facing behavior.
    active component GpsService {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Adapter status input
        sync input port adapterStatusIn: Svc.Ping

        @ Adapter request output
        output port adapterRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Svc.Ping

        @ Request latest GPS status from adapter
        async command REQUEST_GPS_STATUS

        @ Current GPS state
        telemetry GpsFixState: U32

        @ Service heartbeat
        telemetry ServiceHeartbeat: U32

        @ GPS status update event
        event GpsStatusUpdated(statusKey: U32) severity activity low format "GPS status updated state={}"

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
