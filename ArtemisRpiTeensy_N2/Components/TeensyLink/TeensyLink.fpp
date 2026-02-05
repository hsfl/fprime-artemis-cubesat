module Components {
    @ Teensy link health and observability component for the UART relay path.
    active component TeensyLink {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Report current link counters via event
        async command LINK_STATUS

        @ Reset link counters to zero
        async command RESET_COUNTERS

        @ Incrementing heartbeat emitted by run
        telemetry LinkHeartbeat: U32

        @ Counter of malformed frame observations
        telemetry FramingDrops: U32

        @ Counter of timeout observations
        telemetry TimeoutEvents: U32

        @ Current link status snapshot
        event LinkStatus(
            heartbeat: U32,
            framingDrops: U32,
            timeoutEvents: U32
        ) severity activity high format "TeensyLink hb={} framingDrops={} timeoutEvents={}"

        @ Link counters reset notification
        event CountersReset severity activity low format "TeensyLink counters reset"

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
