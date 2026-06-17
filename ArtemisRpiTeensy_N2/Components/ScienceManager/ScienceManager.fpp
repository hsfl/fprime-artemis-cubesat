module Components {
    @ Coordinates collection triggers and science-product handoff.
    active component ScienceManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Collection request input from MissionManager
        sync input port requestIn: Svc.Ping

        @ Payload status input from PayloadService
        sync input port payloadStatusIn: Svc.Ping

        @ Forwarded collection request to PayloadService
        output port payloadRequestOut: Svc.Ping

        @ Science product handoff to StorageService
        output port scienceProductOut: Svc.Ping

        @ Trigger immediate collection
        async command START_COLLECTION

        @ Set the capture duration used by scheduled collections
        async command CONFIGURE_CAPTURE_DURATION(durationSeconds: U32)

        @ Trigger immediate collection for the requested duration
        async command SCIENCE_CAPTURE(durationSeconds: U32)

        @ Pending delay before collection
        telemetry PendingDelaySeconds: U32

        @ Capture duration in seconds used for scheduled collection
        telemetry CaptureDurationSeconds: U32

        @ Number of completed collections
        telemetry CollectionCount: U32

        @ Collection trigger event
        event CollectionTriggered(delaySeconds: U32) severity activity high format "Science collection triggered delay={}s"

        @ Product handoff event
        event ScienceProductReady(productSize: U32) severity activity high format "Science product ready size={}"

        @ Capture duration configuration event
        event CaptureDurationConfigured(durationSeconds: U32) severity activity high format "Science capture duration configured {}s"

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
