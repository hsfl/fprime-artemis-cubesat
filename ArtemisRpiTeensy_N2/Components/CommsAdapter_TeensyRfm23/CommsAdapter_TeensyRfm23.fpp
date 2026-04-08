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

        @ Request handling event
        event RequestHandled(requestKey: U32, statusKey: U32) severity activity low format "Teensy/RFM23 transport request={} status={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
