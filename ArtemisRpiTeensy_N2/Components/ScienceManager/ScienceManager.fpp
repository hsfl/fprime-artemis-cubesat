module Components {
    @ Coordinates collection triggers and science-product handoff.
    active component ScienceManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Collection request input from MissionManager
        async input port requestIn: Components.CollectionRequest

        @ Cancel a pending collection request from MissionManager
        async input port cancelRequestIn: Svc.Ping

        @ Payload status input from PayloadService
        async input port payloadStatusIn: Components.ScienceProductDescriptor

        @ Forwarded collection request to PayloadService
        output port payloadRequestOut: Components.PayloadCaptureRequest

        @ Science product handoff to StorageService
        output port scienceProductOut: Components.ScienceProductDescriptor

        @ Mission mode update output
        output port missionModeOut: Components.MissionModeUpdate

        @ Trigger immediate collection
        async command START_COLLECTION

        @ Set the capture duration used by scheduled collections
        async command CONFIGURE_CAPTURE_DURATION(durationSeconds: U32)

        @ Trigger one immediate collection for the requested duration. Does not change the configured default duration.
        async command SCIENCE_CAPTURE(durationSeconds: U32)

        @ Boot-default capture duration. Runtime CONFIGURE_CAPTURE_DURATION remains volatile.
        param CAPTURE_DURATION_SECONDS: U32 default 30

        @ Pending delay before collection
        telemetry PendingDelaySeconds: U32

        @ Capture duration in seconds used for scheduled collection
        telemetry CaptureDurationSeconds: U32 update on change

        @ Number of completed collections
        telemetry CollectionCount: U32 update on change

        @ Collection trigger event
        event CollectionTriggered(delaySeconds: U32) severity activity high format "Science collection triggered delay={}s"

        @ Product handoff event
        event ScienceProductReady(productSize: U32) severity activity high format "Science product ready size={}"

        @ Capture duration configuration event
        event CaptureDurationConfigured(durationSeconds: U32) severity activity high format "Science capture duration configured {}s"

        @ Collection cancel event
        event CollectionCancelled(remainingSeconds: U32) severity activity high format "Science collection cancelled with {}s remaining"

        @ Science command or request rejected by duration/delay guard
        event ScienceCommandRejected(reason: U32, value: U32) severity warning low format "Science command rejected reason={} value={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel

        @ Parameter get port
        param get port prmGetOut

        @ Parameter set port
        param set port prmSetOut
    }
}
