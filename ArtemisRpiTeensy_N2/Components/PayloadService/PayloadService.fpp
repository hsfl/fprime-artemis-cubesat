module Components {
    @ Payload service exposing stable mission-facing behavior.
    active component PayloadService {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Collection request input from ScienceManager
        sync input port requestIn: Svc.Ping

        @ Adapter status input
        sync input port adapterStatusIn: Svc.Ping

        @ Adapter request output
        output port adapterRequestOut: Svc.Ping

        @ Payload status output to ScienceManager
        output port statusOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Svc.Ping

        @ Request latest payload status from adapter
        async command REQUEST_PAYLOAD_STATUS

        @ Last payload sample value
        telemetry LastPayloadValue: U32

        @ Service heartbeat
        telemetry ServiceHeartbeat: U32

        @ Payload request forwarded to adapter
        event PayloadCollectionForwarded(requestKey: U32) severity activity high format "Payload request forwarded key={}"

        @ Payload status update event
        event PayloadStatusUpdated(statusKey: U32) severity activity low format "Payload status updated key={}"

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
