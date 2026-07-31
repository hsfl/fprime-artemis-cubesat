module Components {

    @ Routes one mission capture request to the selected camera driver.
    active component PayloadDriverSelector {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Capture request from PayloadManager
        async input port requestIn: Components.PayloadCaptureRequest

        @ Status inputs from Lepton (0) and Boson (1)
        async input port driverStatusIn: [2] Components.ScienceProductDescriptor

        @ Capture requests to Lepton (0) and Boson (1)
        output port driverRequestOut: [2] Components.PayloadCaptureRequest

        @ Release the previously selected camera before switching drivers
        output port deactivateDriverOut: [2] Fw.Signal

        @ Selected-driver status output to PayloadManager
        output port statusOut: Components.ScienceProductDescriptor

        @ Select the camera used by future capture requests
        async command SELECT_PAYLOAD_DRIVER(
            driver: Components.PayloadDriverKind
        ) opcode 0

        @ Selected camera driver
        telemetry SelectedDriver: Components.PayloadDriverKind update on change

        @ One means a selected-driver capture is still producing products
        telemetry RequestInFlight: U32 update on change

        @ Driver selection changed
        event PayloadDriverSelected(driver: Components.PayloadDriverKind) \
            severity activity high \
            format "Payload driver selected: {}"

        @ Capture request routed to the selected driver
        event PayloadRequestRouted(driver: Components.PayloadDriverKind) \
            severity activity low \
            format "Payload request routed to driver {}"

        @ Driver selection or request was rejected
        event PayloadSelectionRejected(reason: U32) \
            severity warning low \
            format "Payload driver operation rejected reason={}"

        @ Status from a non-selected or idle driver was ignored
        event PayloadStatusIgnored(driverIndex: U32) \
            severity warning low \
            format "Payload driver status ignored index={}" \
            throttle 5

        @ Port for requesting the current time.
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channel handling
        import Fw.Channel
    }
}
