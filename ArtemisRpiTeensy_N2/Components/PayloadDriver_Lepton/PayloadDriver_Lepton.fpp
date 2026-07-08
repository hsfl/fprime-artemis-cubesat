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

        @ Bring the Lepton camera up and start its stream.
        async command ENABLE opcode 1

        @ Stop the Lepton stream and release the camera.
        async command DISABLE opcode 2

        @ Capture one thermal image and write it as a data product.
        async command CAPTURE_IMAGE opcode 0

        @ Scheduled collection request from PayloadManager.
        async input port requestIn: Components.PayloadCaptureRequest

        @ Data-product write notification from DpWriter.
        async input port dpWrittenIn: Svc.DpWritten

        @ Captured product descriptor/status output
        output port statusOut: Components.ScienceProductDescriptor

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

        @ Lepton camera is ready
        event LeptonReady() severity activity low format "Lepton camera ready"

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

        @ Enables command handling
        import Fw.Command

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
