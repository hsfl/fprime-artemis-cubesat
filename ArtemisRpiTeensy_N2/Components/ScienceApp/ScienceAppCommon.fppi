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

@ Forwarded collection request to PayloadManager
output port payloadRequestOut: Components.PayloadCaptureRequest

@ Optional duration configuration request for a configured payload.
output port durationConfigOut: Components.PayloadDurationConfigRequest

@ Terminal result for a duration configuration request.
async input port durationConfigResultIn: Components.PayloadDurationConfigResult

@ Optional trigger for a capture using the payload's configured duration.
output port configuredCaptureOut: Components.PayloadConfiguredCaptureRequest

@ Science product handoff to StorageManager
output port scienceProductOut: Components.ScienceProductDescriptor

@ Opt-in request to deliver the just-stored product through CommsApp.
output port deliveryRequestOut: Components.ScienceProductDeliveryRequest

@ Mission mode update output
output port missionModeOut: Components.MissionModeUpdate

@ Trigger immediate collection
async command START_COLLECTION opcode 0

@ Set the configured capture duration, through PayloadManager when connected.
async command CONFIGURE_CAPTURE_DURATION(durationSeconds: U32) opcode 1

@ Capture one product and automatically request its downlink after StorageManager validates and stores it.
async command COLLECT_AND_DOWNLINK(durationSeconds: U32) opcode 3

@ Token-correlated capture and automatic downlink for Yamcs command-history verification.
async command VERIFIED_COLLECT_AND_DOWNLINK(verificationToken: U32, durationSeconds: U32) opcode 4

@ Local boot default for profiles that set duration in ScienceApp.
param CAPTURE_DURATION_SECONDS: U32 default 30 set opcode 5 save opcode 6

@ Pending delay before collection
telemetry PendingDelaySeconds: U32

@ Local capture default or last confirmed payload duration in seconds.
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
