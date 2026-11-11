module Components {
    @ Payload manager exposing stable mission-facing behavior.
    active component PayloadManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        async input port run: Svc.Sched

        @ Collection request input from ScienceApp
        async input port requestIn: Components.PayloadCaptureRequest

        @ Correlated simulated-driver operation result
        async input port driverOperationResultIn: Components.PayloadDriverOperationResult

        @ Current simulated-driver capture capacity
        async input port driverAvailabilityIn: Components.PayloadDriverAvailability

        @ MissionApp's capture control. While inhibited, no new capture or pass-through starts.
        async input port captureControlIn: Components.OperationControl

        @ Report back to MissionApp that a capture control is applied
        output port captureControlReportOut: Components.OperationControlReport

        @ Driver request output
        output port driverRequestOut: Components.PayloadDriverCaptureRequest

        @ Opaque synthetic pass-through request output to the payload driver
        output port driverPassthroughOut: Components.PayloadDriverPassthroughRequest

        @ Admission and completion notices consumed by ScienceApp
        output port operationNoticeOut: Components.PayloadOperationNotice

        @ Status output to SoH manager
        output port sohStatusOut: Components.HealthStatus

        @ Request latest payload status from driver
        async command REQUEST_PAYLOAD_STATUS

        @ Configure placeholder collection settings
        async command CONFIGURE_PAYLOAD(sampleCount: U32, periodMs: U32)

        @ Start a payload collection by collection ID
        async command START_PAYLOAD_COLLECTION(collectionId: U32)

        @ Capture neutron samples for the requested duration in seconds
        async command SCIENCE_CAPTURE(durationSeconds: U32)

        @ Forward an opaque printable ASCII literal to the board-free synthetic payload facade.
        @ Embedded NUL and non-ASCII input are outside this demo contract and are not validated here.
        @ This does not encode or assume any real payload command protocol.
        async command SYNTHETIC_PASSTHROUGH(
            requestId: U32,
            literalCommand: string size PAYLOAD_PASSTHROUGH_LITERAL_MAX
        )

        @ Configure one local-Yamcs simulator fault for the next admitted operation.
        @ This command is rejected unless the deployment is built with NEUTRON2_LOCAL_YAMCS.
        async command CONFIGURE_SIMULATION(
            faultMode: Components.PayloadSimulationFault,
            delayMs: U32,
            timeoutMs: U32
        )

        @ Last payload sample value
        telemetry LastPayloadValue: U32

        @ Last collection ID requested
        telemetry LastCollectionId: U32

        @ Last requested capture duration in seconds
        telemetry LastCaptureDurationSeconds: U32

        @ Configured sample count
        telemetry SampleCount: U32

        @ Configured sample period in milliseconds
        telemetry SamplePeriodMs: U32

        @ Manager heartbeat
        telemetry ManagerHeartbeat: U32

        @ Whether the simulated driver's capture subprocess slot is idle and reusable
        telemetry PayloadDriverAvailable: bool

        @ Whether a manager operation is admitted and awaiting a terminal result
        telemetry PayloadOperationInFlight: bool

        @ Latest manager-owned operation generation token
        telemetry LastOperationGeneration: U32

        @ Whether MissionApp's capture control is inhibiting new operations
        telemetry CaptureInhibited: bool

        @ Payload request forwarded to driver
        event PayloadCollectionForwarded(requestKey: U32) severity activity high format "Payload request forwarded key={}"

        @ Payload status update event
        event PayloadStatusUpdated(statusKey: U32) severity activity low format "Payload status updated key={}"

        @ Payload configuration event
        event PayloadConfigured(sampleCount: U32, periodMs: U32) severity activity high format "Payload configured samples={} periodMs={}"

        @ Payload science capture event
        event PayloadScienceCaptureRequested(durationSeconds: U32) severity activity high format "Payload science capture requested duration={}s"

        @ Literal pass-through accepted by the manager and sent to the synthetic driver.
        event SyntheticPassthroughForwarded(
            requestId: U32,
            literalCommand: string size PAYLOAD_PASSTHROUGH_LITERAL_MAX
        ) severity activity high format "Synthetic payload pass-through forwarded request={} literal='{}'"

        @ Synthetic pass-through command rejected before it reached the driver.
        event SyntheticPassthroughRejected(requestId: U32, reason: U32) severity warning high format "Synthetic payload pass-through rejected request={} reason={}"

        @ Correlated response returned by the synthetic payload driver with the exact literal echo.
        event SyntheticPassthroughResponse(
            requestId: U32,
            status: Components.PayloadPassthroughStatus,
            literalEcho: string size PAYLOAD_PASSTHROUGH_LITERAL_MAX
        ) severity activity high format "Synthetic payload response request={} status={} literal='{}'"

        @ Admission, rejection, and terminal status for a serialized operation.
        event PayloadOperationStatus(
            requestId: U32,
            generation: U32,
            kind: Components.PayloadOperationKind,
            $state: Components.PayloadOperationState
        ) severity activity high format "Payload operation request={} generation={} kind={} state={}"

        @ Driver response dropped because it is stale or duplicated.
        event PayloadReplyIgnored(requestId: U32, generation: U32, reason: U32) severity warning low format "Ignored payload reply request={} generation={} reason={}"

        @ Local simulator fault configuration accepted.
        event SimulationConfigured(
            faultMode: Components.PayloadSimulationFault,
            delayMs: U32,
            timeoutMs: U32
        ) severity activity high format "Payload simulator configured fault={} delayMs={} timeoutMs={}"

        @ Simulator configuration rejected because it is unsupported or unsafe at this time.
        event SimulationConfigRejected(reason: U32) severity warning low format "Payload simulator configuration rejected reason={}"

        @ A capture control from MissionApp was applied. A running operation is left to finish.
        event CaptureControlApplied(control: Fw.Enabled, activeOperation: bool) severity activity high format "Payload capture control {} applied, operation running={}"

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
