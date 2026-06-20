module Components {
    @ Channel wrapper between ComCcsds/payload traffic and the Linux UART driver.
    passive component UartChannelMux {

        @ CCSDS bytes from ComStub, wrapped as channel 0.
        guarded input port ccsdsSendIn: Drv.ByteStreamSend

        @ Payload packets from PayloadDownlinkManager, wrapped as channel 1.
        guarded input port payloadSendIn: Fw.BufferSend

        @ Local Teensy subsystem RPC packets, wrapped as channel 2.
        guarded input port localSendIn: Fw.BufferSend

        @ Wrapped bytes received from the UART driver.
        sync input port drvReceiveIn: Drv.ByteStreamData

        @ Return path from ComStub for buffers emitted by ccsdsRecvOut.
        sync input port ccsdsRecvReturnIn: Fw.BufferSend

        @ Wrapped bytes sent to the UART driver.
        output port drvSendOut: Drv.ByteStreamSend

        @ Unwrapped channel 0 bytes sent to ComStub.
        output port ccsdsRecvOut: Drv.ByteStreamData

        @ Unwrapped channel 1 packets sent to PayloadDownlinkManager.
        output port payloadRecvOut: Fw.BufferSend

        @ Unwrapped channel 2 packets sent to the local Teensy subsystem adapter.
        output port localRecvOut: Fw.BufferSend

        @ Original UART receive buffer returned to the UART driver.
        output port drvReceiveReturnOut: Fw.BufferSend

        @ UART channel frames transmitted
        telemetry FramesTx: U32

        @ UART channel frames received
        telemetry FramesRx: U32

        @ Dropped malformed channel frames
        telemetry FrameDrops: U32

        @ Channel frame dropped
        event FrameDropped(reason: U32) severity warning low format "UART channel frame dropped reason={}"

        @ Port for requesting the current time
        time get port timeCaller

        @ Enables event handling
        import Fw.Event

        @ Enables telemetry channel handling
        import Fw.Channel
    }
}
