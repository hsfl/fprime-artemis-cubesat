module Components {
    @ Response-paced best-effort Lepton preview uploader.
    active component PayloadStreamApp {

        @ Health ping input
        async input port pingIn: Svc.Ping

        @ Health ping output
        output port pingOut: Svc.Ping

        @ One-hertz watchdog for an outstanding target-4 local-RPC response.
        async input port run: Svc.Sched drop

        @ Fixed 80x60 preview supplied by PayloadDriver_Lepton. The driver keeps the buffer stable
        @ until this component asks for another preview, so this queued handoff is lifetime-safe.
        async input port previewIn: Fw.BufferSend

        @ Local-RPC result supplied synchronously by UartChannelMux.
        guarded input port previewResponseIn: Fw.BufferSend

        @ Queued self-handoff that processes a copied local-RPC response.
        async input port responseAdvanceIn: Fw.Signal

        @ Ask PayloadDriver_Lepton for its newest validated preview.
        output port previewRequestOut: Fw.Signal

        @ Send a target-4 local preview RPC through UartChannelMux.
        output port previewPacketOut: Components.PayloadPacketSend

        @ Queue response processing onto this component's active task.
        output port responseAdvanceOut: Fw.Signal

        @ Start response-paced preview streaming.
        async command START_STREAM

        @ Stop after the current local operation; send an abort best effort.
        async command STOP_STREAM

        @ Emit current preview-stream state.
        async command GET_STREAM_STATUS

        @ 0=stopped, 1=waiting for source, 2=uploading.
        telemetry StreamState: U32 update on change

        @ Monotonic source-frame sequence number.
        telemetry FrameSequence: U32

        @ Current preview session identity.
        telemetry SessionId: U32

        @ Successfully committed previews.
        telemetry FramesUploaded: U32

        @ Previews abandoned because a local RPC was rejected or malformed.
        telemetry FramesDropped: U32

        @ Latest local-RPC status value.
        telemetry LastResponseStatus: U32

        @ Preview stream started.
        event PreviewStreamStarted() severity activity high format "Lepton preview stream started"

        @ Preview stream stopped.
        event PreviewStreamStopped() severity activity high format "Lepton preview stream stopped"

        @ One preview was committed to the local preview target.
        event PreviewUploaded(frameSequence: U32, sessionId: U32) severity activity low \
            format "Lepton preview uploaded frame={} session={}"

        @ A best-effort preview was abandoned without retry or repair.
        event PreviewDropped(reason: U32, detail: U32) severity warning low \
            format "Lepton preview dropped reason={} detail={}" throttle 10

        @ Stream status emitted only by GET_STREAM_STATUS.
        event PreviewStatus(stateValue: U32, frameSequence: U32, uploaded: U32, dropped: U32) severity activity low \
            format "Lepton preview status state={} frame={} uploaded={} dropped={}"

        @ Port for requesting the current time
        time get port timeCaller

        import Fw.Command
        import Fw.Event
        import Fw.Channel
    }
}
