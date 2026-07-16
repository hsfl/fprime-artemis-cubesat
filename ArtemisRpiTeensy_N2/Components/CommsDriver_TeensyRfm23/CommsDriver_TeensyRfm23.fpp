module Components {
    @ Teensy/RFM23 transport hardware driver shim.
    passive component CommsDriver_TeensyRfm23 {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate-group input used to bound one pending channel-2 request
        guarded input port run: Svc.Sched

        @ Typed radio control request from CommsApp
        guarded input port requestIn: Components.RadioControlRequest

        @ Teensy-local RF status response input from UART channel 2.
        guarded input port teensyResponseIn: Fw.BufferSend

        @ Correlated RPC result and factual radio status
        output port statusOut: Components.RadioStatus

        @ Satellite RF receive counter snapshot for existing transport observability
        output port rfRxCountOut: Svc.Ping

        @ Teensy-local RF status request output to UART channel 2.
        output port teensyRequestOut: Fw.BufferSend

        @ Last requested radio operation
        telemetry LastOperation: Components.RadioOperation

        @ Number of processed driver requests
        telemetry RequestCount: U32

        @ Whether a Teensy-local request is awaiting a response
        telemetry RequestPending: U32 update on change

        @ Age of the pending request in one-second scheduler ticks
        telemetry PendingRequestTicks: U32

        @ Number of timed out, malformed, or locally rejected RPC requests
        telemetry RpcFailureCount: U32

        @ Number of stale, mismatched, or unsolicited responses rejected
        telemetry RejectedResponseCount: U32

        @ Factual RFM23BP hardware state
        telemetry RadioState: Components.RadioState update on change

        @ Last factual RFM23BP fault
        telemetry RadioFault: Components.RadioFault update on change

        @ Teensy boot flags associated with the latest status response
        telemetry RadioBootFlags: U32 update on change

        @ Number of radio initialization attempts reported by the Teensy
        telemetry RadioInitAttempts: U32 update on change

        @ Whether RSSI is backed by an accepted addressed packet
        telemetry RssiValid: U32 update on change

        @ RF RSSI in dBm reported by the satellite Teensy RadioHead driver
        telemetry RssiDbm: I32 update on change \
            low { yellow -100, orange -110, red -120 }

        @ Age in milliseconds of the last accepted RSSI sample
        telemetry RssiAgeMs: U32 update on change

        @ RF receive packet counter reported by the satellite Teensy
        telemetry RfRxPackets: U32 update on change

        @ RF transmit packet counter reported by the satellite Teensy
        telemetry RfTxPackets: U32 update on change

        @ RF transmit drop counter reported by the satellite Teensy
        telemetry RfTxDrops: U32 update on change

        @ A radio RPC was queued on channel 2.
        event RadioRequestQueued(operation: Components.RadioOperation, requestId: U32) severity activity low \
            format "Teensy/RFM23 request={} queued id={}" throttle 10

        @ A radio RPC failed locally or returned a non-success result.
        event RadioRequestFailed(operation: Components.RadioOperation, result: Components.RadioRpcResult) \
            severity warning low format "Teensy/RFM23 request={} failed result={}" throttle 5

        @ A pending radio RPC exceeded its bounded wait.
        event RadioRequestTimedOut(operation: Components.RadioOperation, requestId: U32) \
            severity warning low format "Teensy/RFM23 request={} timed out id={}" throttle 5

        @ A stale, malformed, or unsolicited response was rejected.
        event RadioResponseRejected(reason: U32, receivedId: U32, expectedId: U32) \
            severity warning low format "Teensy/RFM23 response rejected reason={} received={} expected={}" throttle 5

        @ Factual radio state transition event
        event RadioStateChanged(previousState: Components.RadioState, currentState: Components.RadioState, fault: Components.RadioFault) \
            severity activity high format "Teensy/RFM23 radio state {} -> {} fault={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
