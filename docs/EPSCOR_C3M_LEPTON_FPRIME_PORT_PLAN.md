# EPSCoR C3M Lepton to Neutron 2 F Prime Port Plan

## Companion Plan

This plan is the revised working plan. The original FABLE/Claude plan is
preserved unchanged at
[`docs/EPSCOR_C3M_LEPTON_FPRIME_PORT_FABLE_PLAN.md`](EPSCOR_C3M_LEPTON_FPRIME_PORT_FABLE_PLAN.md)
for traceability.

## Intent

Dennis's intent is to converge two mission efforts onto one reusable Artemis
CubeSat F Prime bus:

- `neutron2-develop` is the current Neutron 2 architecture baseline: the
  App-Manager-Driver naming, channelized UART/RF transport, hardened demo flow,
  payload sidecar downlink, and current validation path.
- `EPSCOR_C3M_REFACTOR` is the working Lepton thermal-camera payload branch,
  but it predates the latest Neutron 2 architecture cleanup and its payload
  downlink is too slow for the live demo.

The goal is not to maintain two separate architectures. The goal is to port the
major Neutron 2 bus changes into the EPSCoR C3M refactor by working from
`epscorc3m/demo` (created from `neutron2-develop`) and transplanting the C3M
payload-specific assets into that architecture.

The Lepton payload should keep F Prime Data Products (`.fdp`) as the captured
science product format. The speed fix should optimize the existing channel-1
payload downlink path for `.fdp` bytes. Do not switch to plain file staging just
to improve speed, and do not use stock `Svc.FileDownlink` over the RFM23BP RF
path for this MVP.

## Current Understanding

### Architecture Direction

The active target architecture is the Neutron 2 App-Manager-Driver stack:

- Applications: `MissionApp`, `ScienceApp`, `CommsApp`, `SoHApp`,
  `PayloadDownlinkApp`
- Managers: `PayloadManager`, `StorageManager`, `EpsManager`,
  `TeensyTransportManager`, etc.
- Drivers: `PayloadDriver_NeutronSim` today, `PayloadDriver_Lepton` for this
  mission, and future `PayloadDriver_*` variants for other payloads.

The mission identity should be a small wiring choice at the driver seam, not a
forked architecture. For now, use one deployment and swap payload driver wiring
in the topology. A second deployment variant can wait until the team has a real
need for parallel mission builds.

### `.fdp` Is Not the Main Speed Problem

Keeping `.fdp` is reasonable. `PayloadDownlinkApp` treats the source as opaque
bytes, so the file extension is not what makes the transfer slow. The `.fdp`
adds some metadata overhead, but the dominant cost is how quickly the channel-1
payload packets are fed into the RF relay and whether the RF relay waits for a
per-segment ACK on payload traffic.

The important distinction:

- `.fdp` is the science product format.
- Channel 1 is the transport.
- `PayloadDownlinkApp` is the generic app-level packetizer/retry owner.
- `payload_receiver.py` and the viewer are the ground reconstruction/decode path.

Do not move `.fdp` over stock CCSDS file downlink for this radio. The custom
channel-1 sidecar exists because the RFM23BP path is too constrained for large
byte-perfect CCSDS file transfers.

### Current Bottleneck

The strongest current diagnosis is that the first-order bottleneck is the
application pacing in `PayloadDownlinkApp`, not `.fdp`.

Current shape:

- Lepton full-res `.fdp`: about 38.5 KB.
- Channel-1 payload packet data: 35 bytes per data packet.
- Full-res transfer: about 1,100 data packets before retry overhead.
- Current effective send pacing: roughly 1 payload packet per second.
- Result: about 18 minutes for full-res, which is unusable for the demo.

This means Phase 2 pacing is the key first implementation gate. RF ACK policy
still matters, but it should be treated as a measured transport optimization
after pacing is no longer the obvious limiter.

## Non-Goals

- Do not merge `EPSCOR_C3M_REFACTOR` wholesale into the N2 branch.
- Do not restore the old Service/Adapter naming or old payload topology.
- Do not put Lepton-specific `./DpCat` scanning into `PayloadDownlinkApp`.
- Do not edit `lib/fprime` submodule config directly for DP buffer sizing.
- Do not replace the channel-1 sidecar with stock `Svc.FileDownlink` for the
  RFM23BP MVP.
- Do not block the C3M payload port on a speculative high-throughput transport
  rewrite if pacing alone meets the demo target.

## Updated Phased Plan

### Phase 0 - Measure Current Baseline

Objective: establish the actual current performance before changing anything.

Record the baseline in a new doc such as
`docs/TRANSPORT_CHARACTERIZATION_2026-07.md`.

Measure:

- Full-res `.fdp` wall-clock time.
- Receiver completeness and retry rounds.
- `rf_retries`, `rf_ack_timeouts`, `rf_msg_id_gaps`, queue drops, payload RF
  TX/RX segment counters.
- RSSI where available.
- Whether channel 0 commands remain responsive during channel-1 downlink.

Expected result:

- Full-res is likely about 18 minutes because of the current app pacing.

Verification:

- Use the existing `payload_receiver.py --debug` path.
- Capture ground and satellite `#LINK_STATUS` before and after each run.
- Preserve exact ports, commands, and timing in the characterization doc.

### Phase 1 - Bus-Generic Hardening Cherry-Picks

Objective: bring over C3M branch fixes that are bus-level improvements, not
Lepton-specific architecture.

Candidates:

- Satellite Teensy boot order: radio init before Pi power-enable.
- Bounded RFM23BP chip-ready probe so radio-absent boot degrades instead of
  hanging.
- `rf23_driver.cpp` log-sink fix.
- Pi Zero W cross-compile/toolchain fixes that are still applicable.
- `run_gds_uart.sh` dictionary/default improvements if not already superseded.
- Reconcile systemd units and keep the current `deploy/pi/artemis-fprime.service`
  source of truth.

Verification:

- `./tools/validate_local.sh`
- Satellite and ground Teensy Arduino builds.
- HIL cold-boot smoke.
- Radio-absent or radio-unhealthy boot behavior does not wedge the bridge.

### Phase 2 - Payload Downlink Pacing Fix

Objective: remove the obvious 1-packet-per-second limiter without changing the
RF protocol.

Design:

- Add generated payload downlink pacing constants, for example:
  - `payload.packets_per_run`
  - `payload.retry_packets_per_run`
- Render them through `tools/generate_transport_constants.py` into the F Prime
  `LinkCfg` constants.
- Update `PayloadDownlinkApp` to send a burst per scheduler tick instead of one
  packet per tick.
- Start with a conservative sweep: 8, 16, then 32 packets per tick.
- Port C3M's repeated header-send improvement, but keep it generic.
- Do not port the C3M `./DpCat` newest-file detection hack.

Recommended gate:

- Phase 2 is the real first decision point.
- If full-res reaches acceptable timing with ACKs still enabled, defer
  per-channel UNACK until after the near-term FSR risk is lower.

Targets:

- Full-res `.fdp`: less than or equal to 120 seconds as the minimum useful gate.
- Expected with good pacing and ACKs still on: roughly 35-60 seconds.
- Preview/downsample `.fdp`: less than or equal to 15 seconds once preview mode
  exists.

Important caveat:

- A 1 Hz tick with 32 packets per run caps app feed rate near 32 packets/s.
  Claims like "150 segments/s" require a separate relay-level stress test or a
  faster app feed. Do not mix those metrics.

Verification:

- Unit tests for burst count, retry burst count, and repeated header behavior.
- Local emulation and receiver tests.
- HIL A/B test against Phase 0.
- Require zero queue drops at the chosen burst size.

### Phase 3 - Conditional Per-Channel RF ACK Policy

Objective: make channel 1 faster by removing redundant stop-and-wait RF ACKs
only if Phase 2 is not enough or the team wants the transport optimization.

Design:

- Add a generated per-channel ACK policy in `config/transport_constants.json`,
  for example:
  - channel 0 / CCSDS: ACK required
  - channel 1 / payload: ACK not required
- Generate the policy into both Teensy `link_protocol.hpp` copies and the F Prime
  link constants.
- Extend `tools/check_transport_constants.py` so mismatched ACK policy across
  trees fails validation.
- Guard send-side ACK waits and receive-side ACK sends in both Teensy relay
  implementations.

Policy:

- Keep channel 0 ACKed because CCSDS/GDS has no app-level repair.
- Allow channel 1 UNACK because `PayloadDownlinkApp` plus `payload_receiver.py`
  already implement app-level selective repair and whole-file CRC.

Risk:

- Prior HIL evidence showed channel 1 became reliable only after adding
  per-segment ACK. Removing it must be treated as an A/B bench experiment, not a
  paper-only refactor.

Verification:

- HIL A/B runs with ACK-on and ACK-off payload channel.
- `rf_ack_timeouts` should stay flat during channel-1 transfer when payload ACK
  is disabled.
- Channel 0 `NO_OP` or equivalent command remains responsive mid-transfer.
- 5/5 full-res and preview transfers complete with CRC after app-level repair.

### Phase 4 - Project-Owned DataProducts Path

Objective: support large Lepton `.fdp` products without editing the F Prime
submodule and without leaking Data Product file discovery into
`PayloadDownlinkApp`.

Design:

- Keep `.fdp` as the payload science product format.
- Provide a project-owned DataProducts configuration or vendored subtopology so
  the DP buffer size can hold full-res Lepton products.
- Add a small `DpWrittenRouter` or equivalent fan-out so the written file path
  still reaches the catalog and also reaches `PayloadDriver_Lepton`.
- Use the real DpWriter file path and size for `ScienceProductDescriptor`.

Recommendation:

- Vendoring the DataProducts subtopology is heavy but defensible if it is the
  only clean way to both increase buffer size and fan out `dpWrittenOut`.
- Keep the vendored copy narrowly scoped and clearly documented as an Artemis
  bus customization.

Verification:

- Confirm DP buffer size comes from project-owned config, not `lib/fprime`.
- Build and unit test.
- Generate a synthetic full-res `.fdp` locally and prove the descriptor receives
  the true path, byte count, and CRC.

### Phase 5 - `PayloadDriver_Lepton`

Objective: port the working C3M Lepton capture code into the current N2 driver
seam.

New component:

- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/`

Required behavior:

- Active component, matching the `PayloadDriver_NeutronSim` seam:
  - `requestIn: Components.PayloadCaptureRequest`
  - `statusOut: Components.ScienceProductDescriptor`
  - `pingIn` / `pingOut`
  - `timeCaller`
- Data Product producer ports for `.fdp` generation.
- `dpWrittenIn` or equivalent notification path from DpWriter.

Capture contract:

- `ENABLE` starts or opens the Lepton stream.
- `DISABLE` stops/releases it.
- Scheduled collection captures one product.
- `durationSeconds` may be accepted for interface compatibility, but full-res
  Lepton can still produce one frame per request unless preview/averaging policy
  says otherwise.

Descriptor contract:

- On successful DpWriter notification:
  - compute CRC16 over the written `.fdp`
  - emit `statusOut(productId, productBytes, REAL_PAYLOAD, sourcePath, sourceCrc)`
- On failure:
  - emit `statusOut(0, 0, UNKNOWN, "", 0)`

Build contract:

- Restore a clean stub-vs-libuvc split so local/native unit tests do not require
  Lepton hardware.
- Keep the real libuvc path for Pi/hardware builds.
- Preserve C3M fixes such as Y16 stream handling and blank-frame rejection.

Verification:

- Unit tests with deterministic stub camera.
- Native F Prime build and UTs.
- ARMv6 cross-compile.
- Pi hardware `testLeptonCamera` run.

### Phase 6 - Preview Mode

Objective: provide judge-visible pacing without sacrificing full-res science
proof.

Design:

- Add a capture mode enum or parameter:
  - `FULL_RES`: 160 x 120 U16
  - `PREVIEW`: 80 x 60 U8 or compact U16, depending on viewer needs
- Driver performs downsample/preview generation before writing `.fdp`.
- Viewer supports both full-res and preview product shapes.

Expectation:

- Full-res is the science proof.
- Preview is the live-demo product.

Targets:

- Preview transfer less than or equal to 15 seconds.
- Full-res transfer less than or equal to 60-120 seconds depending on whether
  Phase 3 is in scope before the demo.

### Phase 7 - Topology Integration and Mission Identity

Objective: wire C3M as a payload mission on the same bus without deleting the
NeutronSim path.

Design:

- Add `payloadDriverLepton` instance with a free base ID.
- Rewire only the `PayloadManager` driver binding lines for the C3M mission.
- Keep `PayloadDriver_NeutronSim` defined so revert/swap is cheap.
- Add health ping entries for the Lepton driver.
- Add DataProducts/DpWritten wiring from Phase 4.

Mission model:

- One bus.
- Multiple payload drivers.
- Mission identity is the topology wiring plus runbook/defaults.

Document:

- Update `docs/SYSTEM_ARCHITECTURE.md` later with the C3M/Neutron payload swap
  recipe once the implementation is proven.

### Phase 8 - Ground Tooling

Objective: make the ground flow usable by students and demo operators.

Receiver:

- Port directory/continuous mode into `tools/payload_receiver.py`.
- Keep the receiver pyserial-only and native-Windows-friendly where possible.
- Preserve debug output needed for bench characterization.

Viewer:

- Add a Lepton `.fdp` viewer under a ground-station directory.
- Support full-res and preview product shapes.
- Document Windows setup explicitly.
- Keep the large decoded JSON out of the repo if it is regenerable.

Runbook:

- Command sequence should use the current N2 command names.
- Include expected receiver output, output file names, and viewer commands.

### Phase 9 - HIL Acceptance Gate

Objective: prove the port on the real bench before merge-back.

Acceptance targets:

- At least 5 full-res transfers and 5 preview transfers.
- Full-res completes with CRC.
- Preview completes with CRC and renders plausibly.
- Zero queue drops at selected pacing.
- Retry rounds less than or equal to 2 in normal bench conditions.
- Channel 0 commands remain usable during channel-1 payload transfer.
- `./tools/validate_local.sh` passes.
- Cross-compiled Pi binary runs on the target.
- Radio-absent or degraded-radio boot does not wedge the system.

Suggested timing gates:

- Minimum acceptable full-res: <= 120 seconds.
- Strong target full-res: <= 60 seconds.
- Preview target: <= 15 seconds.

### Phase 10 - Merge Back to `neutron2-develop`

Objective: bring bus-level improvements back without accidentally turning
Neutron 2 into the C3M mission.

Before merge-back:

- Confirm the FSR/demo release candidate tag exists.
- Keep `neutron2-develop` demo path intact.

Merge back in this order:

1. Bus-generic hardening.
2. Pacing constants and `PayloadDownlinkApp` generic improvements.
3. Per-channel ACK policy only if bench-proven and still wanted.
4. Project-owned DataProducts support if inert for NeutronSim.
5. Additive Lepton component and ground tooling.

Do not merge the C3M topology wiring swap into `neutron2-develop` unless the
team explicitly wants Neutron 2's default payload changed. Mission identity
should remain an intentional wiring choice.

## Recommendations Before Starting Tomorrow

1. Freeze or tag the current FSR-safe `neutron2-develop` state before invasive
   transport work.
2. Start with Phase 0 measurement, even if the current timing is obviously bad.
   The before/after table will be useful for the FSR writeup.
3. Do Phase 2 pacing before Phase 3 ACK policy. Pacing is lower risk and likely
   gets most of the win.
4. Treat Phase 3 as conditional. If Phase 2 plus preview meets demo needs,
   defer UNACK until after the near-term demo risk is lower.
5. Keep `.fdp`, but make path/CRC honest. The Lepton driver must not rely on
   `PayloadDownlinkApp` guessing the newest file in `./DpCat`.
6. Preserve the N2 architecture names and seams. The port should produce
   `PayloadDriver_Lepton`, not resurrect `PayloadAdapter_Lepton` as-is.

## Working Definition of Done

The port is successful when:

1. The current Neutron 2 bus architecture remains intact.
2. `PayloadDriver_Lepton` can replace `PayloadDriver_NeutronSim` through the
   documented driver seam.
3. The Lepton driver emits real `.fdp` products and an honest
   `ScienceProductDescriptor`.
4. `PayloadDownlinkApp` downlinks the `.fdp` bytes generically over channel 1.
5. The ground receiver reconstructs the file and the viewer renders the thermal
   image.
6. Full-res and preview transfers meet the HIL timing/CRC gates.
7. Bus-level improvements can merge back to `neutron2-develop` without changing
   Neutron 2's default mission wiring.

## Critical Files

- `config/transport_constants.json`
- `tools/generate_transport_constants.py`
- `tools/check_transport_constants.py`
- `ArtemisRpiTeensy_N2/Components/LinkCfg/`
- `ArtemisRpiTeensy_N2/Components/PayloadDownlinkApp/`
- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/`
- `ArtemisRpiTeensy_N2/Components/Types/Types.fpp`
- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/`
- `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.*`
- `GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.*`
- `ArtemisRpiTeensy_N2/tools/payload_receiver.py`
- `ground-station/`
- `docs/SYSTEM_ARCHITECTURE.md`
- `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`
