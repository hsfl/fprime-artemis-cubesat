module Components {
    @ Teensy/RFM23 transport hardware adapter shim.
    passive component CommsAdapter_TeensyRfm23 {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Adapter request input
        sync input port requestIn: Svc.Ping

        @ Adapter status output
        output port statusOut: [2] Svc.Ping

        @ Last handled request key
        telemetry LastRequestKey: U32

        @ Number of processed adapter requests
        telemetry RequestCount: U32

        @ Current modeled link state (0=down, 1=acquiring, 2=locked, 3=degraded)
        telemetry LinkState: U32

        @ Synthetic RF RSSI estimate in dBm
        telemetry RssiDbm: I32

        @ Synthetic RF receive packet counter
        telemetry RfRxPackets: U32

        @ Synthetic RF transmit packet counter
        telemetry RfTxPackets: U32

        @ Synthetic RF transmit drop counter
        telemetry RfTxDrops: U32

        @ Request handling event
        event RequestHandled(requestKey: U32, statusKey: U32) severity activity low format "Teensy/RFM23 transport request={} status={}"

        @ Link state transition event
        event LinkStateChanged(previousState: U32, currentState: U32) severity activity high format "Teensy/RFM23 link state {} -> {}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
