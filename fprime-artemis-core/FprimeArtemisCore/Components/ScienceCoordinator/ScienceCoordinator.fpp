module Components {
  enum ScienceJobState: U8 { IDLE, SCHEDULED, CAPTURING, STAGING, CACHE_READY, DOWNLINKING, SENT, FAILED, CANCELLED }
  @ FC-owned science workflow. Pi owns files; the supplied FC RAM owns a complete cached product.
  passive component ScienceCoordinator {
    guarded input port run: Svc.Sched
    guarded input port admissionIn: TwinScience.Admission
    guarded input port payloadStateIn: Components.PayloadStateReport
    guarded input port messageIn: TwinScience.Message
    output port messageOut: TwinScience.Message
    guarded input port scheduleIn: TwinScience.Schedule
    guarded input port cancelIn: TwinScience.Cancel
    guarded input port downlinkIn: TwinScience.Downlink
    guarded input port radioNext: Components.RadioScienceNext
    guarded input port radioSent: Components.RadioScienceSent
    guarded input port radioRetry: Components.RadioScienceRetry
    guarded command CONFIGURE_CAPTURE_DURATION(durationSeconds: U32)
    guarded command REQUEST_STATUS
    @ Explicitly restore a retained Pi product after FC reset; no new capture.
    guarded command RESTAGE_PRODUCT(productId: U32)
    telemetry JobState: ScienceJobState update on change
    telemetry ProductId: U32 update on change
    telemetry ProductBytes: U32 update on change
    telemetry CacheReceivedBytes: U32 update on change
    telemetry CacheValid: bool update on change
    telemetry CachedProductId: U32 update on change
    telemetry CachedProductBytes: U32 update on change
    telemetry TransferId: U32 update on change
    telemetry TxPackets: U32 update on change
    event ScienceProgress(jobState: ScienceJobState, productId: U32, bytes: U32) severity activity high format "Science {} product={} bytes={}"
    event ScienceFailed(reason: U32) severity warning high format "Science failed reason={}"
    time get port timeCaller
    import Fw.Command
    import Fw.Event
    import Fw.Channel
  }
}
