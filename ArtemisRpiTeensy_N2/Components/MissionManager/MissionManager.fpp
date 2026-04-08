module Components {
    @ Mission-level mode and collection scheduler manager.
    active component MissionManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Collection scheduling output to ScienceManager
        output port collectionRequestOut: Svc.Ping

        @ Enter base mode
        async command ENTER_BASE_MODE

        @ Simple local ping command for manual GDS loop checks
        async command PING(token: U32)

        @ Schedule data collection delay in seconds
        async command SCHEDULE_COLLECTION(delaySeconds: U32)

        @ Current mission mode
        telemetry CurrentMode: U32

        @ Last requested schedule delay
        telemetry LastScheduledDelaySeconds: U32

        @ Number of pings handled
        telemetry PingCount: U32

        @ Mission manager heartbeat
        telemetry ModeHeartbeat: U32

        @ Mission mode transition
        event ModeChanged(mode: U32) severity activity high format "Mission mode changed to {}"

        @ Ping response event
        event Pong(token: U32, count: U32) severity activity low format "MissionManager pong token={} count={}"

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
