module Components {
    @ Artemis thermal hardware driver shim.
    passive component ThermalDriver_Artemis {

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

        @ Number of processed driver requests
        telemetry RequestCount: U32

        @ Current thermal state (0=nominal, 1=warming, 2=hot, 3=cold)
        telemetry ThermalState: U32

        @ OBC board temperature estimate in centi-degrees C
        telemetry ObcTempCentiC: I32

        @ Battery temperature estimate in centi-degrees C
        telemetry BatteryTempCentiC: I32

        @ Request handling event
        event RequestHandled(requestKey: U32, statusKey: U32) severity activity low format "Artemis thermal request={} status={}"

        @ Thermal state transition event
        event ThermalStateChanged(previousState: U32, currentState: U32) severity activity high format "Thermal state {} -> {}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
