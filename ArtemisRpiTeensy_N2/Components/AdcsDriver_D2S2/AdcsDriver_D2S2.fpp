module Components {
    @ D2S2 ADCS hardware driver shim.
    passive component AdcsDriver_D2S2 {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Driver request input
        sync input port requestIn: Svc.Ping

        @ Driver status output
        output port statusOut: Svc.Ping

        @ Last handled request key
        telemetry LastRequestKey: U32

        @ Request handling event
        event RequestHandled(requestKey: U32, statusKey: U32) severity activity low format "D2S2 ADCS request={} status={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
