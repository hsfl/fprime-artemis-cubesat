module Components {
    @ ADCS service exposing stable mission-facing behavior.
    active component AdcsService {

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

        @ Request latest ADCS status from adapter
        async command REQUEST_ADCS_STATUS

        @ Current ADCS state
        telemetry AdcsState: U32

        @ Service heartbeat
        telemetry ServiceHeartbeat: U32

        @ ADCS status update event
        event AdcsStatusUpdated(statusKey: U32) severity activity low format "ADCS status updated state={}"

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
