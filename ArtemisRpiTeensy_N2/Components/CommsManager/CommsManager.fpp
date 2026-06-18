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
        sync input port scienceReadyIn: Components.ScienceDownlinkReady

        @ Adapter status input
        sync input port adapterStatusIn: Svc.Ping

        @ Downlink request output to storage
        output port downlinkRequestOut: Components.ScienceDownlinkRequest

        @ Generic payload downlink request output
        output port payloadDownlinkRequestOut: Svc.Ping

        @ Adapter request output
        output port adapterRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Mission mode update output
        output port missionModeOut: Components.MissionModeUpdate

        @ Request science downlink
        async command REQUEST_SCIENCE_DOWNLINK

        @ Request latest link status from the selected radio adapter
        async command REQUEST_LINK_STATUS

        @ Select logical radio backend. 0=RFM23BP, 1=SatNOGS.
        async command SELECT_RADIO_BACKEND(backend: U32)

        @ Current link state
        telemetry LinkState: U32

        @ Pending science bytes
        telemetry PendingScienceBytes: U32

        @ Selected logical radio backend
        telemetry ActiveRadioBackend: U32

        @ Number of explicit link polls
        telemetry LinkPollCount: U32

        @ Downlink request event
        event DownlinkRequested(bytes: U32) severity activity high format "Downlink requested for {} bytes"

        @ Downlink completion event for the current synchronous/demo downlink path
        event DownlinkFinished(bytes: U32) severity activity high format "Downlink finished for {} bytes"

        @ Link state event
        event LinkStateUpdated(linkState: U32) severity activity low format "Comms link state updated {}"

        @ Radio backend selection event
        event RadioBackendSelected(backend: U32) severity activity high format "Radio backend selected {}"

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
