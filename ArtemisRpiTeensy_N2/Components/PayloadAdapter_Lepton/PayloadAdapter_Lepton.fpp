module Components {

    @ Record structure for thermal image data storing the timestamp and array of values (per pixel)
        struct ThermalImageRecordType {
            timeTag: Fw.TimeValue
            value: [160*120] U16  # 160x120 pixels = 19,200 for single frame (native 16-bit thermal)
        }
    
    @ Data Product Producer for Lepton Camera
    active component PayloadAdapter_Lepton {

        @ Bring the Lepton camera up and start its continuous thermal stream.
        @ Must be sent before CAPTURE_IMAGE.
        async command ENABLE opcode 1

        @ Stop the Lepton stream and release the camera.
        async command DISABLE opcode 2

        @ Capture a thermal image from the Lepton camera and store it as a data product on the RPi
        async command CAPTURE_IMAGE opcode 0

        @ Data product record holding one thermal image (timestamp + per-pixel values)
        product record ThermalImageRecord: ThermalImageRecordType id 0

        @ Data product container holding a captured thermal image from the Lepton camera
        product container ThermalImageContainer id 0 default priority 10

        @ Event indicating that the Lepton is not busy and ready to capture a new image
        event LeptonReady() \
            severity activity low id 3 \
            format "Lepton camera is ready for image capture"

        event ImageCaptureStart() \
            severity activity low id 4 \
            format "Starting capture of Lepton image"

        @ A thermal image was captured and stored as a data product
        event ImageCaptureSuccess(byteSize: FwSizeType) \
            severity activity high id 1 \
            format "Lepton image captured and stored as data product ({} bytes)"
        
        event ImageCaptureFailed(reason: string size 80) \
            severity warning high id 2 \
            format "Lepton image capture failed: {}"


        @ Event indicating failure to allocate memory for data product
        event DpMemoryFailure(allocationSize: FwSizeType) \
            severity warning high id 0 \
            format "Memory allocation of size {} for data product container failed" \
            throttle 2
        

        ###############################################################################
        # Standard AC Ports: Required for Channels, Events, Commands, and Parameters  #
        ###############################################################################
        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel

        @ Port to return the value of a parameter
        param get port prmGetOut

        @Port to set the value of a parameter
        param set port prmSetOut

        @ Data product get port: allocates a data product container
        product get port productGetOut

        @ Data product send port: sends the filled data product container
        product send port productSendOut

    }
}
