# Neutron 2 development intent — 2026-10-07 (updated 2026-10-27)

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

**BLUF:** October CDR targets **OBC + real payload + PDU**, including PDU-controlled payload power on/off and raw-data acquisition. CDR skips SatNOGS/SDR/RF. Pi Zero W F′ connects directly to Pi 5 Yamcs over LAN using stock ComCcsds/TCP and stock FileDownlink; do not add a network mux. Keep the physical UART mux channel 2 for Teensy/PDU RPC. The local profile and ground-only launcher pass native F′/Yamcs loopback and legacy regression checks. The local tests remain software evidence; the October 27 checkpoint above separately validates Pi runtime and the exercised PDU commands. The complete real payload sequence remains open. December FSR adds SatNOGS/SDR/RF and the previously planned mission modes/scheduling. The accepted A → C → B work remains local software evidence on its existing custom channel-1 path. See the [canonical plan](CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md), [runbook](../operations/CDR_TCP.md), and [portable evidence pack](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md).

## October 20 CDR scope and transport selection

CDR retains payload/PDU power on/off and raw acquisition, with Yamcs on the Pi 5 and F′ on the Pi Zero W. Use the stock F′ TCP server on Zero W and Yamcs tcp-fast-client on Pi 5. Stock ComCcsds carries command, telemetry and stock FileDownlink directly over TCP. The final local profile and legacy regression passed. No network mux is used. The separate physical Pi↔Teensy UART mux channel 2 remains the PDU RPC route. SatNOGS/SDR/RF is FSR scope. See the [canonical TCP/Yamcs implementation plan](CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md), [runbook](../operations/CDR_TCP.md), and [portable evidence pack](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md). This does not prove target runtime or hardware acceptance.

This October 20 selection supersedes the October 18–19 CDR transport proposal below. No RF-sized chunk/retry behavior or custom network channel is a CDR requirement. The earlier custom channel-1 `VERIFIED_COLLECT_AND_DOWNLINK` evidence remains valid for its local software profile but is a separate path and is not silently migrated to stock FileDownlink.

## October 18 hardware/ownership clarification

Dennis confirmed payload↔OBC command/telemetry over USB↔RS-422; exact adapter/settings, public protocol and raw-file transfer remain open. The satellite Pi Zero W runs the F′ deployment binary with the Teensy embedded split retained. The separate Pi 5 Ubuntu host runs Yamcs/F′ ground services for LAN browser operators. Sam and Zach handle SatNOGS firmware/SPI and SDR; Dennis owns F′/Yamcs integration. Pi Zero W is the selected SPI controller; harness pinout, firmware packet contract and ground software handoff remain to be agreed. This confirms the intended allocation, not hardware acceptance.

## October 19 transport proposal — superseded October 20

- **CDR proposal (superseded):** Pi Zero W ↔ Pi 5 TCP with a custom network mux for channel-0 CCSDS and channel-1 chunked science. The current selection is direct stock ComCcsds/TCP plus stock FileDownlink, with no network mux; see the October 20 section above.
- **PDU, both milestones:** Pi Zero W F′ → existing UART mux channel 2/local RPC → satellite Teensy → UART → Artemis PDU hardware **v2.2**, using the **v2 protocol** reference on [`codex/pdu-protocol-refactor`](https://github.com/hsfl/artemis-cubesat-pdu-firmware/tree/codex/pdu-protocol-refactor). Dennis reports it mostly wired; no new hardware test is claimed. Exact running firmware, rail mapping/readback and readiness timing remain to be verified. The pinned submodule is unchanged.
- **FSR radio:** custom SatNOGS-specific F′ radio-driver component on Pi Zero W → SPI breakout → 6-pin Molex-to-PC104 harness → separately powered SatNOGS-COMMS v0.3.2 PHASMA PC104 board → SMA/coax UHF RF → USRP B200 → USB → Pi 5/Yamcs → LAN operator browser. This is real conducted RF, not a digital cable bypass. SPI is the host bus; UHF is the band, not UART; S-band is not assumed.
- Sam/Zach own SDR/demodulation and the matching RF arrangement; this checkpoint does not specify their RF setup. Harness pinout, SPI/firmware packet contract and SDR-to-Yamcs software handoff remain open. The FSR mission chain uses RF; the role of a retained maintenance TCP link is not specified here.

## Current decisions and boundaries

- **Development stage:** Local software acceptance and the separately authorized October 27 Pi/PDU bench check are recorded above. The complete real payload sequence remains open. Further deployment, flashing, RF or power switching requires task-specific authorization.
- **CDR FlatSat:** The planned OBC is a Raspberry Pi Zero W running F′ as the FlatSat proxy, with the Neutron 2 payload board. The revised October sequence requires the PDU to power the payload on/off; COMMS and SDR are deferred to FSR. The current software-only stage does not establish those hardware paths.
- **Ground host:** Pi 5 Ubuntu/Yamcs serves browser operators on the LAN. For CDR it runs Yamcs and tcp-fast-client; FSR adds SDR services. The ground-only launcher passes local loopback and startup-cancellation checks; cancellation exited in 0.444 seconds with both exact process groups gone. Pi 5 deployment and dictionary reload were exercised in the October 27 record; representative load and the full payload sequence remain separate gates. See the [ground-host plan](../integration/GROUND_HOST.md) and [Yamcs primer](../operations/YAMCS_BASICS.md).
- **CDR scope:** Real payload command/acquisition and PDU-controlled payload on/off are required. Formal mission modes, scheduling, SatNOGS/SDR/RF, SOH and ADCS/geo-based commanding are not required. Use direct stock ComCcsds/TCP and stock FileDownlink. Local software tests use fake payload/PDU interfaces; they do not establish real hardware acceptance.
- **Payload data:** The payload supplies raw science data. Preserve the existing simulator CSV/histogram and custom channel-1 results as local software evidence, separate from CDR. Final local CDR checks passed stock FileDownlink SendFile for both opaque multi-packet fixtures with independent Yamcs GET byte/hash equality; the source/product helper was exercised. Do not claim the custom VERIFIED workflow has migrated. The real payload-side file boundary remains to be agreed.
- **Ownership:** Dennis owns OBC F′ and Yamcs integration, including the F′ SatNOGS component/driver. Aris owns the payload. Sam and Zach own SatNOGS firmware/SPI development and SDR integration. USB↔RS-422 payload command/telemetry is selected; electrical settings, public command/lifecycle and raw-file contract remain open.
- **Historical evidence:** The October 7 local Yamcs ping and manual 26-byte stock FileDownlink retrieval remain recorded in the [local Yamcs runbook](../development/YAMCS_LOCAL_TESTING.md). They are compatibility evidence only; they do not prove the payload command path, raw-data path, SatNOGS/SDR path, or either milestone end to end.
- **Evidence boundary:** Keep source inspection, local Yamcs simulations, target builds, payload/OBC integration, RF/FlatSat demonstration, and flight qualification distinct. Do not promote a simulation or old demo record to hardware acceptance.

## October 2026 CDR target

Scope and transport selected October 20:

1. Use Pi 5 Yamcs tcp-fast-client and the Zero W stock F′ TCP server with direct ComCcsds over LAN. Keep physical UART mux channel 2 for Teensy/PDU RPC.
2. OBC commands PDU payload power on and verifies the agreed power/readiness indicators.
3. Command the real PLD over USB↔RS-422 to collect; correlate acceptance, terminal status and the product.
4. Retain the raw product on the OBC and use stock FileDownlink SendFile. Verify received bytes, size and hash independently; command-OK, FileSent, or local buffer return alone is not receipt. Test disconnect/error recovery and issue a fresh SendFile after reconnect. The local ground helper can select a source or catalogued product through Yamcs and issue stock SendFile; the final loopback exercised this path.
5. Command PDU payload power off and verify the off state. Keep the onboard product after ground receipt.

This requires OBC, PLD and PDU. It does not require SatNOGS, SDR or RF. PDU board/rail/harness, startup/shutdown timing, readback and recovery need a revision-matched runbook. TCP and PDU software tests use local fake interfaces; target runtime, real raw-product transfer, and the hardware on/collect/retain/off sequence remain open. A build or synthetic payload result does not establish this hardware milestone.

## December 2026 FSR target

The FSR FlatSat extends the CDR payload/PDU integration with the complete COMMS and ground chain: Yamcs ↔ SDR ↔ SatNOGS ↔ OBC ↔ PLD, plus PDU payload power control. Starting in Base Mode, Yamcs schedules a collection-mode transition for time X. The command arrives through `SDR → SatNOGS → OBC`; F′ executes it at the scheduled time, powers the payload through EPS, and transitions the payload to acquisition. After the requested duration, the payload enters standby and the OBC commands processing mode to retrieve the raw data. F′ then returns to Base Mode and powers the payload off. The science product is downlinked through SatNOGS and SDR to Yamcs/ground.

Required FSR behavior is nominal modes, the listed subsystems, and time-based collection. Minimal SOH display is desirable. If additional scope is possible, SAFE is the highest-priority extra mode among SAFE, LEOPS, and EE&CO. ADCS, geo-based commanding, and HIL D2S2 depend on hardware availability and rank below the core FSR sequence.

## Software slice A: literal pass-through first

The first authorized implementation slice is a synthetic Yamcs-to-F′ pass-through against a simulated payload facade:

`Yamcs dummy command → F′ facade unchanged → simulated payload response → correlated Yamcs observation`

The facade must preserve the supplied literal command bytes/string without interpreting private opcode meaning. Use a dummy string in simulation; do not invent or issue a real payload opcode. Show the returned outcome correlated to the request in Yamcs. Use consistent F′ types and framework framing; do not add a test-tool length policy that conflicts with the actual F′ buffer configuration.

This proves only the public software seam. It does not define the real payload protocol or prove the physical payload interface. Preserve the existing `MissionApp → ScienceApp → PayloadManager → driver` ownership pattern unless source evidence demonstrates a need to change it; do not create a second mission authority.

**October 8 implementation/evidence:** `payloadManager.SYNTHETIC_PASSTHROUGH(requestId, literalCommand)` forwards to the simulated driver and returns a correlated `SyntheticPassthroughResponse` with status `SIMULATED_ECHOED`. Yamcs verified exact 59-byte and 128-byte ASCII literals; mode stayed BASE. This bounded facade uses a 128-byte printable-ASCII demo contract, distinct from Aris's unresolved protocol. See the [confirmed A operator walkthrough](../operations/walkthroughs/PASSTHROUGH.md). Slices C and B were accepted later for the local software profile; see their separate records below and in the [gap plan](SDD_GAP_PLAN.md).

The [A walkthrough](../operations/walkthroughs/PASSTHROUGH.md) documents slice A only. Its screenshots were captured after Yamcs acceptance; it does not document the later C/B flows or establish payload hardware, RF, or full CDR acceptance.

## Software slice C: serialized outcomes and recovery (accepted for local software scope)

`PayloadManager` is the sole admission and serialization point for every payload operation: literal pass-through, the current capture request, and any legacy or scheduled capture entry. All routes must enter the same arbiter before dispatching to a driver. Permit one in-flight operation. Any competing valid request gets `BUSY` without driver dispatch, including a request that reuses the active external request ID. Zero synthetic request IDs or explicit legacy collection IDs, empty literals, and invalid durations get `INVALID`; a missing/unavailable driver gets `DRIVER_UNAVAILABLE` without dispatch.

Every admitted request carries a Yamcs-visible request ID and an internal operation generation. A valid request that reuses an active request ID gets `BUSY` like any other competing request. Synthetic request IDs and explicit legacy collection IDs must be nonzero; duration-only commands allocate IDs internally, and `requestIn(0, duration)` requests allocation. Empty literals and invalid durations get `INVALID`. If a caller reuses an ID after completion, the new generation keeps it distinct from old callbacks. Emit `ACCEPTED` once dispatch begins, then exactly one terminal outcome for that request/generation. Admission rejections use generation zero and return their outcome without `ACCEPTED`. Ignore duplicate callbacks, callbacks after a terminal outcome, and callbacks whose generation is stale. A repeated request ID must not let an older response complete a newer operation.

Only a successful capture whose product descriptor/data pass validation may be forwarded to `ScienceApp` or stored. A successful literal pass-through is a command result and creates no capture/product record. `FAILED`, `TIMED_OUT`, `BUSY`, `INVALID`, `DRIVER_UNAVAILABLE`, stale, and duplicate results must never announce a completed collection or create a science product/storage record. Command acceptance remains distinct from operation completion.

Bound simulated capture child execution. On capture timeout or `HANG_CAPTURE`, the manager reports one `TIMED_OUT`; the driver terminates and reaps its child. Driver availability remains false until reap, preventing a new capture from overlapping the old child. A subsequent normal request must complete after availability returns.

Fault injection is local-profile-only through Yamcs command `CONFIGURE_SIMULATION`. It selects one fixture for the next admitted operation, then returns to `NORMAL`; the unavailable fixture is consumed by its simulated rejection. Supported fixtures are `NORMAL`, `FAIL`, `NO_RESPONSE`, `DELAYED_RESPONSE`, `DUPLICATE_RESPONSE`, `HANG_CAPTURE`, and `DRIVER_UNAVAILABLE`. A configured delay can complete normally or deliver a callback after the deadline, allowing the late response to be observed and ignored after `TIMED_OUT`. These controls model software tests, not payload protocol behavior. The command is rejected when the local profile is OFF.

**October 8 C acceptance:** the local Yamcs lifecycle run passed all 31 checks, including serialized admission, terminal outcomes, fault injection, stale/duplicate-response handling, bounded child cleanup, and recovery. The A regression also passed, and all 8 ScienceApp unit tests passed. Exact run artifacts and binary/dictionary hashes are recorded in the [SDD gap plan](SDD_GAP_PLAN.md#slice-c-acceptance-record-2026-10-08). This C result covers the local software profile only; B has its own later acceptance record. Neither establishes payload hardware, RF, or CDR acceptance.

Use the [lifecycle operator walkthrough](../operations/walkthroughs/LIFECYCLE.md) and reproducible [lifecycle runner](../../tools/yamcs/lifecycle.sh). The walkthrough records local C scope; it does not establish B or hardware/RF acceptance.

## Payload and product contract still needed

**October 8 follow-up authorized:** extend the accepted B software path with a KISS synthetic pulse fixture. Generate fake raw ADC pulses, deliver them through the existing storage/channel-1/Yamcs receipt path, and derive waveform, integrated-charge spectrum, and PSD density plots on the ground. Label all views SIMULATED; defer figure of merit and real calibration. The count-CSV profile remains a regression baseline. See the [synthetic pulse implementation plan](../verification/SYNTHETIC_PULSE_ACCEPTANCE.md); local software acceptance passed on October 8, including independent Yamcs raw readback/plot calculations, packet recovery, and browser download. See the [accepted operator pictures](../operations/walkthroughs/PULSE_SCIENCE.md).

Aris's payload board is expected to produce raw science data. Dennis confirmed USB↔RS-422 command/telemetry on October 18. Adapter/connector/serial settings, exact collection command, duration units, response semantics, raw-data framing/file transfer, and completion/integrity indicators remain unconfirmed. Keep Aris's private protocol/encoding out of mission logic and expose only an agreed public interface.

The local ground viewer parses the accepted simulator CSV schema (`t_s,counts,flag`) from Yamcs-retrieved raw bytes and displays a count-frequency histogram. This is a software demonstration of ground-side processing, not a detector-energy histogram or a validated science product. The payload team's JAR owns real science interpretation/display. Real raw samples will not be supplied for this software work; the FSW file-transfer contract remains open.

## Validation and integration sequence

The recorded board-free functional acceptance below used Yamcs and remains software evidence. The CDR profile and ground-only launcher have final native loopback acceptance, including independent FileDownlink receipt checks; guard tests, startup cancellation and legacy Yamcs regressions also passed. This does not prove Pi or hardware acceptance. A build or service-start result alone is setup evidence, not mission acceptance. Software history and the revised integration sequence are:

1. **A — pass-through:** Yamcs sends a synthetic literal string to a simulated payload facade; Yamcs shows the unchanged value at the facade and a correlated response.
2. **C — truthful outcomes (PASS, local software scope):** Yamcs lifecycle probe passed all 31 checks. The A regression passed and ScienceApp unit tests passed 8/8. See the evidence record in the gap plan. Source/build checks remain separate from these functional results.
3. **B — nominal raw-data cycle (PASS, local software scope):** `scienceApp.COLLECT_AND_DOWNLINK(durationSeconds)` automatically collects simulator data, validates/stores the product, sends immutable bytes on custom channel 1, reconstructs them independently at the local ground adapter, then uploads and reads back the raw object through Yamcs. The verified receipt carries run/product/transfer identity, byte count, CRC16, and SHA-256. The viewer derives a count-frequency histogram from the received simulator CSV. See the [B acceptance record](SDD_GAP_PLAN.md#slice-b-acceptance-record-2026-10-08) and [B operator walkthrough with screenshots](../operations/walkthroughs/RAW_CYCLE.md). This does not implement Aris's real command or raw-data contract.
4. **CDR local profile (PASS):** stock ComCcsds/TCP and stock FileDownlink loopback passed both multi-packet opaque fixtures with independent GET byte/hash checks. Fragmentation/disconnect recovery, missing-source error/no-object behavior, source/product helper, and concurrent fake channel-2 PDU RPC passed. Guard tests rejected both legacy automatic-science commands before capture/token allocation; startup cancellation exited in 0.444 seconds with both exact process groups gone. Legacy pass-through, pulse, ScienceApp, UART mux, and ground regressions passed. Then validate Pi Zero W/Teensy and real PLD/PDU power-on/off on hardware. SatNOGS/SDR/RF remains deferred; local software results do not establish hardware acceptance. See the [canonical implementation plan](CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) and [portable evidence pack](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md).
5. **FSR extension:** add SatNOGS/SPI, SDR and Yamcs ground delivery to the CDR payload/PDU chain, with the previously planned Base/collection/processing transitions, time-based execution and return to Base; validate the complete scheduled flow and independent ground receipt.

## Open decisions and questions

1. **Payload command and transport:** What exact public pass-through command syntax and duration units should F′ send? Which USB↔RS-422 adapter, connector/settings, and readiness/response signals does Aris's interface use?
2. **Aris production lifecycle:** The local C contract defines request ID/generation, `ACCEPTED`, and one terminal result (`COMPLETED`, `BUSY`, `INVALID`, `DRIVER_UNAVAILABLE`, `FAILED`, or `TIMED_OUT`). Confirm which of these the real Aris interface can report, its collection-start/progress/cancel behavior, and its real operation deadline and late-response rules before substituting the payload driver.
3. **Raw product boundary:** How does the payload delimit a raw product and identify its collection? What format, expected size/range, timestamp, and integrity indicator are available? Who finalizes the file on the OBC?
4. **Data access:** What retained raw-product inspection/retrieval is required for CDR? For FSR, which interface reconstructs science across real SatNOGS/SDR and what constitutes verified operator receipt? Local Yamcs readback is software evidence only.
5. **SatNOGS integration:** What SPI contract, pin/reset mapping, host endpoint, framing, throughput, and firmware readiness are Sam/Zach establishing? How will Dennis's F′ driver and the firmware endpoint be exercised together?
6. **Science view (resolved October 9):** use the payload team's supplied JAR for actual science viewing. FSW acquires/stores/downlinks opaque raw bytes. The local synthetic viewer remains a regression fixture; real sample files will not be supplied for this work.
7. **Scheduled FSR behavior:** Is time X absolute UTC or a relative delay? What clock-validity, restart, lost-link, cancellation, and persistence rules apply? What are the agreed mode names and payload-ready/EPS sequencing signals?
8. **CDR hardware details:** The direct Pi Zero W stock TCP server ↔ Pi 5 Yamcs tcp-fast-client path has final local loopback evidence; use the [canonical plan](CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) and [runbook](../operations/CDR_TCP.md) for current checks. Confirm the selected ground receipt location and how the operator selects a retained product. Which populated PDU, payload rail, harness and on/off/readiness timing will be used? COMMS is deliberately outside CDR; there is no RF fallback decision required for that milestone.

Record answers with an owner and date. Until then, use synthetic commands and payload data in software simulations and use the confirmed USB↔RS-422 allocation while leaving its unspecified electrical/protocol details open.

## October 9 scope update and next authorized slices

Dennis authorized **D: Yamcs completion verification** and **E: durable raw-product storage/restart recovery**, with functional testing through Yamcs. See the [implementation and acceptance plan](../verification/COMPLETION_STORAGE.md). Real payload samples will not be supplied for this work. The payload team provides the science-viewing JAR; our FSW scope is acquiring, storing, and downlinking opaque raw science bytes. Existing synthetic plots remain test fixtures. SatNOGS/SDR is being developed by another team. Public payload/transport integration remains a separate interface boundary, not a prerequisite for completing D/E using synthetic files.


October 9 software acceptance: completion verification and durable raw storage/restart recovery passed through Yamcs (six scenarios plus A/C/B and pulse regressions). See the [D/E implementation and acceptance record](../verification/COMPLETION_STORAGE.md). This is host-native synthetic raw-byte evidence, with actual payload viewing owned by the supplied JAR and SatNOGS/SDR owned by the other team.
