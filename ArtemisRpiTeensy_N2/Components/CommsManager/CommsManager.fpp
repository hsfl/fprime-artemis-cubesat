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

        @ Adapter RSSI input
        sync input port rssiStatusIn: Components.RssiStatus

        @ Payload downlink transfer status input
        sync input port payloadDownlinkStatusIn: Components.PayloadDownlinkStatus

        @ Downlink request output to storage
        output port downlinkRequestOut: Components.ScienceDownlinkRequest

        @ Generic payload downlink request output
        output port payloadDownlinkRequestOut: Components.ScienceDownlinkRequest

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

        @ Poll the radio adapter and log current RSSI in dBm
        async command PING_LINK_RSSI

        @ Current link state
        telemetry LinkState: U32

        @ Pending science bytes
        telemetry PendingScienceBytes: U32

        @ Number of explicit link polls
        telemetry LinkPollCount: U32

        @ Latest RF link RSSI in dBm
        telemetry RssiDbm: I32

        @ Downlink request event
        event DownlinkRequested(bytes: U32) severity activity high format "Downlink requested for {} bytes"

        @ Downlink completion event for the current synchronous/demo downlink path
        event DownlinkFinished(bytes: U32) severity activity high format "Downlink finished for {} bytes"

        @ Downlink failure or rejected request event
        event DownlinkFailed(stateValue: U32, lastError: U32) severity warning low format "Downlink failed state={} error={}"

        @ Link state event
        event LinkStateUpdated(linkState: U32, rssiDbm: I32) severity activity low format "Comms link state updated {} rssi={}dBm"

        @ RSSI ping event
        event LinkRssiPing(linkState: U32, rssiDbm: I32, pollCount: U32) severity activity high format "Comms link RSSI ping state={} rssi={}dBm polls={}"

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
