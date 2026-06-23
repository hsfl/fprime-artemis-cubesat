module Components {
    @ Generic blob downlink manager for channel 1 payload packets.
    active component PayloadDownlinkManager {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Retry/control packets from the ground payload receiver.
        sync input port packetIn: Fw.BufferSend

        @ Mission downlink request carrying the staged generic blob byte count.
        async input port downlinkRequestIn: Svc.Ping

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
        telemetry PayloadState: U32

        @ Transfer identifier for the latest payload downlink.
        telemetry TransferId: U32

        @ Generic product identifier selected by command.
        telemetry ProductId: U32

        @ Blob size in bytes.
        telemetry TotalBytes: U32

        @ Total payload data packets expected.
        telemetry TotalPackets: U32

        @ Number of data packets sent, including retry packets.
        telemetry PacketsSent: U32

        @ Retry rounds serviced.
        telemetry RetryRound: U32

        @ Number of missing packets in the latest retry request.
        telemetry PacketsMissing: U32

        @ Last error code.
        telemetry LastError: U32

        @ Payload downlink started.
        event PayloadDownlinkStarted(productId: U32, byteCount: U32, totalPackets: U32) \
            severity activity high format "Payload downlink started product={} bytes={} packets={}"

        @ Payload downlink completed.
        event PayloadDownlinkComplete(transferId: U32, packetsSent: U32) \
            severity activity high format "Payload downlink complete transfer={} packetsSent={}"

        @ Payload downlink failed.
        event PayloadDownlinkFailed(reason: U32, detail: U32) \
            severity warning low format "Payload downlink failed reason={} detail={}"

        @ Payload retry request received.
        event PayloadRetryRequested(startIndex: U32, missingCount: U32) \
            severity activity low format "Payload retry requested start={} missing={}"

        @ Payload status.
        event PayloadStatus(stateValue: U32, sent: U32, total: U32, lastError: U32) \
            severity activity low format "Payload status state={} sent={} total={} error={}"

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
