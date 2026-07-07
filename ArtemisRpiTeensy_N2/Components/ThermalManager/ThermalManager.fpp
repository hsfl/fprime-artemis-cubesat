module Components {
    @ Thermal manager exposing stable mission-facing behavior.
    active component ThermalManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Driver status input
        sync input port driverStatusIn: Svc.Ping

        @ Driver request output
        output port driverRequestOut: Svc.Ping

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Request latest thermal status from driver
        async command REQUEST_THERMAL_STATUS

        @ Set placeholder thermal control mode. 0=off, 1=observe, 2=heater-auto.
        async command SET_THERMAL_MODE(mode: U32)

        @ Current thermal state
        telemetry ThermalState: U32

        @ Requested thermal control mode
        telemetry ThermalMode: U32

        @ Manager heartbeat
        telemetry ManagerHeartbeat: U32

        @ Thermal status update event
        event ThermalStatusUpdated(statusKey: U32) severity activity low format "Thermal status updated state={}"

        @ Thermal mode update event
        event ThermalModeUpdated(mode: U32) severity activity high format "Thermal mode updated mode={}"

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
