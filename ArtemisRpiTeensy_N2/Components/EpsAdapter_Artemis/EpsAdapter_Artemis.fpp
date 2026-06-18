module Components {
    @ Artemis EPS hardware adapter shim.
    passive component EpsAdapter_Artemis {

        @ Health ping input
        sync input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Adapter request input
        sync input port requestIn: Components.EpsCommand

        @ Teensy-local RPC response input from UART channel 2.
        sync input port teensyResponseIn: Fw.BufferSend

        @ Adapter status output
        output port statusOut: Components.EpsStatus

        @ Teensy-local RPC request output to UART channel 2.
        output port teensyRequestOut: Fw.BufferSend

        @ Last handled PDU request
        telemetry LastRequest: Components.EpsRequest

        @ Last PDU protocol status
        telemetry LastPduStatus: U32

        @ Last PDU opcode
        telemetry LastPduOpcode: U32

        @ Last PDU protocol version
        telemetry PduProtocolVersion: U32

        @ Last PDU output bitmap
        telemetry PduOutputBitmap: U32

        @ Last PDU fault bitmap
        telemetry PduFaultBitmap: U32

        @ Last PDU uptime in seconds
        telemetry PduUptimeSeconds: U32

        @ Teensy-local RPC failure count
        telemetry TransportFailureCount: U32

        @ PDU request queued to the Teensy-local channel
        event PduRequestQueued(epsRequest: Components.EpsRequest, requestId: U32) severity activity low format "Artemis PDU request={} queued local request={}"

        @ PDU request handling event
        event PduRequestHandled(epsRequest: Components.EpsRequest, status: U32) severity activity low format "Artemis PDU request={} status={}"

        @ PDU request failure event
        event PduRequestFailed(epsRequest: Components.EpsRequest, status: U32) severity warning low format "Artemis PDU request={} failed status={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
