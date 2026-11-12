module Components {
  enum RadioState : U8 { OFF, IDLE, RX, TX, FAULT }
  array RadioBytes = [49] U8
  struct RadioPacket { bytes: RadioBytes, length: U8, to: U8, from: U8, network: U8, version: U8 }
  struct RadioStatus { $state: RadioState, rssi: I16, crcDrops: U32 }
  port RadioPower(enabled: bool) -> bool
  port RadioSend($packet: RadioPacket) -> bool
  port RadioReceive(ref $packet: RadioPacket) -> bool
  port RadioStatusGet() -> RadioStatus
  @ C3M application packet before channel-1 RF segmentation.
  array ScienceBytes = [44] U8
  @ Ask the serialized science owner for one pending packet.
  port RadioScienceNext(ref bytes: ScienceBytes, ref length: U8) -> bool
  @ Local TX outcome; successful transmission is not ground receipt.
  port RadioScienceSent(success: bool)
  @ Reassembled channel-1 ground repair request.
  port RadioScienceRetry(bytes: ScienceBytes, length: U8)
}
