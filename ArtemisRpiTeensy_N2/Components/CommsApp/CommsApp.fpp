module Components {
    @ Mission-level communications state and downlink manager.
    active component CommsApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Link status input from transport
        async input port linkStatusIn: Svc.Ping

        @ Science ready input from storage
        async input port scienceReadyIn: Components.ScienceDownlinkReady

        @ Driver status input
        async input port driverStatusIn: Svc.Ping

        @ Driver RSSI input
        async input port rssiStatusIn: Components.RssiStatus

        @ Payload downlink transfer status input
        async input port payloadDownlinkStatusIn: Components.PayloadDownlinkStatus

        @ Downlink request output to storage
        output port downlinkRequestOut: Components.ScienceDownlinkRequest

        @ Generic payload downlink request output
        output port payloadDownlinkRequestOut: Components.ScienceDownlinkRequest

        @ Driver request output
        output port driverRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Mission mode update output
        output port missionModeOut: Components.MissionModeUpdate

        @ Request science downlink
        async command REQUEST_SCIENCE_DOWNLINK

        @ Request latest link status from the selected radio driver
        async command REQUEST_LINK_STATUS

        @ Poll the radio driver and log current RSSI in dBm
        async command PING_LINK_RSSI

        @ Current link state
        telemetry LinkState: U32 update on change

        @ Pending science bytes
        telemetry PendingScienceBytes: U32 update on change

        @ Number of explicit link polls
        telemetry LinkPollCount: U32

        @ Latest RF link RSSI in dBm
        telemetry RssiDbm: I32 update on change \
            low { yellow -100, orange -110, red -120 }

        @ Downlink request event
        event DownlinkRequested(bytes: U32) severity activity high format "Downlink requested for {} bytes"

        @ Downlink completion event for the current synchronous/demo downlink path
        event DownlinkFinished(bytes: U32) severity activity high format "Downlink finished for {} bytes"

        @ Operator command rejected by validation guard
        event CommsCommandRejected(reason: U32, value: U32) severity warning low format "Comms command rejected reason={} value={}"

        @ Downlink failure from payload transfer/status paths; keep throttled because RF/status paths can storm.
        event DownlinkFailed(stateValue: U32, lastError: U32) severity warning low format "Downlink failed state={} error={}" throttle 5

        @ Link state event from polling/status paths; keep throttled because RF/status paths can storm.
        event LinkStateUpdated(linkState: U32, rssiDbm: I32) severity activity low format "Comms link state updated {} rssi={}dBm" throttle 10

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
