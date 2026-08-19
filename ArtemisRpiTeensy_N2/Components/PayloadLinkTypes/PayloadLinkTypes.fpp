module Components {

  @ Local acceptance result for a payload packet copied toward the Pi UART.
  @ This does not claim RF transmission or ground receipt. The caller retains
  @ ownership of the supplied buffer after the synchronous call returns.
  enum PayloadSendStatus : U8 {
    LOCAL_ACCEPTED = 0
    LOCAL_RETRY = 1
    LOCAL_ERROR = 2
  }

  @ Synchronous payload packet handoff with explicit local-UART acceptance.
  port PayloadPacketSend(
    ref sendBuffer: Fw.Buffer
  ) -> PayloadSendStatus
}
