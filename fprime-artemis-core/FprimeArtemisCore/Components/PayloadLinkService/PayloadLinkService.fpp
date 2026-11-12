module Components {
  @ Pi-side science transaction service. Inputs only enqueue/cache work;
  @ run is the sole producer of science frames and capture requests.
  passive component PayloadLinkService {
    guarded input port messageIn: TwinScience.Message
    output port messageOut: TwinScience.Message
    output port captureOut: Components.PayloadCaptureRequest
    @ Asynchronous manager/driver cancellation; capture ownership is retained
    @ until the driver reports a terminal descriptor and fresh readiness.
    output port cancelOut: Fw.Signal
    @ Dedicated local completion barrier; ordinary product callbacks cannot
    @ acknowledge an asynchronously queued cancellation.
    guarded input port cancelDoneIn: Fw.Signal
    guarded input port productIn: Components.ScienceProductDescriptor
    guarded input port stateIn: Components.PayloadStateReport
    @ Exactly 100 Hz; retry deadlines are expressed in these ticks.
    guarded input port run: Svc.Sched
    telemetry CaptureRequests: U32 update on change
    telemetry CommittedProducts: U32 update on change
    telemetry StagedBytes: U32 update on change
    telemetry ProtocolErrors: U32 update on change
    event ScienceServiceError(code: U8, requestId: U32) severity warning high format "Pi science error={} request={}"
    event ScienceProductCommitted(productId: U32, bytes: U32) severity activity high format "Pi committed product={} bytes={}"
    event ScienceCacheReady(productId: U32, generation: U32) severity activity high format "FC cache ready product={} generation={}"
    time get port timeCaller
    import Fw.Event
    import Fw.Channel
  }
}
