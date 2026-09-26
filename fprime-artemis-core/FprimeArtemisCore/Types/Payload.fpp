@ Contract between PayloadManager and any payload driver.
@
@ Nothing here names a camera or bus: a new payload driver implements these
@ ports and PayloadManager does not change. PayloadState also crosses the
@ FC<->PC link on FcPcLink.PAYLOAD_STATUS, so both deployments build it.
module Components {

  # ----------------------------------------------------------------------
  # Data types
  # ----------------------------------------------------------------------

  @ Whether the payload can take a capture right now
  enum PayloadState: U8 {
    UNKNOWN = 0 @< No report yet, or reports stopped arriving
    NOT_READY = 1 @< Payload hardware not detected or failed to start
    READY = 2 @< Hardware streaming and idle; a capture can start now
    BUSY = 3 @< A capture is in progress
  }

  @ Normalized subsystem health state
  enum HealthState: U8 {
    UNKNOWN = 0
    OK = 1
    WARN = 2
    FAIL = 3
  }

  @ Source class for a produced science dataset
  enum ScienceProductSource: U8 {
    UNKNOWN = 0
    NEUTRON_SIM = 1
    REAL_PAYLOAD = 2
    TEST = 3
    BOSON = 4
  }

  # ----------------------------------------------------------------------
  # Port types
  # ----------------------------------------------------------------------

  @ Payload capture request in seconds
  port PayloadCaptureRequest(durationSeconds: U32)

  @ Payload readiness report
  port PayloadStateReport(payloadState: PayloadState)

  @ Science product descriptor handoff. The source path is a driver-owned
  @ local file path.
  port ScienceProductDescriptor(
    productId: U32,
    productBytes: U32,
    sourceKind: ScienceProductSource,
    sourcePath: string size 192,
    sourceCrc: U32
  )

  @ Normalized subsystem health report with subsystem-specific detail
  port HealthStatus(healthState: HealthState, detail: U32)

}
