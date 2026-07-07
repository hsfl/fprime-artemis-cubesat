module Components {
    @ GPS manager exposing stable mission-facing behavior.
    active component GpsManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Driver status input
        sync input port driverStatusIn: Svc.Ping

        @ Driver request output
        output port driverRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Request latest GPS status from driver
        async command REQUEST_GPS_STATUS

        @ Current GPS state
        telemetry GpsFixState: U32

        @ Manager heartbeat
        telemetry ManagerHeartbeat: U32

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
