module Components {
    @ Teensy/RFM23 transport hardware adapter shim.
    passive component CommsAdapter_TeensyRfm23 {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Adapter request input
        sync input port requestIn: Svc.Ping

        @ Teensy-local RF status response input from UART channel 2.
        sync input port teensyResponseIn: Fw.BufferSend

        @ Adapter status output
        output port statusOut: [2] Svc.Ping

        @ Current RSSI output
        output port rssiStatusOut: Components.RssiStatus

        @ Teensy-local RF status request output to UART channel 2.
        output port teensyRequestOut: Fw.BufferSend

        @ Last handled request key
        telemetry LastRequestKey: U32

        @ Number of processed adapter requests
        telemetry RequestCount: U32

        @ Current link state from Teensy transport poll contract (0=down, 1=acquiring, 2=locked, 3=degraded)
        telemetry LinkState: U32 update on change

        @ RF RSSI in dBm reported by the satellite Teensy RadioHead driver
        telemetry RssiDbm: I32 update on change \
            low { yellow -100, orange -110, red -120 }

        @ RF receive packet counter reported by the satellite Teensy
        telemetry RfRxPackets: U32 update on change

        @ RF transmit packet counter reported by the satellite Teensy
        telemetry RfTxPackets: U32 update on change

        @ RF transmit drop counter reported by the satellite Teensy
        telemetry RfTxDrops: U32 update on change

        @ Request handling event; keep throttled because polling paths can storm.
        event RequestHandled(requestKey: U32, statusKey: U32) severity activity low format "Teensy/RFM23 transport request={} status={}" throttle 10

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
