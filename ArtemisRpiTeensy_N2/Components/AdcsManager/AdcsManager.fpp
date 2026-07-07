module Components {
    @ ADCS manager exposing stable mission-facing behavior.
    active component AdcsManager {

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

        @ Request latest ADCS status from driver
        async command REQUEST_ADCS_STATUS

        @ Set placeholder ADCS mode. 0=idle, 1=detumble, 2=pointing.
        async command SET_ADCS_MODE(mode: U32)

        @ Request attitude update from driver or simulator
        async command REQUEST_ATTITUDE_UPDATE(requestKey: U32)

        @ Current ADCS state
        telemetry AdcsState: U32

        @ Requested ADCS mode
        telemetry AdcsMode: U32

        @ Last attitude update request key
        telemetry AttitudeRequestKey: U32

        @ Manager heartbeat
        telemetry ManagerHeartbeat: U32

        @ ADCS status update event
        event AdcsStatusUpdated(statusKey: U32) severity activity low format "ADCS status updated state={}"

        @ ADCS mode update event
        event AdcsModeUpdated(mode: U32) severity activity high format "ADCS mode updated mode={}"

        @ Attitude update request event
        event AttitudeUpdateRequested(requestKey: U32) severity activity high format "ADCS attitude update requested key={}"

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
