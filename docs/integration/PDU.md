# N2 PDU payload-power integration

**CDR scope, transport selected October 20, 2026:** OBC + real payload + PDU, including payload power on/off. Use stock ComCcsds over TCP between Pi Zero W F′ and Pi 5 Yamcs, with stock FileDownlink for the retained file. No network mux or SatNOGS/SDR/RF is required. The physical UART mux channel 2 remains for PDU RPC. Final local native loopback passed both opaque fixture transfers with independent GET byte/hash checks and 41 fake channel-2 PDU RPCs before/during transfer and while disconnected; the guard tests and legacy Yamcs regressions also passed. This does not exercise real PDU power. FSR adds SatNOGS/SDR/RF and the complete ground chain. See the [canonical plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md), [runbook](../operations/CDR_TCP.md), and [portable evidence pack](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md).

## Target CDR sequence

1. Command/observe Pi Zero W F′ from Pi 5/Yamcs over stock ComCcsds/TCP. Keep the physical UART mux channel 2 for PDU RPC.
2. OBC commands the agreed PDU payload output on. Confirm the rail/readback and payload-ready condition after the agreed startup delay.
3. Command collection through the USB↔RS-422 payload interface. Observe acceptance and terminal status separately.
4. Retain the complete raw product on the OBC and use stock FileDownlink SendFile. Independently verify received bytes, size and hash; command completion or FileSent alone is not ground receipt. Start with the supplied multi-packet test files before wiring a retained-product ground helper.
5. Command the PDU output off and confirm the off state. Keep the onboard product after verified ground receipt.

This is a hardware acceptance target, not a result established by the local simulator. Formal modes and time-based scheduling are not CDR prerequisites. The PDU power sequence and the operator/data-inspection procedure still need implementation/runbook agreement.

## CDR firmware and command behavior — October 22, 2026

The pinned PDU firmware is 2.2.2. Pi↔Teensy remains at 115200 baud and
Teensy↔PDU remains at 9600 baud. The firmware uses nonblocking byte reads and
a 250 ms transmit deadline; timed output actions continue to be serviced while
the transmitter waits. These changes do not change GPIO defaults or add new
power-control commands.

`epsManager` serializes requests and waits for the matching driver result before
returning command completion. Busy, timeout, malformed replies and PDU rejection
produce failure. GET/SET rail replies update the enable-state bitmap; a SET reply
must match the requested output and state. A power-cycle ACK confirms the initial
off state, not completion of the later restoration. Request fresh status after
the off interval to observe restoration.

`epsManager.REQUEST_EPS_DRIVER_INFO` reports firmware major/minor/patch,
`DriverGitHash` and `DriverBuildFlags`. Flag bit 0 means the hash is known;
bit 1 means the firmware source tree was dirty when built. The hash is a
12-character Git commit prefix, not a hash of uncommitted edits. Legacy
seven-byte protocol-info replies remain supported and report an unknown hash.
The extended firmware reply preserves those seven bytes and appends build
metadata. Rebuild the N2 binary and its matching Yamcs dictionary together.

Readback is the PDU's enable-pin state, not measured rail voltage or proof that
the payload is ready. The October 21 record established Pi/Teensy UART byte flow
but still reported PDU UNKNOWN. A later real PDU response and one-rail switching
were observed on October 27, as recorded below; measured payload on/off remains
required for the CDR sequence.

Local verification: PDU host smoke and address/undefined-behavior sanitizer
checks passed; EPS driver 4/4 and manager 2/2 unit tests passed. The native CDR
deployment built, listened on loopback TCP with a pseudo-terminal UART, remained
running, and exited with status 0 after SIGINT. No target MPLAB build, flashing,
physical UART or power switching was performed for this change.

## Readable PDU operations — October 27, 2026

The October 26 local readability slice adds named PDU commands, validated
readback/error events and nine enum state channels (`UNKNOWN`, `OFF`, `ON`,
`STALE`). It preserves the PDU v2 wire contract and numeric EPS commands. Only
the F′ binary and matching Yamcs dictionary change; no PDU/Teensy flash is
required. See the [operator and local QA guide](../operations/PDU_READABILITY.md).
The [live deployment record](../verification/evidence/n2-pdu-deployment-2026-10-27/README.md)
shows protocol 2 / firmware 2.2.2 and all nine named readbacks through the Pi 5
GUI, with 3V3_1 SET ON → GET ON → SET OFF → GET OFF and a final all-OFF
readback. This validates the exercised UART/RPC path and enable readback only;
it does not measure rail voltage or establish payload readiness.

## Inputs to agree

- Selected hardware is Artemis PDU v2.2; verify exact populated board, running firmware and payload output/rail mapping; harness, connector/pinout, voltage and current requirements.
- Selected path: Pi Zero W F′ → UART mux channel 2/local RPC → satellite Teensy → UART → PDU v2 protocol. The October 27 test exercised this path for version, status, and one switched rail. Firmware reference: [`codex/n2-cdr-pdu`](https://github.com/hsfl/artemis-cubesat-pdu-firmware/tree/codex/n2-cdr-pdu), pinned by `external/artemis-pdu`. Hardware v2.2, firmware version, and protocol v2 are distinct.
- Payload startup/readiness and shutdown/standby timing; command order and what readback or measurement proves powered-on and powered-off states.
- All payload supply paths, including the USB↔RS-422 adapter, so the intended PDU-controlled off state can be verified.
- Timeout, missing readback, payload-not-ready, failed collection and failed retrieval behavior. Agree whether/when power is removed on each failure and how retained data are protected.
- The CDR TCP profile and ground-only launcher have final local native loopback acceptance, including independent file byte/hash checks, fake channel-2 UART RPC, startup-cancellation and legacy regressions. The fake UART/PDU test does not switch real power; the hardware on/off sequence still needs a populated-board, rail/readback and timing record. See the [TCP plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md), [runbook](../operations/CDR_TCP.md), and [portable evidence pack](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md).

## Source and evidence boundary

- [PDU v2 ICD](../../external/artemis-pdu/PDU_PROTOCOL_ICD.md) owns wire semantics for the pinned reference.
- [EpsManager](../../ArtemisRpiTeensy_N2/Components/EpsManager/EpsManager.fpp), [EpsDriver_Artemis](../../ArtemisRpiTeensy_N2/Components/EpsDriver_Artemis/EpsDriver_Artemis.fpp) and [topology](../../ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.fpp) own current software wiring.
- [Payload contract](PAYLOAD.md) owns the USB↔RS-422 command/lifecycle and raw-product boundary.

Record exact board/firmware, F′ binary/dictionary, command outcomes, payload rail/readiness observations, retained-product integrity and off-state evidence for the CDR sequence. The October 27 PDU bench check is bounded hardware evidence, not acceptance of the full payload sequence. Hardware operations follow a revision-matched runbook and separate task-specific authorization.
