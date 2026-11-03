module Components {
    @ Where a capture refused in SAFE came from (ScienceInhibited event).
    enum ScienceInhibitSource : U8 {
        START_COLLECTION = 0
        SCIENCE_CAPTURE = 1
        COLLECT_AND_DOWNLINK = 2
        VERIFIED_COLLECT_AND_DOWNLINK = 3
        @ A MissionApp SCHEDULE_COLLECTION request on requestIn
        SCHEDULED_REQUEST = 4
        @ A scheduled countdown that reached zero
        SCHEDULED_COUNTDOWN = 5
    }

    @ Coordinates collection triggers and science-product handoff.
    active component ScienceApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Collection request input from MissionApp
        async input port requestIn: Components.CollectionRequest

        @ Cancel a pending collection request from MissionApp
        async input port cancelRequestIn: Svc.Ping

        @ Correlated admission/completion/failure status from PayloadManager
        async input port operationNoticeIn: Components.PayloadOperationNotice

        @ MissionApp's mode, sent on every change and at start-up. In SAFE every capture is refused
        @ (SDD v4.0 Table 5: science inhibited in Safe).
        async input port missionModeIn: Components.MissionModeUpdate

        @ Forwarded collection request to PayloadManager
        output port payloadRequestOut: Components.PayloadCaptureRequest

        @ Science product handoff to StorageManager
        output port scienceProductOut: Components.ScienceProductDescriptor

        @ Opt-in request to deliver the just-stored product through CommsApp.
        output port deliveryRequestOut: Components.ScienceProductDeliveryRequest

        @ Mission mode update output
        output port missionModeOut: Components.MissionModeUpdate

        @ Trigger immediate collection
        async command START_COLLECTION

        @ Set the capture duration used by scheduled collections
        async command CONFIGURE_CAPTURE_DURATION(durationSeconds: U32)

        @ Trigger one immediate collection for the requested duration. Does not change the configured default duration.
        async command SCIENCE_CAPTURE(durationSeconds: U32)

        @ Capture one product and automatically request its downlink after StorageManager validates and stores it.
        async command COLLECT_AND_DOWNLINK(durationSeconds: U32)

        @ Token-correlated capture and automatic downlink for Yamcs command-history verification.
        async command VERIFIED_COLLECT_AND_DOWNLINK(verificationToken: U32, durationSeconds: U32)

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

        @ Payload operation failed, timed out, or was rejected before capture.
        event ScienceOperationRejected(requestId: U32, $state: Components.PayloadOperationState) severity warning high format "Science payload operation request={} state={}"

        @ Token-bound admission and terminal result; FSW generation is internal to PayloadManager.
        event VerifiedCaptureStatus(
            verificationToken: U32,
            requestId: U32,
            generation: U32,
            $state: Components.PayloadOperationState,
            productId: U32,
            productBytes: U32,
            sourceCrc: U32,
            reasonCode: U32
        ) severity activity high format "Verified capture token={} request={} generation={} state={} product={} bytes={} crc={} reason={}"

        @ A capture was refused because MissionApp is in SAFE, where science is inhibited (SDD v4.0 Table 5).
        @ ScienceApp owns the capture commands, so the mode check sits here (SDD v4.0 §4.2).
        event ScienceInhibited(source: Components.ScienceInhibitSource) severity warning low format "Science capture refused: SAFE mode inhibits science, source={}"

        @ MissionApp moved into or out of SAFE. inhibited = 1 while SAFE inhibits science.
        event ScienceInhibitChanged(inhibited: U8, mode: Components.MissionMode) severity activity high format "Science inhibit={} in mission mode {}"

        @ A capture is running during SAFE. ScienceApp cannot stop it: PayloadManager owns the capture and
        @ the EPS rail owns payload power. Its product will be withheld when it ends.
        event CaptureRunningInSafe(requestId: U32) severity warning high format "Capture request {} is running in SAFE and cannot be stopped from ScienceApp: its product will be withheld"

        @ A capture that ran during SAFE ended. Its product is withheld: not handed to StorageManager, not
        @ reported as SCIENCE_READY and not sent. The attempt stays partial (SDD v4.0 §5.2) and the payload's
        @ file stays where the payload wrote it.
        event CaptureWithheld(requestId: U32, productId: U32, productBytes: U32) severity warning high format "Capture request {} ran during SAFE: product {} ({} bytes) withheld, not stored or sent"

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
