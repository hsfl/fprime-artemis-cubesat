module Components {

    @ Thermal image data record for one 160x120 Lepton frame.
    struct ThermalImageRecordType {
        timeTag: Fw.TimeValue
        value: [160*120] U16
    }

    @ Raspberry Pi-hosted Lepton thermal camera payload driver.
    active component PayloadDriver_Lepton {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Scheduled collection request from PayloadManager.
        async input port requestIn: Components.PayloadCaptureRequest

        @ Readiness probe. Opens the camera if it is not streaming, then
        @ reports the payload state. Dropped rather than queued while a capture
        @ holds the driver task, so a slow capture cannot overflow the queue.
        async input port run: Svc.Sched drop

        @ Release this camera when another payload driver is selected.
        sync input port deactivateIn: Fw.Signal

        @ Request a fresh 80x60 U8 preview derived from the newest validated Lepton frame.
        @ Queued on this driver's task so camera lifecycle access is serialized with science capture.
        async input port previewRequestIn: Fw.Signal

        @ Data-product write notification from DpWriter.
        async input port dpWrittenIn: Svc.DpWritten

        @ Captured product descriptor/status output
        output port statusOut: Components.ScienceProductDescriptor

        @ Payload readiness, reported on every probe and on every change
        output port stateOut: Components.PayloadStateReport

        @ One fixed 80x60 U8 preview buffer. It stays valid until the stream app requests another preview.
        output port previewOut: Fw.BufferSend

        @ Data product record holding one thermal image.
        product record ThermalImageRecord: ThermalImageRecordType id 0

        @ Data product container holding one thermal image.
        product container ThermalImageContainer id 0 default priority 10

        @ Last requested capture duration in seconds
        telemetry LastDurationSeconds: U32

        @ Last science product identifier
        telemetry LastProductId: U32

        @ Last generated data product bytes, excluding the container packet wrapper
        telemetry LastDataBytes: U32

        @ Last written .fdp file size
        telemetry LastFileBytes: U32

        @ Last capture status, where 0 means success
        telemetry LastCaptureStatus: U32

        @ Pending data-product write count
        telemetry PendingWrites: U32

        @ Whether the camera can take a capture now
        telemetry PayloadState: Components.PayloadState update on change

        @ Lepton camera is ready
        event LeptonReady() severity activity high format "Lepton camera ready"

        @ Lepton camera could not be opened. Emitted when readiness is lost,
        @ not on every failed probe.
        event LeptonNotReady(reason: string size 96) \
            severity warning high \
            format "Lepton camera not ready: {}"

        @ Selected Lepton frame source backend
        event LeptonBackendSelected(backend: string size 16) \
            severity activity low \
            format "Lepton camera backend selected: {}"

        @ Image capture started
        event ImageCaptureStart() severity activity low format "Starting Lepton image capture"

        @ Image capture was queued to the data-product writer
        event ImageCaptureQueued(dataBytes: FwSizeType) \
            severity activity high \
            format "Lepton image queued as data product ({} data bytes)"

        @ Image capture was written to disk
        event ImageCaptureSuccess(fileBytes: FwSizeType) \
            severity activity high \
            format "Lepton image stored as data product ({} file bytes)"

        @ Image capture failed
        event ImageCaptureFailed(reason: string size 96) \
            severity warning high \
            format "Lepton image capture failed: {}"

        @ Data-product memory allocation failed
        event DpMemoryFailure(allocationSize: FwSizeType) \
            severity warning high \
            format "Memory allocation of size {} for Lepton data product failed" \
            throttle 2

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channel handling
        import Fw.Channel

        @ Data product get port
        product get port productGetOut

        @ Data product send port
        product send port productSendOut
    }
}
