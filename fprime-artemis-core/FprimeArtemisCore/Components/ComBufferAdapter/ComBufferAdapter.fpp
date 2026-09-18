module Components {

  @ Adapts Svc.GenericHub's buffer-driver interface to the F Prime Com framing
  @ pipeline, so a pair of hubs can talk over a raw byte stream such as a UART.
  @
  @ A UART has no message boundaries, and Drv.ByteStreamBufferAdapter is a pure
  @ pass-through, so hub messages would arrive split or coalesced and the hub
  @ would drop them. Putting FprimeFramer / FrameAccumulator / FprimeDeframer in
  @ the path fixes that, but those components speak Svc.ComDataWithContext while
  @ the hub speaks Fw.BufferSend. This component is the (protocol-free) converter
  @ between the two.
  @
  @ Downlink:
  @   hub.toBufferDriver  -> adapter.bufferIn
  @   adapter.comDataOut  -> framer.dataIn
  @   framer.dataReturnOut -> adapter.comDataReturnIn
  @   adapter.bufferInReturn -> hub.toBufferDriverReturn
  @
  @ Uplink:
  @   deframer.dataOut    -> adapter.comDataIn
  @   adapter.bufferOut   -> hub.fromBufferDriver
  @   hub.fromBufferDriverReturn -> adapter.bufferOutReturn
  @   adapter.comDataReturnOut -> deframer.dataReturnIn
  @
  @ The context carried on the Com ports is unused: hub messages are not F Prime
  @ packets and carry no APID. FprimeDeframer tolerates this (it logs
  @ PayloadTooShort or sets an invalid APID and still passes the payload through).
  passive component ComBufferAdapter {

    # ----------------------------------------------------------------------
    # Hub-facing side: this component is a Drv.PassiveBufferDriver
    # ----------------------------------------------------------------------

    @ Provides bufferIn, bufferInReturn, bufferOut, bufferOutReturn
    import Drv.PassiveBufferDriver

    # ----------------------------------------------------------------------
    # Framing-pipeline-facing side
    # ----------------------------------------------------------------------

    @ Outgoing hub message, passed to the framer
    output port comDataOut: Svc.ComDataWithContext

    @ Ownership of an outgoing hub message coming back from the framer
    sync input port comDataReturnIn: Svc.ComDataWithContext

    @ Incoming deframed hub message from the deframer
    sync input port comDataIn: Svc.ComDataWithContext

    @ Ownership of an incoming hub message returned to the deframer
    output port comDataReturnOut: Svc.ComDataWithContext

  }

}
