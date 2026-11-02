# CDR TCP/Yamcs implementation plan

## October 27 Pi/PDU checkpoint

The [live deployment record](../verification/evidence/n2-pdu-deployment-2026-10-27/README.md)
now establishes ARMv6 Pi Zero W runtime, the paired Pi 5 Yamcs dictionary, real
PDU protocol 2 / firmware 2.2.2, nine named rail readbacks, and 3V3_1
ON → GET ON → OFF → GET OFF. Final readback was all nine OFF. No PDU or Teensy
flash was needed for the readability slice. This closes the exercised Pi/UART/RPC
functional checks; measured payload power/readiness, real payload collection and
raw-file return, RF and flight qualification remain open. The October 20 local
acceptance below retains its original software-only scope. Further hardware
operations require their own task-specific authorization.

**Selected scope, October 20, 2026:** connect Yamcs on the Pi 5 to the F′ deployment on the Pi Zero W over the LAN using stock ComCcsds and TCP. The Zero W runs the stock F′ TCP server; the Pi 5 Yamcs client uses tcp-fast-client. CCSDS carries commands, telemetry, and stock FileDownlink. Do not add a network mux.

CDR retains OBC + real payload + PDU-controlled payload on/off and raw-data acquisition. SatNOGS/SDR/RF is deferred from CDR. The physical Pi Zero W UART and UartChannelMux channel 2 remain connected to the Teensy for local PDU RPC. TCP and UART must work independently.

**Implementation status, October 20:** the local CDR profile and ground-only launcher are implemented and passed final native acceptance. Both opaque fixture files passed stock FileDownlink with independent Yamcs GET byte/hash equality; forced TCP fragmentation, disconnect recovery, fresh SendFile after reconnect, missing-source `FileOpenError`/no object, source/product helper, and fake UART channel-2 RPC passed. Guard tests, startup cancellation, and the legacy Yamcs regression passed. Startup cancellation exited in 0.444 seconds with both exact process groups gone. This is local software evidence only; it does not establish Pi deployment, real payload, or PDU power operation. See the [portable evidence pack](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md).

## Final local acceptance — PASS

- Stock FileDownlink transferred the 729-byte `sample-ayload-config-sohdata.csv` and 606273-byte `sample-payload-raw-science.csv`. Independent Yamcs GET checks matched each source byte-for-byte and by SHA-256; fixture contents remain opaque.
- The ground source/product helper path was exercised. Forced TCP fragmentation and a mid-transfer disconnect were tested; a fresh SendFile after reconnect was independently verified.
- A missing source file raised `FileOpenError` and produced no received Yamcs object. Command status or a sender event alone is not receipt.
- Forty-one fake PDU RPCs on physical UART mux channel 2 completed without errors, including 15 during a large transfer and requests while TCP was disconnected.

The final rerun confirmed that both legacy automatic-science commands are rejected before capture/token allocation, launcher startup cancellation is handled, the native CDR profile rebuilds cleanly, and the existing Yamcs regression remains intact. Independent review passed. See the [CDR TCP runbook](../operations/CDR_TCP.md), the portable [evidence pack](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md), and its [structured acceptance record](../verification/evidence/n2-cdr-tcp-2026-10-20/acceptance.json).

## Path

Pi 5: Yamcs + ground-only launcher + tcp-fast-client ↔ LAN TCP ↔ stock F′ TCP server + ComCcsds: Pi Zero W. A separate physical UART connects the Zero W’s UartChannelMux channel 2 to the satellite Teensy and PDU.

Use the matching Zero W dictionary and CCSDS settings. Keep the existing host-native simulator profile and legacy/RF profile available with their current behavior. The ground-only launcher must not start a second spacecraft deployment.

## Implementation and local acceptance

1. **Implemented:** the CDR profile uses the stock `TcpFastClientAdapter`, `UdpFastAdapter`, TM frame aggregator, and `PacketBridge`, with a small 5 ms UDP pacing wrapper for outgoing TM. Large unpaced bursts can overrun the UDP handoff. FileDownlink remains stock; no source FileDownlink scheduler changes, network mux, or new wire protocol are needed. Periodic PDU status queries remain independent of TCP. Preserve the existing local simulation and legacy/RF profiles.
2. **Loopback PASS:** the ground-only launcher starts Yamcs and the TCP client against native F′. The final native CDR build and loopback passed.
3. **File-transfer PASS:** Yamcs SendFile transferred both opaque fixtures (729 and 606273 bytes); independent Yamcs GET checks matched source bytes and SHA-256. The tests forced TCP fragmentation and a mid-transfer disconnect, then verified a fresh SendFile after reconnect. A missing source generated `FileOpenError` and no received object.
4. **Helper PASS:** the ground helper supports sending a source or selected StorageManager product through Yamcs. Received bytes are validated separately. Do not discover private onboard paths or migrate existing VERIFIED commands/custom channel-1 behavior into stock FileDownlink.
5. **Recovery PASS:** missing source, forced fragmentation, disconnect, and fresh-send recovery passed. Do not count command-OK, FileSent, or local buffer return as ground receipt; missing or partial bytes fail.
6. **Fake-PDU PASS:** 41 channel-2 RPCs completed without errors, including 15 during a large file transfer and requests while TCP was disconnected. This is software isolation evidence only; it does not test real PDU switching.
7. The final profile, source/dictionary identity, command history, file size/hash, disconnect result, and fake-PDU RPC result are recorded in the portable acceptance record.

## FileDownlink boundary

The first end-to-end file step is a manual stock SendFile command. The local ground helper can select a source or catalogued product through Yamcs and issue stock SendFile, with separate byte-integrity validation. Do not add private source-path discovery, silently migrate the existing VERIFIED_COLLECT_AND_DOWNLINK workflow, or claim stock FileDownlink now has the verified mission semantics of the separate custom channel-1 science sender.

The earlier 26-byte stock FileDownlink check is historical compatibility evidence only. The later custom channel-1 science demonstration is not stock FileDownlink acceptance. Stock FileDownlink has no selective retransmission guarantee in this plan; verify complete received bytes independently, reject partial/missing receipts, then retry with a fresh SendFile after reconnect.

## Operation

Use the [CDR TCP runbook](../operations/CDR_TCP.md) for build, ground-only launch, stock downlink and local acceptance commands.

## Hardware boundary

After local acceptance, separately validate the matching ARMv6 Zero W build/runtime, Pi 5 deployment and LAN connection. Real payload command/file behavior and PDU power-on/readiness/collection/power-off require agreed interface details and the applicable hardware runbook. Record those results separately from loopback, fake-PDU, and host-native tests. CDR does not require SatNOGS, SDR, RF, or radio firmware acceptance; those remain FSR work.
