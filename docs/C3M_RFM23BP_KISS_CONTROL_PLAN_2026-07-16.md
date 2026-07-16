# C3M RFM23BP KISS Control and Recovery Plan

**Date:** 2026-07-16

**Status:** Software implementation, local emulation, ARMv6 deployment, and close-range software-observable HIL passed on 2026-07-16. Physical electrical/destructive gates remain pending.

**Scope:** Conservative software-only reliability work derived from Options 1, 4, and 5 in `C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md`.

## BLUF

Make the Raspberry Pi/F Prime deployment the radio policy owner and the Teensy a deterministic hardware executor.

On every Teensy reset, put the RFM23BP in real datasheet shutdown using `SDN = HIGH`, assert `RPI_ENABLE` as early as possible, and bring up the local Pi-to-Teensy channel before attempting RF initialization. Once F Prime is running, it declares the radio desired `READY` and issues `SET_ENABLED(true)`. If initialization fails or hangs, the Teensy returns or watchdog-resets into radio `OFF`; the Pi stays alive, reports comms degraded, and F Prime retries forever at a conservative capped cadence until the critical communications service is restored.

This is not a tight Teensy retry loop: every F Prime request causes at most one RadioHead initialization attempt, only one RPC may be outstanding, and retry delays are `30 s`, then `120 s`, then `900 s` between later attempts. A successful `READY` report resets the backoff.

The radio needs only two steady hardware states:

- `OFF`
- `READY`

Failure information is metadata, not another radio state. F Prime may report the spacecraft/comms service as degraded when the radio is `OFF` because of a fault.

This is simpler and more conservative than letting radio initialization participate in the Pi power-up decision.

## Intent Confirmation

The intended outcome is:

- The Raspberry Pi remains the spacecraft brain.
- A finicky or stalled RFM23BP must never intentionally hold the Pi off.
- The Teensy exposes factual status and performs safe, low-level actions; it does not own retry policy.
- Software can actually place the RFM23BP into its datasheet shutdown state through the real `SDN` pin.
- RadioHead remains unchanged and trusted.
- The proven RF frequency, bitrate, power, framing, addressing, ACK behavior, and payload path remain unchanged.
- The direct Teensy USB 5 V-to-radio VCC jumper remains the accepted MVP power arrangement, subject only to a non-modifying safety verification.
- Nominal boot gets one simple enable attempt. A bad attempt produces degraded comms and slow, bounded F Prime recovery attempts, not a Teensy boot loop or dead spacecraft.
- States, commands, retry paths, telemetry concepts, and LED patterns are reduced rather than expanded.

## Selected Architecture

### Ownership

| Owner | Responsibilities |
|---|---|
| Teensy | Apply safe pin levels immediately, execute `OFF`/`ON`, call the unchanged RadioHead initialization once per request, guard all radio access, use its watchdog as the final escape, and report factual results. |
| Raspberry Pi/F Prime | Maintain the desired radio service state, request nominal enable, retry failed enable attempts with capped backoff, report degraded comms, and expose operator telemetry/events. |
| RFM23BP `SDN` | Provide functional chip shutdown and a clean power-on-reset boundary. It does not physically remove the external 5 V rail. |

### Why the Pi should request the first enable

The cleanest MVP sequence is for the Teensy to boot safely `OFF`, bring up the Pi and channel 2, and let F Prime issue the one nominal enable request.

This is preferred over a Teensy automatic RF attempt because:

- RF cannot carry useful F Prime traffic before the Pi is running.
- A hanging RadioHead initialization can reset only the Teensy while the Pi retains policy state.
- No retained `INIT_IN_PROGRESS` marker is needed.
- No special "cold boot versus watchdog boot" initialization branch is needed.
- The same idempotent RPC path handles nominal enable, intentional shutdown, and every later bounded recovery attempt.

The tradeoff is only that RF becomes ready after F Prime starts. That delay is acceptable for this architecture and MVP.

## Three-View Design Check

### Satellite-bus view

A critical processor should not depend on successful startup of a high-current peripheral. The radio should default safe, the flight computer should boot independently, and fault recovery policy should have one owner. A future flight bus should provide a switched and monitored radio rail, but that is hardware follow-up rather than a requirement for this software MVP.

### Embedded view

Two steady states, two idempotent operations, ready guards, and one watchdog escape are easier to reason about than multiple initialization and recovery states. A missing peer or RF ACK is not proof of a broken local radio and must not trigger recovery. Only factual local evidence such as `OFF`, initialization failure, RPC timeout, protocol error, or a Teensy watchdog reboot drives the slow recovery policy.

### F Prime view

The existing App/Driver split and channel-2 RPC path already fit this design. `CommsApp` should own the desired-versus-actual state and capped recovery policy, while `CommsDriver_TeensyRfm23` owns the one-pending RPC mechanism and timeout. No new boot coordinator, UART channel, or F Prime component is needed.

**Consensus:** Pi decides; Teensy safely executes; failed radio service is degraded comms, not failed spacecraft boot.

## Verified Hardware and Software Truth

### Hardware

- On the inspected v4.23, v4.24, and v4.24 RF-plane designs, RFM23BP `SDN` is U3 pad 7 and routes directly to Teensy pin 37.
- There is no PCB pull-up, pull-down, transistor, or level shifter on this net.
- `RPI_ENABLE` is Teensy pin 36.
- Do not use the net named `RADIO_RESET` as an RFM23BP reset. It is shared with the other radio and connects to the RFM23BP `GPIO_0`/POR-related pin, not a dedicated RFM23BP reset input.
- The v4.23 and v4.24 physical boards cannot be identified reliably from the printed `4.23` PCB marking.
- v4.23 routes RFM23BP VCC through R8 to `5V_BUS`; v4.24 routes it through R8 to `SW_3V3_2`.

Therefore the direct 5 V jumper is accepted for this MVP, but R8/continuity must be checked on each physical node. A populated v4.24 R8 could otherwise let the jumper backfeed the switched 3.3 V rail. This is a verification gate, not a power-system redesign.

### RFM23BP behavior

- `SDN = HIGH` places the chip in shutdown, loses register state, and makes SPI unavailable.
- A falling edge on `SDN` initiates power-on reset.
- The datasheet allows up to 40 ms for the reset interval; use 50 ms as the simple conservative wait.
- `SDN` is functional shutdown while VCC remains present. True rail isolation requires a load switch or PDU-controlled rail and cannot be implemented in software.
- Teensy pins are 3.3 V and are not 5 V tolerant. Never add or drive 5 V directly onto pin 37.

### Pre-implementation gaps closed

- Satellite setup now makes radio pins safe and asserts `RPI_ENABLE` before any RF initialization.
- The project wrapper uses an SDN power-on-reset boundary and stable ID reads before one unchanged RadioHead initialization attempt.
- Every relay access is readiness-guarded; a failed initialization and a factual terminal local TX failure force safe `OFF`.
- Channel 2 exposes radio `STATUS` and idempotent `SET_ENABLED` service operations.
- The F Prime driver permits one pending request, validates responses and request IDs, and times out after 15 seconds.
- The public Teensy model is only `OFF`/`READY`; failure remains separate metadata and `CommsApp` is the single radio-health policy owner.
- RSSI is invalid until a correctly addressed packet is accepted and is invalidated again on shutdown.

## Minimal State Model

### Teensy radio state

| State | Meaning | Required hardware behavior | Allowed radio access |
|---|---|---|---|
| `OFF` | Intentionally disabled, not yet enabled, or safely contained after a fault | Amplifier idle, CS high, `SDN` high, `ready = false` | No RFM23BP SPI calls |
| `READY` | RadioHead initialization completed successfully | `SDN` low, configured RX/amp behavior preserved | Normal guarded RX/TX/statistics calls |

Initialization is a transient function call, not a durable public state.

### Fault metadata

Keep the last result separate from state. The minimum useful reasons are:

- `NONE`
- `INIT_FAILED`
- `WATCHDOG_RESET`
- `LOCAL_TX_FAULT`

`OFF + NONE` means intentional/not-yet-enabled. `OFF + non-NONE` means F Prime should report degraded comms. Do not add separate Teensy states for `UNINITIALIZED`, `INITIALIZING`, `ACQUIRING`, `LOCKED`, or `DEGRADED`.

An ACK timeout, absent peer, low RSSI, or quiet RF channel is not by itself a local hardware fault.

## Minimal Boot and Recovery Sequence

1. At the earliest safe point in Teensy setup:
   - configure CS and hold it high;
   - configure the existing amplifier controls to their tested idle combination;
   - configure pin 37 and drive `SDN` high;
   - set software state to `OFF`.
2. Assert `RPI_ENABLE` high immediately after those safe outputs. Never intentionally lower it because of a radio fault.
3. Start the Teensy watchdog, Pi UART, channel mux/router, PDU support, and local channel 2.
4. Do not initialize RF inside the Pi power-up gate.
5. F Prime requests `RADIO_STATUS` after its comms manager starts.
6. F Prime sets desired state to enabled and makes one nominal `RADIO_SET_ENABLED(true)` request.
7. Teensy enable execution:
   - ensure the radio is first safely `OFF`;
   - hold `SDN` high for 50 ms;
   - drive `SDN` low and wait 50 ms;
   - perform stable, non-mutating device-ID reads;
   - call the unchanged RadioHead initialization exactly once;
   - on success, enter `READY`;
   - on a returned failure, force `OFF`, record `INIT_FAILED`, and respond with failure.
8. If RadioHead hangs, the Teensy watchdog resets the Teensy. The reboot returns to `OFF`, brings the Pi/channel 2 back first, and reports the watchdog boot cause. The F Prime driver waits longer than the 12-second watchdog before declaring the RPC timed out.
9. F Prime reports degraded comms and schedules the same idempotent enable operation after `30 s`, then `120 s`, then every `900 s`. It never overlaps requests or retries immediately.
10. `READY` resets the failure count/backoff. The low-level idempotent `SET_ENABLED(false)` remains available to trusted Pi/bench software for verified shutdown testing, but nominal F Prime policy remains desired-enabled. No distinct reset, reinitialize, recover, or power-cycle RPC is added.

During normal operation, the relay performs one bounded low-level FIFO recovery
retry for a local transmit completion/start failure. If that retry also fails,
the Teensy records `LOCAL_TX_FAULT`, invalidates stale RSSI, asserts SDN, and
returns to `OFF`. F Prime observes the factual fault and reuses the same bounded
enable sequence. Peer silence and ordinary ACK loss do not prove a local wedge
and therefore do not power-cycle the radio.

The software can drive pin 37 high very early, but the board has no hardware pull on `SDN`; software cannot guarantee the level while the Teensy itself is held in reset. That limitation belongs in the HIL result and future hardware backlog.

## Channel-2 RPC Contract

Use the existing channel and RF target. Do not add a new UART or component protocol.

### Operations

1. `RADIO_STATUS`
   - May retain the existing link-statistics opcode value for compatibility.
   - Returns state, last fault, relevant boot cause, counters, and honest RSSI fields.
2. `RADIO_SET_ENABLED(bool enabled)`
   - `false`: immediately mark not ready, place amplifier/CS safe, assert `SDN`, and perform no later radio SPI access.
   - `true`: if already healthy `READY + NONE`, return success without reinitializing; if `READY` carries a factual fault, first enter safe `OFF`; otherwise perform exactly one enable sequence.

### RPC rules

- Permit exactly one outstanding radio request.
- Validate target, request ID, operation, status, and response length.
- Base the implementation on the already-proven pending-request/timeout behavior in `EpsDriver_Artemis`.
- Make the F Prime enable timeout longer than the 12-second Teensy watchdog.
- A timeout may mean the Teensy reset; it schedules the next F Prime attempt through the same capped backoff as any other local enable failure.
- While `OFF`, reject and count channel-0/channel-1 traffic instead of queueing stale data for replay after recovery.
- Keep channel 2 responsive whenever the Teensy is running.
- Do not expose a persistent public `RADIO_SET_ENABLED(false)` command over the spacecraft's only RF link today. It could strand the spacecraft or suppress its own acknowledgment. Exercise the real low-level shutdown RPC through local/bench testing; a future operational disable command requires a separately reviewed automatic-wake deadline or alternate command link.

## RadioHead Boundary

Do not modify, vendor, or replace RadioHead.

Software changes later should stay in the project-owned wrapper, driver, relay, router, and F Prime components. Remove the redundant mutating preflight reset from the wrapper path; after the SDN POR and stable non-mutating ID reads, call the unchanged RadioHead initialization once. The watchdog remains the bounded escape if that library call stalls.

## Options 1, 4, and 5: KISS Scope

### Option 1: Pi-first boot and explicit radio control

**Keep:**

- Pi boot independence from radio initialization.
- Real SDN control on pin 37.
- Existing channel-2 status/control path.
- Ready guards on every radio operation.
- Watchdog containment and fault telemetry.

**Simplify:**

- Use only `OFF` and `READY` on the Teensy.
- Let Pi/F Prime own desired state, the first enable, and capped autonomous recovery.
- Use the existing `CommsApp`/`CommsDriver_TeensyRfm23`; add no coordinator component.
- Use two RPC operations only.

**Defer:**

- Retained initialization-stage markers.
- Runtime board-revision detection.
- Predicted-contact-aware retry acceleration or a second recovery command.

### Option 4: Honest RSSI and link evidence

**Keep only:**

- `RssiValid`
- `LastAcceptedRxRssiDbm`
- `LastAcceptedRxAgeMs`
- Existing packet/error counters

Set RSSI valid only after a correctly addressed packet is accepted. Before that, GDS should display unknown rather than a fabricated default such as `-120 dBm`.

Use the existing end-to-end F Prime `PING` as contact proof. A local `PING_LINK_RSSI` statistics request is not an over-the-air ping and should be renamed/deprecated in favor of `RADIO_STATUS`.

Do not add boot acquisition, fresh/stale states, or new `DOWN/ACQUIRING/LOCKED/DEGRADED` derivations today.

### Option 5: Minimal LED behavior

- `READY`: retain the existing traffic flicker.
- `OFF`/unavailable: one slow, recognizable blink pattern.

Do not add a blink alphabet for every fault or initialization stage. Do not repurpose the Pi activity LED. Fault reason and watchdog history belong in channel-2/F Prime telemetry.

## Implemented Work Order and Remaining HIL

### Gate 0: Non-modifying hardware verification

For each physical node, regardless of its printed board label:

1. Verify Teensy pin 37 continuity to RFM23BP U3 pad 7.
2. Verify the installed R8/rail path so the direct 5 V jumper is not backfeeding `SW_3V3_2`.
3. Drive SDN high from the Teensy and measure approximately 3.3 V at the module input.
4. Confirm SDN high collapses radio branch current and makes SPI unavailable.
5. Confirm SDN low plus a 50 ms wait restores POR/default-register behavior.
6. Confirm pin 37 is never driven above 3.3 V, including Teensy reset/unpowered conditions.

This gate must pass before flashing this behavior onto the demo nodes and before calling the result HIL-reliable. Local implementation/emulation may proceed without making electrical claims. If the gate fails, software-only shutdown is not dependable and hardware deployment stops for a hardware decision.

### Work package A: Shared low-level radio control

- Add the SDN pin to the project-owned pin contract for both copied radio wrappers.
- Preserve byte-identical shared wrapper copies as required by repository validation.
- Implement one safe-off helper and one enable attempt.
- Replace redundant mutating preflight reset behavior with stable ID reads followed by one unchanged RadioHead init.
- Add ready guards to all RX, TX, availability, statistics, and recovery paths.

### Work package B: Satellite boot ordering and relay containment

- Apply safe radio outputs first.
- Assert `RPI_ENABLE` before any radio initialization.
- Bring up channel 2 without depending on radio readiness.
- Reject/count channel-0/1 traffic while `OFF`.
- Ensure an RF fault never deliberately toggles `RPI_ENABLE`.

### Work package C: Existing channel-2 protocol and F Prime policy

- Extend generated transport constants from `config/transport_constants.json`; do not hand-edit generated headers.
- Add `STATUS` and idempotent `SET_ENABLED` handling to RF target 2.
- Add one-pending-request, response validation, and timeout behavior to the existing comms driver.
- Refactor `CommsApp` into desired-enabled startup, slow status polling, and capped recovery policy before reconnecting it to a rate group.
- Add a deterministic channel-2 responder to local emulation. It starts `OFF`, keeps channel 2 alive, blocks/drops channel-0/1 RF traffic until enabled, and supports repeatable init-failure and watchdog-timeout injection.

### Work package D: Honest telemetry and one LED pattern

- Remove duplicated four-state link estimation from this path.
- Report readiness, fault reason, counters, and valid/aged last-accepted RSSI.
- Add the single unavailable blink without changing current traffic indication.

### Work package E: Verification

- Run shared-file drift and generated-constant checks.
- Build both Teensy workspaces.
- Run F Prime unit tests/build and repository local validation.
- Run nominal local emulation and prove the full demo cannot pass until F Prime enables the emulated radio.
- Run fault-injected local emulation and require F Prime `RadioRecoveryScheduled`/`RadioRecovered` evidence plus the expected enable-attempt count and minimum `30 s`/`120 s`/`900 s` gaps. An emulator-only `READY` marker is not sufficient proof.
- Complete the HIL gates below before calling the behavior reliable.

## Local Validation Results — 2026-07-16

The software-only gates passed on macOS:

- Generated transport-constant checks passed, and all three project-owned RFM23BP wrapper copies remained byte-identical.
- Satellite and ground Teensy firmware builds passed.
- The native F Prime deployment build passed. The focused `CommsApp` suite passed all 12 cases, the new `CommsDriver_TeensyRfm23` suite passed all 3 cases, and repository validation passed all 81 Python tests and all 7 CTest executables.
- Nominal process-level C3M emulation reached radio `READY` only after the F Prime enable request, then passed 3 consecutive capture/downlink/decode cycles. Each ground-decoded Lepton CSV exactly matched the real sample input. Evidence: `ArtemisRpiTeensy_N2/tools/logs/c3m_local_demo_20260716_091525/`.
- Returned-init-failure emulation reported `INIT_FAILED`/`TARGET_ERROR`, emitted `RadioRecoveryScheduled`, waited 30.067 seconds between enable attempts, emitted `RadioRecovered`, and passed the subsequent ground-data cycle. Evidence: `ArtemisRpiTeensy_N2/tools/logs/c3m_local_demo_20260716_091906/`.
- Watchdog/silent-response emulation timed out the guarded RPC, emitted `RadioRecoveryScheduled`, waited 44.186 seconds between attempts (approximately 15-second RPC timeout plus 30-second recovery delay), emitted `RadioRecovered`, and passed the subsequent ground-data cycle. Evidence: `ArtemisRpiTeensy_N2/tools/logs/c3m_local_demo_20260716_092052/`.
- The final post-refinement full gate again passed 3/3 consecutive capture/downlink/decode cycles with exact decoded Lepton data. Evidence: `ArtemisRpiTeensy_N2/tools/logs/c3m_local_demo_20260716_113338/`.
- `git diff --check` passed.

These results validate the state machine, wire contract, timeout/backoff policy, event evidence, and end-to-end software path. They do not validate Teensy pin levels, SDN electrical behavior, VCC/current behavior, SPI shutdown, RF initialization on a physical RFM23BP, or Pi survival through a Teensy watchdog reset. Those remain Gate 0 and bench/HIL work.

## Live HIL Results — 2026-07-16

### Bench and deployed state

- Static close-range bench, approximately 2–5 ft between fixed antennas, using the existing direct Teensy-USB 5 V jumper and unchanged 30 dBm RF configuration.
- Ground Teensy upload ID: `usb:100000`; satellite Teensy upload ID: `usb:2100000`.
- Ground channel-0 device: `/dev/cu.usbmodem115553301`; payload output: `/dev/cu.usbmodem115553305`; satellite debug: `/dev/cu.usbmodem115565001`.
- Raspberry Pi: `artemis-pi-c3m` / `raspberrypi-c3m`, `armv6l`, `192.168.0.234`.
- Radio-recovery baseline release: `/home/pi/artemis/releases/c3m-rf-recovery-20260716T213916Z-9e392fb3`.
- Radio-recovery baseline ARMv6 SHA-256: `9e392fb31ea7e4751a5e18898bf83f55b46f2ae0519de05b9b3b7db3b13dee84`.
- Ground dictionary SHA-256 remained `5a961fb5d301097cb0ad0dcd01d6ef2a27709f3156c5f7ed96084b0c4b54716d`.
- The active flight release was later superseded by the payload retry/camera
  guard release
  `/home/pi/artemis/releases/c3m-payload-retry-camera-20260716T224440Z-7c23d355`,
  SHA-256
  `7c23d3554e4d6abb0b5c81180190c1113c779fc1271b74ebfb7f687edf5d9e79`.
  The dictionary and proven radio lifecycle/recovery behavior were unchanged.

### Recovery and nominal command results

- F Prime PING passed before recovery testing and again after final deployment.
- `CommsApp.PING_LINK_RSSI` reported `READY`, no radio fault, valid accepted-packet RSSI, and a fresh age.
- Ten real `OFF` → `READY` cycles passed. Each cycle first verified `OFF` through the channel-2 status RPC, then allowed nominal F Prime startup policy to request `READY` again.
- The radio initialization-attempt counter increased from 1 to 11, as expected. There were no automatic systemd restarts and no observed Raspberry Pi restart.
- The final five cycles each returned to observed `READY` in approximately 3 seconds. Earlier 8–10 second measurements included SSH/test-harness overhead rather than radio initialization time.
- The measured `+1 dBm` RSSI is consistent with receiver saturation at 30 dBm and 2–5 ft. The RadioHead signed RSSI conversion and both project encode/decode paths were verified; no library, RF-setting, or wire-format change was made.

### Repeated capture/downlink results

Three new capture/downlink/decode cycles completed without restarting the Pi deployment, GDS, or payload receiver:

| Cycle | Science bytes | Transfer | Ground repair | Time | PING during transfer | Verified SHA-256 |
|---|---:|---:|---|---:|---|---|
| 1 | 38,480 | 1,100/1,100 | None | 65 s | PASS | `5d213a96cca5f44613cc5c1358c391b248d8e771b04cb27e40ba382b3dd3a453` |
| 2 | 38,480 | 1,100/1,100 | 5 missing segments recovered | 67 s | No response; immediate idle retry passed | `9c54edb6b357f3451bc91c174e18f8ab45cdff22303282cdf5562e4728fde713` |
| 3 | 38,480 | 1,100/1,100 | None | 65 s | PASS | `0255c11eea358f1abe83f96f43d45aa07a498cd555836bed76eba7c2fc8f6506` |

- All three transfers completed with CRC OK, decoded as 160×120 / 19,200-pixel images, and matched the corresponding Raspberry Pi data-product file byte for byte.
- Payload acceptance result: 3/3. Command-under-load result: 2/3 immediate responses; the missed command did not wedge the link, and its immediate idle retry passed.
- During this three-cycle window, the ground bridge added one TX drop, five retries, six ACK timeouts, and one reassembly drop. The satellite added no RF TX drops, RF terminal failures, recovery events, wrong-network/address/version frames, reassembly drops, or queue drops.
- Large absolute startup-era counters such as ground `down_q_drops=162`, ground USB backpressure/discards, and satellite `rf_tx_drops=530` were already present before this payload test and did not increase during the three-cycle window.
- At this deliberately saturated close range, the KISS operator rule is: wait for the expected event/response and resend a command once after the transfer or when the link is idle if no response arrives. Do not spam repeated commands.

Evidence directory: `ArtemisRpiTeensy_N2/data/c3m_hil_rf_hardening_20260716_110653/`.

### Defect found and fixed

The first live SOH request incorrectly reported `transport=FAIL` even though the radio was `READY`, channel 2 was responsive, and RF traffic was flowing. The legacy `TeensyTransportManager` treated a lack of recent satellite RF-RX packet-count growth as a failed transport. Healthy radio silence therefore became a false bus failure.

The conservative fix removed only the legacy manager's connection to `sohApp.statusIn[7]`. `CommsApp` remains the single authoritative owner of radio readiness, RPC status, recovery policy, and comms health; the legacy transport counters remain available as diagnostics. After native regeneration/build, Pi Zero W ARMv6 cross-compilation, remote executable smoke testing, and atomic deployment, SOH reported `comms=OK`, `transport=UNKNOWN`, and no false overall failure.

One bench-harness shell initially outlived its command runner and continued cycling after the expected result. It was detected, terminated, and the service was restored before further testing. The recovery test was rerun through one trap-protected remote session. This was test orchestration behavior, not a flight-software or radio failure.

The current debug USB streams are output-only, so `#RESET_COUNTERS` could not be injected through them. Results above use recorded before/after counter snapshots instead of claiming a reset.

### Terminal local TX fail-safe refinement

The final refinement closes the remaining factual local-wedge gap. If the
relay's one bounded FIFO recovery retry cannot complete/start a local transmit,
the satellite Teensy increments `rf_tx_terminal_failures`, invalidates stale
RSSI, records `LOCAL_TX_FAULT`, and asserts SDN to enter `OFF`. `CommsApp`
considers `READY` healthy only with fault `NONE`, so it observes this condition
and reuses the same one-attempt SDN/POR enable path. Peer silence and ordinary
ACK loss still do not reset the local radio.

Static contract tests, the new 3-case F Prime radio-driver suite, the expanded
12-case `CommsApp` suite, both Teensy builds, ARMv6KZ/VFPv2 verification, and the
full local C3M gate passed. After flashing satellite upload ID `usb:2100000`
and atomically deploying the final release above, a controlled channel-2 probe
received `OFF + NONE`; fresh F Prime startup then observed `OFF`, issued one
`SET_ENABLED`, and reached `READY + NONE` in the same second. Post-recovery RF
PING and `PING_LINK_RSSI` passed, with valid accepted-packet RSSI. The satellite
reported two initialization attempts and zero terminal local TX failures.

The final bench check also exposed a ground-process issue unrelated to the
radio: running local emulation while an older hardware GDS remained open reused
the global `/tmp/fprime-server-in` and `-out` IPC endpoints. The old GDS web UI
remained HTTP 200, but its command backend stopped writing ground USB channel 0.
A browser reload could not repair this; restarting the complete GDS process
tree restored UART uplink immediately. Run local validation before starting the
HIL GDS, or restart GDS after local emulation.

### HIL disposition

- **Software-observable close-range HIL: PASS.** Nominal command, real `OFF`/`READY` control, bounded F Prime restoration, repeated science downlink, data integrity, ARMv6 deployment, and truthful SOH behavior are demonstrated.
- **Strict full electrical/fault matrix: PENDING.** Do not claim the physical gates below without the operator and appropriate instruments.
- Still required: meter/scope proof of SDN voltage, pin-37/`RPI_ENABLE` continuity, current draw, VCC/backfeed behavior, brownout margins, and 5 V/3.3 V bounds; five true cold hardware power cycles per node; a safely induced physical initialization stall/watchdog case; and a physical mid-transfer Teensy reset.
- No persistent public ground command was added that could accidentally leave the mission-critical radio disabled.

## Acceptance Gates

### Software behavior

- Repeated `SET_ENABLED(false)` is harmless and leaves `SDN` high.
- Repeated `SET_ENABLED(true)` while healthy `READY + NONE` is harmless and does not reset the radio.
- No radio SPI call occurs while `OFF`.
- Only one radio RPC may be pending, and mismatched/stale responses are rejected.
- A missing enable response times out after the Teensy watchdog window and schedules exactly one later attempt at the configured backoff deadline.
- Persistent local failure continues at the capped `900 s` cadence; it never becomes permanent abandonment of the critical radio service.
- Fault-injected end-to-end emulation records both F Prime recovery events and non-overlapping attempt timing; the demo must not pass merely because the emulator opened RF.
- Repeated low-level disable requests remain idempotently `OFF`; nominal F Prime startup policy still restores desired-enabled service.
- RSSI is invalid before an accepted addressed packet and carries an age afterward.
- Existing RF and transport settings remain byte-for-byte unchanged unless separately approved.

### Bench/HIL behavior

- On 5 cold boots per node, the Pi/F Prime process boots regardless of immediate radio success.
- On nominal boots, the one Pi-requested enable reaches `READY` and existing F Prime PING works.
- On 10 `OFF`/`ON` cycles per node, shutdown and POR behavior are repeatable.
- Commanded `OFF` stops RF activity, leaves channel 2 alive, and causes no subsequent radio SPI activity.
- A safely induced init failure/hang causes at most one Teensy watchdog reset; the Pi remains alive and the Teensy returns `OFF` with a fault report.
- Across 3 safely induced init-failure/watchdog cases, F Prime reports the fault and reissues only at the bounded deadlines; a recovered attempt reaches `READY` without operator intervention.
- One local/bench disable remains verifiably `OFF`, and a later local enable or F Prime restart resumes normal recovery policy.
- One mid-transfer Teensy reset drops the in-flight transfer safely, preserves the Pi process, and recovers the radio without replaying stale channel-0/1 frames.
- Scope `RPI_ENABLE`, SDN, and VCC across cold boot and Teensy watchdog reset. Verify the Pi does not brown out or restart.
- Existing ground traffic decoding, F Prime PING, and at least three nominal payload/capture/downlink cycles still pass.
- At 30 dBm, verify the current MVP supply remains stable and does not backfeed the v4.24 switched 3.3 V rail.

## Stop Rules

Stop hardware deployment/HIL and escalate the hardware issue if any of these are true:

- `SDN = HIGH` does not reliably shut the module down from the Teensy's 3.3 V output.
- The direct 5 V jumper backfeeds the v4.24 `SW_3V3_2` rail through populated R8.
- A Teensy watchdog reset drops `RPI_ENABLE` long enough to reset or brown out the Pi.
- The board cannot prevent a 5 V level from reaching Teensy pin 37.

The software can improve startup ordering and functional shutdown, but it cannot solve those electrical conditions.

## Explicitly Out of Scope

- RadioHead library changes or vendoring
- RF frequency, bitrate, deviation, power, packet format, addressing, ACK, retry, or payload changes
- Physical removal of the existing 5 V jumper
- New load switch, PDU rail, level shifter, or PCB rework
- Runtime v4.23/v4.24 detection
- New F Prime boot coordinator or broad topology redesign
- A persistent public ground command that can leave the only radio disabled
- Persistent init-stage storage
- Fast, unbounded, overlapping, or Teensy-owned retry loops
- Pi activity-LED control or multiple Teensy fault blink codes
- New custom GDS dashboard/indicator work
- Separate ground-receiver RSSI transport
- Broad ground Teensy control changes beyond shared-library compatibility and regression testing

## Likely Files Touched Later

This is a planning inventory, not authorization to edit them:

- `config/transport_constants.json` and regenerated transport headers
- Satellite and ground copies of `firmware/libs/rf23bp/artemis_rf23bp.hpp`
- Satellite `rf23_driver.*`, `relay_uart_rf.*`, `local_teensy_router.*`, and main sketch
- Existing F Prime `CommsDriver_TeensyRfm23` and `CommsApp` files/topology scheduling
- Local-emulation channel-2 handling
- Focused Teensy/F Prime tests and RF contract documentation

## Definition of Done

The work is done when the Pi always boots independently, software can repeatedly place the RFM23BP in verified SDN shutdown and re-enable it, a stalled initialization cannot create a fast reset loop, F Prime reports degraded comms honestly and keeps attempting bounded recovery for this critical service, only `OFF`/`READY` exist as Teensy radio states, local fault emulation proves the policy, and the proven nominal RF/downlink path still passes unchanged in HIL.
