module Components {
    @ Mission-level mode and collection scheduler manager.
    active component MissionApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Collection scheduling output to ScienceApp
        output port collectionRequestOut: Components.CollectionRequest

        @ Collection cancellation output to ScienceApp
        output port cancelRequestOut: Svc.Ping

        @ Mission mode update input from story managers
        async input port modeUpdateIn: [2] Components.MissionModeUpdate

        @ Enter base mode
        async command ENTER_BASE_MODE

        @ Simple local ping command for manual GDS loop checks
        async command PING(token: U32)

        @ Schedule data collection delay in seconds
        async command SCHEDULE_COLLECTION(delaySeconds: U32)

        @ Cancel pending collection and return to base mode
        async command CANCEL_COLLECTION

        @ Current mission mode
        telemetry CurrentMode: Components.MissionMode update on change

        @ Last requested schedule delay
        telemetry LastScheduledDelaySeconds: U32 update on change

        @ Number of pings handled
        telemetry PingCount: U32 update on change

        @ Mission manager heartbeat
        telemetry ModeHeartbeat: U32

        @ Mission mode transition
        event ModeChanged(mode: Components.MissionMode) severity activity high format "Mission mode changed to {}"

        @ Invalid manager-requested mission mode transition rejected; keep throttled because port paths can storm.
        event ModeUpdateRejected(requested: Components.MissionMode, current: Components.MissionMode, detail: U32) severity warning low format "Rejected mode update requested={} current={} detail={}" throttle 5

        @ Mission command rejected by validation guard
        event MissionCommandRejected(reason: U32, value: U32) severity warning low format "Mission command rejected reason={} value={}"

        @ Ping response event
        event Pong(token: U32, count: U32) severity activity low format "MissionApp pong token={} count={}"

        @ Collection scheduling event
        event CollectionScheduled(delaySeconds: U32) severity activity high format "Collection scheduled in {}s"

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
