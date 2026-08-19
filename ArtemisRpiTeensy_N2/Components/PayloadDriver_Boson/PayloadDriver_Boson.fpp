module Components {

    @ One full Boson 320 frame in native raw U16 counts.
    struct BosonImageRecordType {
        timeTag: Fw.TimeValue
        value: [320*256] U16
    }

    @ Raspberry Pi-hosted Boson camera payload driver.
    active component PayloadDriver_Boson {

        @ Health ping input.
        async input port pingIn: Svc.Ping

        @ Health ping output.
        output port pingOut: Svc.Ping

        @ Scheduled collection request from the payload-driver selector.
        async input port requestIn: Components.PayloadCaptureRequest

        @ Release this camera when another payload driver is selected.
        sync input port deactivateIn: Fw.Signal

        @ Data-product write notification from DpWriter.
        async input port dpWrittenIn: Svc.DpWritten

        @ Completed Boson data-product descriptor.
        output port statusOut: Components.ScienceProductDescriptor

        @ Data product record holding one full Boson image.
        product record BosonImageRecord: BosonImageRecordType id 0

        @ Data product container holding one full Boson image.
        product container BosonImageContainer id 0 default priority 10

        @ Last requested capture duration in seconds.
        telemetry LastDurationSeconds: U32

        @ Last science product identifier.
        telemetry LastProductId: U32

        @ Last generated data-product bytes, excluding the container wrapper.
        telemetry LastDataBytes: U32

        @ Last written Boson FDP file size.
        telemetry LastFileBytes: U32

        @ Last capture status, where zero means success.
        telemetry LastCaptureStatus: U32

        @ One means a data-product write is pending.
        telemetry CaptureActive: U32 update on change

        @ Boson camera backend is ready.
        event BosonReady() severity activity low format "Boson camera ready"

        @ Selected Boson frame source backend.
        event BosonBackendSelected(backend: string size 16) \
            severity activity low \
            format "Boson camera backend selected: {}"

        @ Full-frame capture started.
        event ImageCaptureStart(captureId: U32) \
            severity activity low \
            format "Starting Boson image capture {}"

        @ Full image was queued to the data-product writer.
        event ImageCaptureQueued(captureId: U32, dataBytes: FwSizeType) \
            severity activity high \
            format "Boson capture {} queued as data product ({} data bytes)"

        @ Full image was saved as one standard FDP.
        event ImageCaptureSuccess(captureId: U32, fileBytes: FwSizeType) \
            severity activity high \
            format "Boson capture {} stored as data product ({} file bytes)"

        @ Image capture failed.
        event ImageCaptureFailed(reason: string size 96) \
            severity warning high \
            format "Boson image capture failed: {}"

        @ Data-product buffer allocation failed.
        event DpMemoryFailure(allocationSize: FwSizeType) \
            severity warning high \
            format "Memory allocation of size {} for Boson data product failed" \
            throttle 2

        @ Port for requesting the current time.
        time get port timeCaller

        @ Enables event handling.
        import Fw.Event

        @ Enables telemetry channel handling.
        import Fw.Channel

        @ Data product get port.
        product get port productGetOut

        @ Data product send port.
        product send port productSendOut
    }
}
