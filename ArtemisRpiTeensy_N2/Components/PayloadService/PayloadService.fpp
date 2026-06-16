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

        @ Configure placeholder collection settings
        async command CONFIGURE_PAYLOAD(sampleCount: U32, periodMs: U32)

        @ Start a payload collection by collection ID
        async command START_PAYLOAD_COLLECTION(collectionId: U32)

        @ Enable or disable simulated payload mode. 0=real, nonzero=simulated.
        async command SET_PAYLOAD_SIM_MODE(enable: U32)

        @ Last payload sample value
        telemetry LastPayloadValue: U32

        @ Last collection ID requested
        telemetry LastCollectionId: U32

        @ Configured sample count
        telemetry SampleCount: U32

        @ Configured sample period in milliseconds
        telemetry SamplePeriodMs: U32

        @ Simulated payload mode state
        telemetry SimModeEnabled: U32

        @ Service heartbeat
        telemetry ServiceHeartbeat: U32

        @ Payload request forwarded to adapter
        event PayloadCollectionForwarded(requestKey: U32) severity activity high format "Payload request forwarded key={}"

        @ Payload status update event
        event PayloadStatusUpdated(statusKey: U32) severity activity low format "Payload status updated key={}"

        @ Payload configuration event
        event PayloadConfigured(sampleCount: U32, periodMs: U32) severity activity high format "Payload configured samples={} periodMs={}"

        @ Payload simulation mode event
        event PayloadSimModeChanged(enable: U32) severity activity high format "Payload sim mode={}"

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
