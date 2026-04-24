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
        sync input port adapterStatusIn: Svc.Ping

        @ Adapter request output
        output port adapterRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Svc.Ping

        @ Request latest EPS status from adapter
        async command REQUEST_EPS_STATUS

        @ Current EPS state
        telemetry EpsHealthState: U32

        @ Service heartbeat
        telemetry ServiceHeartbeat: U32

        @ EPS status update event
        event EpsStatusUpdated(statusKey: U32) severity activity low format "EPS status updated state={}"

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
