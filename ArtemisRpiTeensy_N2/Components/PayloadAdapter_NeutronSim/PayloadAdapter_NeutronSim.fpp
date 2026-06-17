module Components {
    @ Raspberry Pi-hosted neutron payload simulator adapter.
    passive component PayloadAdapter_NeutronSim {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Capture-duration request input, in seconds
        sync input port requestIn: Svc.Ping

        @ Captured product size/status output
        output port statusOut: Svc.Ping

        @ Last requested capture duration in seconds
        telemetry LastDurationSeconds: U32

        @ Rows captured by the simulator
        telemetry LastRowsCaptured: U32

        @ Total neutron counts in the last capture
        telemetry LastTotalCounts: U32

        @ SAA rows in the last capture
        telemetry LastSaaRows: U32

        @ Last captured product size in bytes
        telemetry LastProductBytes: U32

        @ Last simulator exit status
        telemetry LastExitStatus: U32

        @ Simulator capture completed
        event CaptureComplete(durationSeconds: U32, rows: U32, productBytes: U32) severity activity high format "Neutron sim capture duration={}s rows={} bytes={}"

        @ Simulator capture failed
        event CaptureFailed(durationSeconds: U32, status: U32) severity warning high format "Neutron sim capture failed duration={}s status={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
