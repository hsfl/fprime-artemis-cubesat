# EPSCoR C3M Lepton to Neutron 2 F Prime Port Plan

## Companion Plan

This plan is the revised working plan. The original FABLE/Claude plan is
preserved unchanged at
[`docs/EPSCOR_C3M_LEPTON_FPRIME_PORT_FABLE_PLAN.md`](EPSCOR_C3M_LEPTON_FPRIME_PORT_FABLE_PLAN.md)
for traceability.

The focused follow-on plan for restoring real Lepton camera capture inside the
current driver seam is
[`docs/C3M_LEPTON_CAMERA_BACKEND_HIL_PLAN.md`](C3M_LEPTON_CAMERA_BACKEND_HIL_PLAN.md).

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

## Status Update - 2026-07-08

Core local MVP status: implemented and locally validated on `epscorc3m/demo`.
The remaining proof gates are HIL bench validation and measured downlink
optimization.

C3M HIL camera prep status: implemented locally on 2026-07-08. The current
`PayloadDriver_Lepton` now carries the real old-branch `libuvc` Lepton backend,
explicit backend selection, a Pi-only `testLeptonCamera` harness, and real
`.fdp` source CRC emission. Laptop validation still uses the checked-in sample
CSV through `LEPTON_CAMERA_BACKEND=sample`; HIL must run with
`LEPTON_CAMERA_BACKEND=uvc`.

Completed commits:

- `ade1d85 add c3m lepton payload driver and data products path`
  - Added `PayloadDriver_Lepton`.
  - Added project-owned `ArtemisDataProducts` config/subtopology.
  - Added `DpWrittenRouter` so the DpWriter notification reaches both the
    catalog path and the Lepton driver.
  - Wired the C3M mission topology through the existing
    `PayloadManager -> PayloadDriver_*` seam.
- `b606203 add c3m local demo and lepton decoder`
  - Added `tools/run_c3m_local_demo.sh`.
  - Added the Lepton `.fdp` decoder/viewer under `ground-station/lepton-dp-viewer/`.
  - Added payload receiver directory/continuous mode.
  - Added local-emulation tests and `validate_local.sh --demo c3m`.
- Current local/non-HIL update set
  - Added the dedicated C3M runbook:
    [`docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`](EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md).
  - Documented the Neutron/C3M payload-driver swap in
    [`docs/SYSTEM_ARCHITECTURE.md`](SYSTEM_ARCHITECTURE.md).
  - Added generated per-channel RF ACK policy constants.
  - Kept channel 0 / CCSDS ACKed and made channel 1 / payload ACK-free for the
    RF MVP bench candidate.
  - Added the Pi cross-compile helper's `--copy-only` workflow for deploying a
    previously verified binary without rebuilding.

Validated locally:

- `fprime-util generate -f`
- `fprime-util build`
- Python local-emulation tests
- Direct C3M local demo
- `./tools/validate_local.sh --demo c3m`

Measured local full-res downlink:

- Product: full-res Lepton `.fdp`
- Size: 38,480 bytes
- Payload packets: 1,100
- Local emulator timing from `PayloadDownlinkStarted` to
  `PayloadDownlinkComplete`: 34.72 seconds
- Effective local app rate: about 31.7 payload packets/s, about 1.1 KB/s

Important boundary:

- This proves the laptop-local F Prime command, event, Data Product, channel-1
  packetization, receiver, and viewer path.
- It does not prove real Lepton/libuvc behavior, Raspberry Pi deployment, Teensy
  UART/RF timing, channel-1 RF ACK behavior, queue-drop behavior, or bench CRC
  repeatability.

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

The original first-order bottleneck was application pacing in
`PayloadDownlinkApp`, not `.fdp`. That local bottleneck has now been addressed
with generated payload pacing constants and a 32-packet-per-run burst.

Current post-MVP shape:

- Lepton full-res `.fdp`: about 38.5 KB.
- Channel-1 payload packet data: 35 bytes per data packet.
- Full-res transfer: about 1,100 data packets before retry overhead.
- Current app pacing: 32 payload packets per 1 Hz run tick.
- Local emulator result: about 35 seconds for full-res, which is inside the
  original expected 35-60 second range.

The remaining unknown is the real RF bench behavior. Current generated RF
policy keeps per-segment ACK/retry on channel 0 and disables RF segment ACKs on
channel 1. Downlink optimization now needs HIL measurements to distinguish true
radio airtime limits, queue drops, and app-level retry behavior under the
ACK-free payload path.

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

Status:

- Local baseline completed after the C3M port: full-res `.fdp` downlink is about
  34.72 seconds in laptop emulation.
- HIL baseline is still pending and is now the next proof gate.

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

- Pre-pacing full-res was likely about 18 minutes because of the old 1-packet/s
  app pacing.
- Current local full-res is about 35 seconds with 32 packets/run.
- HIL timing is expected to be slower or more variable depending on RF ACK
  behavior, queue drain, and retry rounds.

Verification:

- Use the existing `payload_receiver.py --debug` path.
- Capture ground and satellite `#LINK_STATUS` before and after each run.
- Preserve exact ports, commands, and timing in the characterization doc.

### Phase 1 - Bus-Generic Hardening Cherry-Picks

Objective: bring over C3M branch fixes that are bus-level improvements, not
Lepton-specific architecture.

Status:

- Completed or superseded in the current tree.
- Satellite Teensy boot/radio sequencing, bounded RF23BP chip-ready probing,
  RF23 log-sink cleanup, and Pi service `WorkingDirectory` are already present.
- The remaining branch helper gap, `docker_cross_compile_pi_zero_w.sh
  --copy-only`, is now ported.
- Radio-absent/degraded-radio behavior still requires HIL or bench-like hardware
  proof.

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

Status:

- Implemented in the current tree.
- `config/transport_constants.json` now carries
  `payload.packets_per_run = 32` and `payload.retry_packets_per_run = 32`.
- `PayloadDownlinkApp` sends burst packets per run tick and repeats the header
  generically.
- Local full-res timing is about 35 seconds.
- HIL A/B timing remains pending.

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

Status:

- Implemented as the current RF MVP bench candidate.
- Channel 0 / CCSDS remains per-segment ACKed.
- Channel 1 / payload is RF-segment ACK-free and relies on app-level packet
  indexes, retry requests, and final CRC repair.
- HIL still needs to prove whether the ACK-free payload policy is stable on the
  real RFM23BP link.

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

Status:

- Implemented for the local MVP.
- Added project-owned `ArtemisDataProducts` config/subtopology.
- Added `DpWrittenRouter`.
- Full-res Lepton `.fdp` products are written locally and reported through an
  honest `ScienceProductDescriptor`.

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

Status:

- MVP implemented locally as `PayloadDriver_Lepton`.
- Local/native path uses explicit `LEPTON_CAMERA_BACKEND=sample` sample data for
  deterministic emulation, with `synthetic` still available only as an explicit
  fallback/test backend.
- The driver produces a full-res Lepton Data Product and emits a descriptor with
  the written path, byte count, and real source CRC.
- The real old-branch `libuvc` Lepton backend is restored inside
  `PayloadDriver_Lepton/LeptonCamera.*`, gated to Linux builds that find both
  `libuvc` and `libusb-1.0`.
- Real camera behavior remains a Pi/HIL validation item:
  `LEPTON_CAMERA_BACKEND=uvc ./testLeptonCamera`, then F Prime capture/downlink.
- Follow-on plan/status for backend selection, conservative `libuvc` build
  support, and real descriptor CRC:
  [`docs/C3M_LEPTON_CAMERA_BACKEND_HIL_PLAN.md`](C3M_LEPTON_CAMERA_BACKEND_HIL_PLAN.md).

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

Status:

- Optional and still pending.
- Not required for the current core MVP because full-res local downlink is about
  35 seconds.
- Still useful if HIL full-res timing is too slow for the live judge flow or if
  the team wants a sub-15-second visible product.

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

Status:

- Implemented for `epscorc3m/demo`.
- `payloadDriverLepton` is wired as the C3M payload driver.
- `payloadDriverNeutronSim` remains defined for cheap revert/swap.
- Documentation of the C3M/Neutron payload swap recipe is now in
  `docs/SYSTEM_ARCHITECTURE.md`.

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

Status:

- Receiver directory/continuous mode is implemented.
- Lepton `.fdp` viewer is implemented for full-res products.
- C3M local demo runner is implemented and wired into `validate_local.sh`.
- Dedicated C3M local/RF MVP runbook is now in
  `docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`.

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

Status:

- Pending.
- This is the next major gate now that the local MVP is green.

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

Status:

- Pending until HIL proves the port and the chosen downlink optimization.

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

## Next Local / Non-HIL Steps

These were completed before HIL prep:

1. Updated `docs/SYSTEM_ARCHITECTURE.md` with the C3M/Neutron payload swap recipe.
2. Added a dedicated C3M local/RF MVP runbook that covers:
   - `./tools/validate_local.sh --demo c3m`
   - `ArtemisRpiTeensy_N2/tools/run_c3m_local_demo.sh`
   - Lepton viewer command and expected JSON/CSV outputs
3. Implemented the per-channel ACK policy behind generated constants.
4. Added generator/check coverage for ACK policy constants so F Prime, satellite
   Teensy, and ground Teensy cannot drift.
5. Added `--copy-only` to the Pi cross-compile helper so a previously verified
   binary can be deployed/smoked without a rebuild.

Completed local/non-HIL gates after the transport edit:

1. `./tools/validate_local.sh --demo c3m`
2. Satellite Teensy Arduino build
3. Ground Teensy Arduino build
4. `ArtemisRpiTeensy_N2/tools/docker_cross_compile_pi_zero_w.sh --local-only`

Completed local/non-HIL gates after real Lepton backend restore:

1. `fprime-util generate -f`
2. `fprime-util build`
3. `./tools/validate_local.sh --demo c3m`
4. `./tools/docker_cross_compile_pi_zero_w.sh --local-only`

Remaining local/non-HIL work before HIL:

1. Install/sync `libuvc`, `libuvc/libuvc.h`, and `libusb-1.0` into the Pi build
   environment, or build natively on the Pi, until CMake reports
   `PayloadDriver_Lepton: libuvc enabled`.
2. Re-run the Pi Zero W build gate after the camera libraries are visible. The
   current cross-built ARMv6 binary is architecture-valid, but it compiled the
   fail-hard non-`libuvc` path because those libraries were not in the sysroot.
3. Keep preview mode optional. Implement it only if HIL full-res timing is too
   slow for the live demo story.

## Downlink Optimization Summary

Current RF policy:

- Channel 0 / CCSDS: RF segment ACK/retry enabled.
- Channel 1 / payload: RF segment ACK/retry disabled by generated policy.
- Per-channel ACK policy is generated from `config/transport_constants.json` and
  checked across F Prime, satellite Teensy, and ground Teensy headers.
- Current constants:
  - `RF_ACK_RETRIES = 4`
  - `RF_ACK_TIMEOUT_MS = 80`
  - `RF_INTER_SEGMENT_GAP_MS = 8`
  - `PAYLOAD_PACKETS_PER_RUN = 32`
  - `PAYLOAD_RETRY_PACKETS_PER_RUN = 32`
  - `PAYLOAD_PACKET_DATA_BYTES = 35`

Recommended optimization order:

1. HIL baseline the current 32-packet/run, channel-1 ACK-free behavior.
   - Capture wall-clock time, retry rounds, CRC result, queue drops, RF retries,
     RF ACK timeouts, and channel-0 command responsiveness.
2. If HIL is unstable, roll channel 1 back to ACKed in
   `config/transport_constants.json`, regenerate, rebuild both Teensies, and
   rerun the same HIL sequence.
3. Sweep app pacing only after observing HIL counters.
   - Try 32, 64, 96, then 128 packets/run if queue drops stay zero and channel 0
     remains responsive.
4. Treat larger payload app packets as a later experiment, not the primary fix.
   - Today each `PayloadDownlinkApp` packet carries 35 data bytes and fits in one
     RF segment.
   - Larger app packets would reduce app-layer packet count and header overhead,
     but the Teensy would still split the bytes into RF-sized segments.
   - If the true limiter is RF segment rate, larger app packets will not solve it.
5. Treat RF inter-segment gap tuning as conditional.
   - The 8 ms gap matters mainly for multi-segment RF messages.
   - With the current one-app-packet-to-one-RF-segment shape, it is not the first
     downlink lever.

Decision rule:

- If current HIL full-res is <= 60 seconds with clean counters, keep the current
  ACK-free payload policy and do not add preview mode.
- If current HIL full-res is 60-120 seconds but clean, consider tuning app
  pacing next.
- If CRC failures or retry rounds dominate, A/B channel-1 ACK-on rollback before
  changing the app layer.
- If queue drops appear, reduce packets/run rather than hiding the problem with
  retries.

## Recommendations Before HIL

1. Freeze or tag the current FSR-safe `neutron2-develop` state before invasive
   transport work.
2. Start HIL with the current 32-packet/run, channel-1 ACK-free candidate and
   record the before/after table for the FSR writeup.
3. Optimize only from measured counters: channel-1 ACK-on rollback and
   packet-rate sweeps should be A/B tests, not assumptions.
4. Keep `.fdp`, but make path/CRC honest. The Lepton driver must not rely on
   `PayloadDownlinkApp` guessing the newest file in `./DpCat`.
5. Preserve the N2 architecture names and seams. The port should produce
   `PayloadDriver_Lepton`, not resurrect `PayloadAdapter_Lepton` as-is.
6. Treat preview mode as optional. Full-res is the science proof; preview is only
   needed if HIL full-res timing is too slow for the live demo.

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
