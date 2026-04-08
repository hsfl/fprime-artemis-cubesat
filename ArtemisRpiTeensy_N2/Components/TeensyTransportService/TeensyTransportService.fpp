module Components {
    @ Teensy transport service for UART/RF link observability.
    active component TeensyTransportService {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Adapter status input
        sync input port adapterStatusIn: Svc.Ping

        @ Link status output to CommsManager
        output port linkStatusOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Svc.Ping

        @ Report current link counters via event
        async command LINK_STATUS

        @ Reset link counters to zero
        async command RESET_COUNTERS

        @ Incrementing heartbeat emitted by run
        telemetry LinkHeartbeat: U32

        @ Counter of uplink frame observations
        telemetry UplinkFrames: U32

        @ Counter of downlink frame observations
        telemetry DownlinkFrames: U32

        @ Current link status snapshot
        event LinkStatus(
            heartbeat: U32,
            uplinkFrames: U32,
            downlinkFrames: U32
        ) severity activity high format "TeensyTransport hb={} uplink={} downlink={}"

        @ Link counters reset notification
        event CountersReset severity activity low format "TeensyTransport counters reset"

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
