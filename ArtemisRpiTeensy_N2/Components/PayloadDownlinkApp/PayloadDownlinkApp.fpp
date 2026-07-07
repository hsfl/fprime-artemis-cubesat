module Components {
    @ Generic blob downlink manager for channel 1 payload packets.
    active component PayloadDownlinkApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Retry/control packets from the ground payload receiver.
        sync input port packetIn: Fw.BufferSend

        @ Mission downlink request carrying the staged science-product descriptor.
        async input port downlinkRequestIn: Components.ScienceDownlinkRequest

        @ Payload protocol packets to the UART channel mux.
        output port packetOut: Fw.BufferSend

        @ Payload transfer status to mission communications manager.
        output port statusOut: Components.PayloadDownlinkStatus

        @ Start downlinking a generic deterministic blob.
        async command START_PAYLOAD_DOWNLINK(productId: U32, byteCount: U32)

        @ Abort the active payload downlink.
        async command ABORT_PAYLOAD_DOWNLINK

        @ Emit current status telemetry and event.
        async command GET_PAYLOAD_STATUS

        @ Payload transfer state: 0=idle, 1=downlinking, 2=done, 3=aborted, 4=error.
        telemetry PayloadState: U32 update on change

        @ Transfer identifier for the latest payload downlink.
        telemetry TransferId: U32 update on change

        @ Generic product identifier selected by command.
        telemetry ProductId: U32 update on change

        @ Blob size in bytes.
        telemetry TotalBytes: U32 update on change

        @ Total payload data packets expected.
        telemetry TotalPackets: U32 update on change

        @ Number of data packets sent, including retry packets.
        telemetry PacketsSent: U32

        @ Latest nominal payload downlink progress percent.
        telemetry ProgressPercent: U32

        @ Nominal payload data packets sent for progress display.
        telemetry ProgressPacketsSent: U32

        @ Nominal payload data packets expected for progress display.
        telemetry ProgressTotalPackets: U32

        @ Retry rounds serviced.
        telemetry RetryRound: U32

        @ Number of missing packets in the latest retry request.
        telemetry PacketsMissing: U32

        @ Last error code.
        telemetry LastError: U32 update on change

        @ Payload downlink started.
        event PayloadDownlinkStarted(productId: U32, byteCount: U32, totalPackets: U32) \
            severity activity high format "Payload downlink started product={} bytes={} packets={}"

        @ Payload downlink completed.
        event PayloadDownlinkComplete(transferId: U32, packetsSent: U32) \
            severity activity high format "Payload downlink complete transfer={} packetsSent={}"

        @ Payload downlink progress, throttled to nominal 10 percent increments with short 100 percent replay after completion.
        event PayloadDownlinkProgress(transferId: U32, percentComplete: U32, packetsSent: U32, totalPackets: U32) \
            severity activity high format "Payload downlink progress transfer={} percent={} packets={}/{}"

        @ Payload downlink failed; keep throttled because retry/status paths can storm.
        event PayloadDownlinkFailed(reason: U32, detail: U32) \
            severity warning low format "Payload downlink failed reason={} detail={}" throttle 5

        @ Payload retry request received; keep throttled because retry paths can storm.
        event PayloadRetryRequested(startIndex: U32, missingCount: U32) \
            severity activity low format "Payload retry requested start={} missing={}" throttle 10

        @ Payload status; keep throttled because progress/status paths can storm.
        event PayloadStatus(stateValue: U32, sent: U32, total: U32, lastError: U32) \
            severity activity low format "Payload status state={} sent={} total={} error={}" throttle 10

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables command handling
        import Fw.Command

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channels handling
        import Fw.Channel
    }
}
