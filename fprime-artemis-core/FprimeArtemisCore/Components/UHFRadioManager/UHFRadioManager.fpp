module Components {
  @ Radio communications adapter. One worker owns all hardware and link state.
  passive component UHFRadioManager {
    sync input port pingIn: Svc.Ping
    output port pingOut: Svc.Ping
    import Svc.Com
    import Svc.BufferAllocation
    output port driverPower: RadioPower
    output port driverSend: RadioSend
    output port driverReceive: RadioReceive
    output port driverStatus: RadioStatusGet
    @ Science owner supplies/acknowledges packets; no full-product radio copy.
    output port scienceNext: RadioScienceNext
    output port scienceSent: RadioScienceSent
    output port scienceRetry: RadioScienceRetry
    @ Enqueue a power request for the radio worker.
    sync input port powerRequestIn: RadioPower
    @ Command acceptance reports a queued request; RadioState reports its outcome.
    sync command RADIO_SET_ENABLED(enabled: bool)
    @ Send ARTEMIS-RADIO-TEST plus LF through the worker. Does not enable the radio.
    @ Completion reports local transmission, not ground reception. One request at a time.
    sync command RADIO_SEND_TEST_PACKET
    sync command RADIO_RESET_COUNTERS
    telemetry RadioState: Components.RadioState
    telemetry LastRssi: I16
    telemetry TxPackets: U32
    telemetry RxPackets: U32
    telemetry Retries: U32
    telemetry AckTimeouts: U32
    telemetry Drops: U32
    telemetry Duplicates: U32
    telemetry ReassemblyTimeouts: U32
    telemetry CrcDrops: U32
    event RadioTestPacketResult(cmdSeq: U32, success: bool, bytes: U32) severity activity high format "Radio test seq={} success={} bytes={} (local TX result)"
    event RadioPowerChanged(enabled: bool, success: bool) severity activity high format "Radio enabled={} success={}"
    time get port timeCaller
    import Fw.Command
    import Fw.Event
    import Fw.Channel
  }
}
