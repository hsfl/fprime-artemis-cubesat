module Components {
  @ Packet-radio substitute; production RadioComms owns framing and retry behavior.
  passive component UHFRadioDriver_Sim {
    sync input port power: RadioPower
    sync input port $send: RadioSend
    sync input port receive: RadioReceive
    sync input port status: RadioStatusGet
  }
}
