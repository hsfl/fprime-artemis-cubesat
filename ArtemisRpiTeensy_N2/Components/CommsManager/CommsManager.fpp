module Components {
    @ Mission-level communications state and downlink manager.
    active component CommsManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Link status input from transport
        sync input port linkStatusIn: Svc.Ping

        @ Science ready input from storage
        sync input port scienceReadyIn: Svc.Ping

        @ Adapter status input
        sync input port adapterStatusIn: Svc.Ping

        @ Downlink request output to storage
        output port downlinkRequestOut: Svc.Ping

        @ Adapter request output
        output port adapterRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Svc.Ping

        @ Request science downlink
        async command REQUEST_SCIENCE_DOWNLINK

        @ Current link state
        telemetry LinkState: U32

        @ Pending science bytes
        telemetry PendingScienceBytes: U32

        @ Downlink request event
        event DownlinkRequested(bytes: U32) severity activity high format "Downlink requested for {} bytes"

        @ Link state event
        event LinkStateUpdated(linkState: U32) severity activity low format "Comms link state updated {}"

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
