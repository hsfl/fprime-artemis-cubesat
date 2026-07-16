module Components {
    @ Mission-level communications state and downlink manager.
    active component CommsApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Science ready input from storage
        async input port scienceReadyIn: Components.ScienceDownlinkReady

        @ Correlated radio RPC result and factual radio status
        async input port driverStatusIn: Components.RadioStatus

        @ Payload downlink transfer status input
        async input port payloadDownlinkStatusIn: Components.PayloadDownlinkStatus

        @ Downlink request output to storage
        output port downlinkRequestOut: Components.ScienceDownlinkRequest

        @ Generic payload downlink request output
        output port payloadDownlinkRequestOut: Components.ScienceDownlinkRequest

        @ Typed radio request output
        output port driverRequestOut: Components.RadioControlRequest

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

        @ Desired radio enable policy; one means F Prime will recover it autonomously
        telemetry DesiredRadioEnabled: U32 update on change

        @ Whether at least one factual radio response has been received
        telemetry RadioStatusKnown: U32 update on change

        @ Factual RFM23BP hardware state
        telemetry RadioState: Components.RadioState update on change

        @ Last factual local RFM23BP fault
        telemetry RadioFault: Components.RadioFault update on change

        @ Most recent channel-2 RPC result
        telemetry RadioRpcResult: Components.RadioRpcResult update on change

        @ Consecutive failed automatic recovery attempts
        telemetry RadioRecoveryFailures: U32 update on change

        @ Approximate seconds remaining until the next recovery attempt
        telemetry RadioRetrySeconds: U32 update on change

        @ Radio initialization attempts reported by the Teensy
        telemetry RadioInitAttempts: U32 update on change

        @ Pending science bytes
        telemetry PendingScienceBytes: U32 update on change

        @ Number of explicit link polls
        telemetry LinkPollCount: U32

        @ Whether a science downlink request is currently active.
        telemetry DownlinkActive: U32 update on change

        @ Product identifier owned by the active science downlink.
        telemetry ActiveDownlinkProductId: U32 update on change

        @ Payload transfer identifier latched from active-transfer status.
        telemetry ActiveDownlinkTransferId: U32 update on change

        @ Latest request disposition: 0=accepted/none, 1=idempotent duplicate, 2=conflict rejected.
        telemetry DownlinkRequestDisposition: U32 update on change

        @ Latest RF link RSSI in dBm
        telemetry RssiDbm: I32 update on change \
            low { yellow -100, orange -110, red -120 }

        @ Whether RSSI is backed by an accepted addressed packet
        telemetry RssiValid: U32 update on change

        @ Age in milliseconds of the last accepted RSSI sample
        telemetry RssiAgeMs: U32 update on change

        @ Downlink request event
        event DownlinkRequested(bytes: U32) severity activity high format "Downlink requested for {} bytes"

        @ Downlink completion event for the current synchronous/demo downlink path
        event DownlinkFinished(bytes: U32) severity activity high format "Downlink finished for {} bytes"

        @ Repeated command for the exact active product was accepted idempotently without new fan-out.
        event DownlinkRequestDuplicate(productId: U32, activeBytes: U32) severity activity low \
            format "Downlink request duplicate product={} activeBytes={}"

        @ Request conflicts with a different active product descriptor.
        event DownlinkRequestConflict(activeProductId: U32, requestedProductId: U32) severity warning low \
            format "Downlink request conflict activeProduct={} requestedProduct={}"

        @ Payload status did not identify the transfer currently owned by communications.
        event PayloadDownlinkStatusIgnored(activeTransferId: U32, receivedTransferId: U32, productId: U32) \
            severity warning low format "Payload status ignored activeTransfer={} receivedTransfer={} product={}" throttle 5

        @ Operator command rejected by validation guard
        event CommsCommandRejected(reason: U32, value: U32) severity warning low format "Comms command rejected reason={} value={}"

        @ Downlink failure from payload transfer/status paths; keep throttled because RF/status paths can storm.
        event DownlinkFailed(stateValue: U32, lastError: U32) severity warning low format "Downlink failed state={} error={}" throttle 5

        @ Factual radio status changed or an explicit operator status poll completed.
        event RadioStatusUpdated(radioState: Components.RadioState, radioFault: Components.RadioFault, result: Components.RadioRpcResult) \
            severity activity low format "Comms radio state={} fault={} result={}" throttle 10

        @ A bounded autonomous recovery retry was scheduled.
        event RadioRecoveryScheduled(failures: U32, retrySeconds: U32, fault: Components.RadioFault, result: Components.RadioRpcResult) \
            severity warning low format "Comms radio recovery failures={} retry={}s fault={} result={}" throttle 5

        @ Radio service returned to READY after one or more failed attempts.
        event RadioRecovered(recoveryFailures: U32) severity activity high format "Comms radio recovered after {} failed attempts"

        @ RSSI ping event
        event LinkRssiPing(radioState: Components.RadioState, rssiValid: U32, rssiDbm: I32, rssiAgeMs: U32, pollCount: U32) \
            severity activity high format "Comms radio RSSI state={} valid={} rssi={}dBm age={}ms polls={}"

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
