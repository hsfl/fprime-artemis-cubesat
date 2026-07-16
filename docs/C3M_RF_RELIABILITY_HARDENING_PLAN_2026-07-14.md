# C3M RFM23BP MVP Reliability Plan — 2026-07-14

## BLUF

This is **MVP hardening**, not a flight-protocol redesign. The demo uses
RFM23BP radios to prove that F Prime and the C3M spacecraft bus can repeatedly:

1. boot and show basic SOH telemetry;
2. capture one Lepton image;
3. store it as a new science product;
4. downlink it over the existing N2 channel-1 protocol;
5. reconstruct, CRC-check, save, and display the ground-received image; and
6. return to ready so the operator can repeat the cycle until both systems are
   powered off.

The nominal indoor demo already works. The remaining goal is predictable
behavior when RF, USB, a bridge, or a ground process becomes messy. Keep the
current F Prime App-Man-Drv architecture and current N2 wire format. Add only
the bounded recovery, transfer isolation, and honest failure behavior needed
for a reliable proof-of-concept demonstration.

This file is the active monitoring source of truth. The executable protocol
contract is [C3M MVP Downlink Protocol](C3M_MVP_DOWNLINK_PROTOCOL.md).

## Demo Intent And Operator Story

```text
one-time startup
  -> satellite boot / READY
  -> basic F Prime and bus SOH visible in GDS

repeat while both sides remain powered
  -> schedule or immediately request one capture
  -> wait for a NEW stored product
  -> request downlink of that product
  -> ground receiver reconstructs and verifies CRC
  -> GUI saves and displays that ground copy
  -> flight and ground return to READY

shutdown only when the operator is finished
```

Success is not one lucky photo. A judge/operator must be able to request
multiple photographs and downlinks in one powered session without restarting
F Prime, GDS, the payload receiver, or either Teensy between cycles.

Each cycle is sequential and isolated:

- one capture creates a fresh product ID/path;
- one active downlink owns one immutable stored product;
- one ground run directory owns that transfer's complete or partial state;
- duplicate requests do not reset active progress;
- stale retry/partial state never contaminates a later picture;
- a failed/abandoned cycle returns cleanly enough for the next cycle.

## Simplified Demo Command Flow

Use the smallest command story that still demonstrates F Prime component and
bus coordination.

### One-time startup

1. Wait for boot/base readiness and basic SOH.
2. Optionally request one explicit SOH snapshot.
3. Leave the Lepton and storage plumbing under the existing component path.

### Per-picture loop

1. Use the existing scheduled collection command with a short delay (for
   example, five seconds) when the schedule behavior is useful to show.
   Immediate `START_COLLECTION` is also valid for engineering repetition.
2. Wait for a **new** `ScienceStored`/product identity rather than a fixed sleep.
3. Request science downlink for that new product.
4. Wait for receiver `COMPLETE` with valid whole-product CRC, or an honest
   `PARTIAL`/`ERROR` outcome.
5. Confirm both sides are ready, then repeat.

Do not require separate operator steps for Lepton enable, capture duration, or
storage-history reports when the current capture path already opens the camera,
takes one frame, and forwards the new product. Keep those interfaces for
compatibility; simplify the script instead of refactoring shared FPP solely for
the demo.

## Scope Guardrails

### In scope now

- Existing N2 `HEADER`, indexed `DATA`, `END`, and `RETRY_REQUEST` behavior.
- Simple half-duplex rhythm: bounded payload burst, ground quiet/retry window,
  repair burst, repeated `END`.
- Bounded RF transmit calls and recovery below the 12-second watchdog.
- Honest zero/partial USB-write handling and independent channel queues.
- Ground receiver persistence, serial re-enumeration, restart, and partial
  artifacts.
- Duplicate/conflicting start protection and additive missing-packet repair.
- Multiple sequential capture/downlink cycles without process restart.
- Sparse channel-0 lifecycle/fault telemetry during bulk payload.
- Local-emulation proof followed by practical indoor HIL proof.

### Deferred unless a reproduced MVP failure requires it

- New `C2` magic or an incompatible protocol decoder.
- Boot epochs and flight restart-resume checkpoints.
- CRC-32 confirmation, final ACK, and ACK tombstones.
- Request/confirmation/lease IDs and phase-control packets.
- Formal distributed half-duplex ownership leases.
- Spaceflight retention, expiry, and clock-domain rules.
- Radio replacement, SatNOGS/SDR integration, or Neutron 2 flight CONOPS.
- RF PHY/bitrate/power changes before comparable HIL evidence exists.

The former C3M-DL v2 proposal was appropriate design exploration but too large
for this demo. Its mission-grade details remain in Git history at `87eca99` and
`72fcb24`; they are not acceptance requirements.

## Required Invariants

1. Channel `0` remains the bounded command/event/telemetry control plane.
2. Channel `1` remains the N2 indexed bulk-payload path with selective repair.
3. Channel `2` remains satellite-local RPC and never crosses RF.
4. Ground GDS USB carries CCSDS only; bridge diagnostics use the debug USB
   interface.
5. A bridge queue entry is removed only after complete local acceptance or an
   explicit counted discard.
6. RF/USB work returns before the Teensy watchdog deadline.
7. Flight local `END` acceptance is not ground CRC completion.
8. Ground reports `COMPLETE` only after reconstructing every indexed packet and
   validating the whole-product CRC.
9. Partial products retain byte positions, missing-packet metadata, and an
   explicit reason; they never masquerade as complete.
10. One transfer's packets or retry state cannot mutate another transfer.
11. After complete, partial, error, or operator abort, the system can accept a
   later capture/downlink without process or hardware restart.
12. Hardware uploads use verified Teensy `usb:*` identity.

## Current Status

| Area | Local status | HIL status |
| --- | --- | --- |
| Nominal single capture/downlink/decode | Passed | Passed before and after the bridge fix; current post-fix product/transfer 1 was 38,480 bytes, 1,100/1,100, exact source/ground SHA-256, 65.1 s |
| Bounded Teensy RF TX timeout/retry/recovery | Passed host tests and builds | Focused injected policy passed; direct physical `waitPacketSent()` fault injection is N/A without an MVP-only firmware hook |
| Honest ground USB writes and independent queues | Passed host tests and build | Passed six-second channel-0 reader stop and full ground USB reconnect; backpressure/recovery remained honest, GDS reconnected, and PING returned |
| Persistent/re-enumerating ground receiver | Passed focused tests | Passed process restart from 253/1,100 and physical ground USB reconnect from 423/1,100; both completed by repair with CRC-valid files |
| Duplicate start and bounded F Prime payload work | Passed component tests | Passed on target: duplicate preserved active product/progress, one start event, CRC complete, no F Prime restart |
| Additive N2 repair | Passed component tests | Passed one natural one-round repair in the 3/3 nominal run; focused fade remains |
| Automatic progress-event removal | Passed component/full local validation | Passed observation: zero automatic progress events; five PING responses delivered during four bulk transfers |
| Ground-side local reconstruction | Passed: real receiver PTY/CRC/decode path | Passed physical channel-1 proof with four exact source/ground files and complete decode artifacts |
| Repeated capture/downlink cycles | Passed: three cycles, one uninterrupted session | Passed: three new products/transfers, 3/3 CRC-valid, 64.7-65.2 s, no process/hardware restart |
| Deterministic packet-loss repair | Passed: dropped DATA 100, retry/repair/CRC | Needs brief-RF-fade proof |
| Permanent loss then clean next cycle | Passed: honest 1,099/1,100 partial, next transfer exact | Local proof retained; a sustained physical fade is not a separate current MVP matrix gate |
| Outdoor/Yagi behavior | Not locally provable | Deferred until bench passes |

## What Existing Evidence Proved

### Nominal baseline

The July 14 indoor run completed a real `38,480`-byte Lepton product:

- `1,100/1,100` N2 packets;
- `58.336` seconds;
- expected/actual CRC `33720`;
- matching Pi/ground SHA-256;
- decoded `160x120`, `19,200` pixels;
- no unexplained F Prime or Teensy restart during the clean run.

### Failures that justify MVP hardening

- Outdoor/Yagi pointing loss interrupted a transfer and produced many missing
  packets.
- Channel 0 wedged while USB remained enumerated; old bridge counters falsely
  implied complete host writes.
- USB unplug/re-enumeration terminated the old receiver and lost live state.
- A duplicate downlink start reset an active transfer; timing/queue pressure
  later asserted F Prime and restarted the service.
- Payload traffic produced rate-group cycle-slip warnings.

These failures justify bounded RF work, honest USB writes, persistent receiver
state, duplicate protection, and simple burst/quiet repair. They do not justify
an entirely new flight protocol for a different future radio.

## Implementation Work Plan

### A. Keep the documentation and command path MVP-sized

- [x] Retain N2 and remove C2/v2 implementation from the active scope.
- [x] Define repeated sequential capture/store/downlink/view cycles.
- [x] Keep scheduled collection as an optional visible demo step.
- [x] Simplify the demo script to wait on new-product and receiver terminal
  state instead of fixed sleeps and unnecessary commands.
- [x] Keep immediate capture as an engineering shortcut without changing the
  shared capture-duration interfaces.

### B. Make local emulation prove the actual ground copy

- [x] Feed emulated channel-1 bytes into the real payload receiver path instead
  of only counting them.
- [x] Feed receiver N2 retry requests back through the emulated ground/satellite
  path.
- [x] Make the demo decode the receiver-reconstructed artifact, never the
  satellite/source `.fdp` as proof.
- [x] Require ground CRC success before a nominal local demo passes.
- [x] Keep one receiver session alive across several transfers and preserve
  separate complete/partial run history.

### C. Prove repeated-cycle isolation

- [x] Run at least three capture/downlink cycles without restarting any process.
- [x] Assert three unique stored products and three unique ground artifacts.
- [x] Inject one deterministic DATA-packet loss and complete through the actual
  bidirectional N2 retry path. The injection ran as a focused transfer rather
  than the middle of the nominal three-cycle evidence run.
- [x] Restart the receiver during a full emulated transfer, reload its
  checkpoint, repair packets lost during the handoff, and complete CRC-valid.
- [x] Permanently lose one packet, save an honest positional partial with its
  missing map, then complete a fresh capture/downlink in the same session.
- [x] Reject stale/conflicting packets in focused tests and prove three nominal
  transfers do not mix products.
- [x] Exercise transfer-ID progression within a bounded session; do not claim
  safe wrap or flight-restart resume.

### D. Keep N2 reliability changes small

- [x] Bound payload work to 18 frames per invocation.
- [x] Make duplicate active descriptors idempotent and conflicts busy.
- [x] Merge overlapping retry work without replacing unfinished repair.
- [x] Advance cursors only after local UART acceptance.
- [x] Repeat `END` after each repair round/burst.
- [x] Send ground retry only after `END`/receive quiet; enforce a simple
  turnaround quiet time in receiver behavior.
- [x] Strictly validate retry bitmap length and index bounds.
- [x] Bound receiver repair time with inactivity/absolute deadlines, then save
  partial/error state and return ready for a new transfer.
- [ ] Batch receiver checkpoints enough to avoid avoidable disk-sync slowdown
  while retaining restart value.

### E. Keep channel 0 useful and quiet

- [x] Remove automatic ten-percent progress and repeated completion-summary
  events.
- [x] Retain progress telemetry and explicit `GET_PAYLOAD_STATUS` fallback.
- [ ] During bulk downlink emit only start, slow status heartbeat, terminal
  lifecycle, commands/responses, warnings/errors, resets, and link faults.
- [ ] Restore normal telemetry behavior on every terminal path and restart.
- [ ] Measure command latency and delivered payload goodput in HIL.

Do not dynamically retime `rateGroup1`. It drives more than telemetry and the
checked-out `Svc::RateGroupDriver` uses initialization-time divisors. Emit less
routine telemetry instead of destabilizing system scheduling. A future
`Svc::TlmPacketizer` migration is optional and not needed for this MVP.

### F. Preserve already implemented bridge/receiver hardening

- [x] Finite RF TX-completion timeout below watchdog deadline.
- [x] One bounded retry and radio/FIFO recovery.
- [x] Retain queued RF/USB work until accepted or explicitly discarded.
- [x] Track actual USB bytes and retain zero/partial-write suffixes.
- [x] Use independent channel-0/channel-1 ground USB queues.
- [x] Persist receiver identity, packet bitmap, bytes, and partial outcomes.
- [x] Rediscover a re-enumerated port by stable identity.
- [ ] Verify the satellite `RPI_ENABLE_PIN` startup behavior before flashing.

## Local Acceptance Gate Before HIL

All local tests run without claiming physical-radio proof:

| Local case | Required result |
| --- | --- |
| One nominal cycle | Receiver-generated ground artifact has exact CRC/content and displays |
| Three sequential cycles | Three unique products/artifacts; no process restart or mixed state |
| Focused recoverable loss | Retry repairs missing indexes; repeated END; valid final CRC |
| Duplicate active request | Idempotent; active cursor does not reset |
| Conflicting active request | Busy/rejected; active product remains unchanged |
| Receiver process restart | Same transfer reloads and completes or stays honestly partial |
| Permanent loss/abandon | Position-preserving partial/error; next fresh cycle completes |
| Full regression | Component tests, Python/emulation tests, Teensy builds, native build, and exact ground decode pass |

Local proof covers deterministic software behavior. It does **not** prove
RFM23BP turnaround, USB device recovery, watchdog behavior, real antenna
geometry, Pi scheduling, or measured goodput.

## Today's HIL Execution

1. Use basic antennas, keep the Mac awake with the lid open, and connect the
   ground Teensy before launching GDS.
2. Record all enumerated serial devices and positively identify the ground
   Teensy's channel 0, channel 1, and debug interfaces. Do not upload by a
   guessed `/dev/cu.usbmodem*` order.
3. Build/flash the ground and satellite Teensys using verified `usb:*`
   identities, then record firmware hashes from the actual uploaded artifacts.
4. Connect/boot the satellite and wait about 90 seconds before SSH/service
   inspection. Verify the Pi boot ID, service PID/restart count, binary hash,
   `/dev/serial0`, and both bridge debug counter streams.
5. Run HIL-MVP-1 startup, HIL-MVP-2a one complete cycle, then HIL-MVP-2b
   three-cycle nominal. Do not begin induced fades, process restarts, or USB
   reconnects until all three pass.
6. Continue HIL-MVP-3 through HIL-MVP-8 one fault at a time. After every
   induced fault, require an honest terminal result and a clean next capture.

## Practical Indoor HIL Matrix

Use basic antennas on the bench. Keep the Mac awake and lid open. Verify live
hardware identities before every flash; do not assume July 14 device paths.

| HIL case | Action | Pass condition | 2026-07-15 result |
| --- | --- | --- | --- |
| HIL-MVP-1 startup | Boot both sides, start GDS/receiver, request SOH/PING | Correct ports, SOH visible, no reset/restart, counters captured | **PASS** — PING 4245, SOH snapshot, direct UVC frame, PID 706 / zero restarts |
| HIL-MVP-2a single nominal | Capture/downlink/display one new photo | Exact CRC/hash ground copy; 160x120 decode; all sides return ready | **PASS** — product/transfer 1, 1,100/1,100, 64.8 s, SHA-256 `d166dee7...e214` |
| HIL-MVP-2b repeated nominal | Capture/downlink/display three new photos sequentially | Three unique CRC-valid ground images; all sides remain running/ready | **PASS** — products/transfers 2-4, 3/3 exact files, 64.7-65.2 s, PID unchanged |
| HIL-MVP-3 duplicate command | Repeat active downlink request once | No progress reset, assertion, or F Prime restart | **PASS** — duplicate product 5 request preserved the active transfer; one start event; 1,100/1,100 and CRC complete; PID unchanged |
| HIL-MVP-4 brief RF fade | Create far-field attenuation with distance/off-axis antenna geometry, then restore | Missing packets requested/repaired or honest partial; next cycle succeeds | **PASS** — with both normal monopoles attached, the portable GDS was carried around the eighth floor while the satellite remained battery-powered in the lab. Product/transfer 4 reached a 24-packet deficit, recovered all packets in four selective-repair rounds, and completed exact in 70.3 s; PING 39013 returned. The restored inside-lab product/transfer 5 then completed exact with zero repairs in 64.6 s and PING 39014. The separate aluminum-near-antenna incident remains invalid hardware-stress evidence and was not used for qualification |
| HIL-MVP-5 ground receiver restart | Restart only payload receiver during transfer | State reloads and completes or remains honest partial; following cycle succeeds | **PASS** — replacement receiver resumed transfer 6 at 253/1,100; one repair round; CRC complete in 68.7 s; PID unchanged |
| HIL-MVP-6 channel-0 backpressure | Stop/restart only GDS reader | Short write/backpressure counted; no false delivery or watchdog reset; PING recovers | **PASS** — six-second reader stop increased backpressure, then recovery count; no new discards/queue drops; PING 37606 returned; PID unchanged |
| HIL-MVP-7 ground USB reconnect | Unplug/replug ground Teensy once | Ports rediscovered; no silent complete; current attempt resolves honestly; next cycle succeeds | **PASS** — all ground ports disappeared at 423/1,100; receiver entered `recovering`; ports, receiver, and GDS auto-reconnected; transfer completed after two repairs; PING 39008 and a zero-repair next cycle passed |
| HIL-MVP-8 RF TX timeout injection | Exercise the bounded `waitPacketSent` failure path with a focused host-injected test; optionally remove the peer only to exercise ACK retry | Bounded retry/recovery; no 12-second watchdog reset; do not mislabel no-peer ACK loss as a TX-completion timeout | **PASS focused injection / N/A physical** — both bridges use a 500 ms completion timeout and one recovered retry; injected timeout/terminal cases pass; no MVP-only hardware hook added |

### Post-HIL Follow-Up Backlog — Plan Only, Not Implemented

Preserve the currently proven firmware and F Prime build for the outdoor/Yagi
campaign. The following tasks capture findings from the July 15 HIL session
and the subsequent source investigation. They are intentionally deferred until
after range testing so the outdoor comparison is not contaminated by an
untested boot, firmware, or UI change.

#### Follow-up 1 — Pi-First Boot and Bounded Local Radio Recovery

**Context:** The aluminum incident was not a valid RF-fade test, but it exposed
a persistent local satellite RFM23BP TX-completion wedge. Static inspection also
found that the satellite Teensy currently holds `RPI_ENABLE_PIN` low until
`g_rfDriver.begin()` returns. The wrapper performs a mutating software-reset
probe before RadioHead performs additional resets and enters an unbounded
chip-ready wait. A bad radio initialization can therefore watchdog-loop before
the Pi or F Prime boots. The existing channel `2` RPC is Pi-to-Teensy local and
already carries RF statistics, but it has no radio-init or recovery operation.

Plan:

1. Assert Pi power and initialize the Pi UART/channel `2` service path before
   any potentially blocking radio initialization. Verify in HIL that a Teensy
   reset does not pulse `RPI_ENABLE_PIN` long enough to brown out the Pi.
2. Put the RFM23BP into a safe startup condition using the real active-high SDN
   pin. Do not treat the board net named `RADIO_RESET` as a reset input; it is
   the radio GPIO0/POR output and must not be driven as a reset.
3. Start the Teensy radio state as `UNINITIALIZED`, keep channel `2` available,
   and let a bounded onboard F Prime boot coordinator request `RADIO_STATUS`
   followed by one `RADIO_INIT`. A ground command cannot be the recovery
   dependency because a wedged RF link cannot deliver that command.
4. Replace the mutating preflight with stable, non-mutating device-type reads,
   then call the existing RadioHead `radio.init()` path once. Do not patch the
   vendored RadioHead or hardware-definition files for the MVP follow-up.
5. Add a radio-ready guard to `available`, `recv`, `send`, and `linkStats` so a
   failed or incomplete initialization cannot continue touching the radio.
6. Record an init-in-progress marker before the potentially blocking call. If
   the hardware watchdog resets during that stage, boot with the Pi/channel `2`
   path alive, report `DEGRADED`, and do not automatically repeat the same
   initialization loop.
7. Add bounded channel `2` operations for status and explicit reinitialize;
   optionally include one SDN cycle. Permit one deliberate recovery attempt,
   then remain degraded until the next operator-approved attempt or power
   cycle.
8. Report ready state, exact init stage, watchdog/init-failure reason, attempt
   count, and radio recovery result through counters, F Prime events, and
   telemetry once communications are available.

Acceptance:

- the Pi and F Prime boot even when radio initialization fails or hangs;
- a watchdog-reset init attempt cannot create an endless automatic loop;
- channel `2` returns an honest ready/degraded state after Teensy recovery;
- nominal capture/downlink behavior and packet-repair semantics are unchanged;
- focused local/channel-2 tests, both Teensy builds, the F Prime test/build
  gates, a 10-20-cycle cold-boot HIL soak, and one safe recovery HIL case pass.

Do not simply reconnect `commsApp.run` at its current full rate: it polls and
emits telemetry every tick and was intentionally removed from the RF MVP rate
group. Use a boot-only or otherwise explicitly paced coordinator.

#### Follow-up 2 — Reconcile Receiver Status After USB Reconnect

**Status (2026-07-16): Implemented and passed local regression/emulation.**

**Context:** After the powered-off ground antenna swap and USB reconnect, the
payload receiver UI temporarily labeled completed product/transfer 5 as
`receiving` while still displaying `1,100/1,100`, zero missing packets, and
`crc_ok=true`. The receiver currently treats any retained non-null transfer ID
as an active transfer when it handles a new `ready` event. This is stale UI
state, not RF corruption.

Plan:

1. Determine active transfer state from terminal/incomplete evidence rather
   than `transfer_id != null` alone.
2. On reconnect, preserve `complete`, `partial`, or `failed` for a terminal run
   while separately setting the serial connection state to connected/ready.
3. Change to `receiving` only for a genuinely incomplete checkpoint or a new
   transfer-start/resume event.
4. Add a regression covering the observed sequence: exact completion, USB
   reconnect, `ready`, terminal label retained, then a new header changes the
   UI to receiving.

Acceptance: a reconnect never presents a CRC-complete transfer as actively
receiving, and the next fresh product still replaces the prior presentation
without restarting the UI.

Result: `ready` now derives the display state from durable run evidence instead
of a retained transfer ID. Complete, partial, and failed runs remain terminal;
only a genuinely incomplete checkpoint resumes as receiving. Focused tests
cover all terminal states, incomplete resume, and fresh-transfer replacement.
The receiver-restart local emulation completed capture, checkpoint recovery,
CRC verification, decode, and exact CSV comparison.

#### Follow-up 3 — Temperature Hover Overlay for Archived History Images

**Status (2026-07-16): Implemented and passed local browser verification.**

**Context:** The live thermal preview already loads its CSV and reports the
pixel temperature under the pointer. The selected history detail currently
shows only the archived PNG even though its archived CSV URL is already
available.

Plan:

1. Reuse one thermal-grid loader and coordinate-mapping helper for the live
   preview and the large selected history image.
2. Show an overlay tooltip on the history image with temperature and row/column;
   show `No data` for missing/white pixels.
3. Keep the small history-list thumbnails passive; apply inspection only to the
   selected full image.
4. Preserve correct coordinate mapping when the image is responsively scaled.

Acceptance: hovering a selected archived image displays the value from that
run's CSV at the correct pixel without affecting history selection or mobile
layout.

Result: the live and archived full-size previews now share one configurable
CSV/grid coordinate helper. Only the selected archived image is interactive;
the history thumbnails remain passive. Browser verification against a retained
HIL artifact displayed `Column 79, row 59 · 19.54°C` as an overlay on the image
with no browser warnings or errors.

#### Follow-up 4 — Boot Link Acquisition and GDS RSSI Visibility

**Context:** The satellite channel `2` RF-statistics response and F Prime comms
driver already carry `last_rssi_dbm`, but the MVP does not provide a clear,
fresh, operator-facing link-strength indication. An RSSI register read before a
real received packet is normally a noise-floor/instantaneous measurement, not
proof of end-to-end link strength, so the UI must not present it as a valid
link sample.

Plan:

1. After the Pi-first radio initialization reaches `READY`, start a bounded
   boot link-acquisition step and request the local channel `2` RF status.
2. Treat RSSI as `UNKNOWN` until the satellite radio receives a valid packet.
   After a successful PING/status exchange, publish the satellite's most recent
   receive RSSI, validity, and sample age through F Prime telemetry.
3. Expose a plainly labeled GDS indication such as `Satellite RX RSSI (uplink)`
   with `UNKNOWN`, `FRESH`, and `STALE` semantics. Do not imply that this is the
   ground receiver's downlink RSSI.
4. Retain counters and PING success as the primary link-health evidence; RSSI
   supplements them and does not replace packet/repair/error evidence.
5. If ground-side downlink RSSI is later required, plan a separate ground-bridge
   metadata path rather than silently mixing it with satellite receive RSSI.

Acceptance: a normal boot produces either an honest unknown/no-packet state or
a timestamped RSSI from a verified received packet, and GDS never displays a
stale/default value as current link strength.

#### Follow-up 5 — Visible Teensy/F Prime Fault LED Patterns

**Context:** The OBC exposes the Teensy LED and the Raspberry Pi activity LED,
which are useful during field work when serial logs or SSH are not immediately
visible. Normal radio traffic already influences the Teensy LED, but there is
no distinct visual indication for terminal bridge errors, impending
software-requested resets, or a previous watchdog reset.

Plan:

1. Define and document a small non-conflicting LED contract: retain normal
   activity behavior, use a rapid Teensy blink for a detected terminal/fatal
   bridge error, and use a recognizable diagnostic burst after boot when the
   watchdog-reset flag is present.
2. Before an intentional software-requested watchdog reset, blink rapidly for
   a short bounded grace period and record the reset reason. Do not delay a
   safety-critical reset indefinitely for LED presentation.
3. Be explicit that firmware blocked inside an unbounded call cannot reliably
   execute a pre-reset blink. For that case, use the post-watchdog boot pattern
   and retained reset/init-stage reason.
4. Inspect current OBC wiring and Linux ownership before assigning the Pi
   activity LED. Prefer using it only as a Pi/F Prime alive indicator; do not
   disable storage/activity behavior or repurpose an unavailable LED blindly.
5. Document the final patterns in the operator runbook so a field operator can
   distinguish normal RF traffic, Pi/F Prime alive, detected fatal error, and
   previous watchdog recovery without opening a serial console.

Acceptance: focused tests or controlled HIL injection demonstrate the normal,
detected-fatal, intentional-reset, and post-watchdog patterns; the Pi stays
powered during a radio-only fault; and the patterns do not interfere with RF
timing, watchdog servicing, or normal activity indication.

These are planned tasks only. Do not implement them during the current outdoor
range campaign. After range evidence is captured, implement and validate them
as small, independently reviewable changes rather than one combined refactor.

For every case record:

- branch/HEAD and submodule SHA;
- exact ground/satellite `usb:*` identities and firmware hashes;
- Pi binary hash, boot ID, service PID, and restart count;
- GDS, payload receiver, and both bridge debug logs;
- product/transfer identity, source and ground hashes/CRC;
- duration, missing count, repair rounds, queue/USB/RF counters;
- explicit `PASS`, `RECOVERED`, `PARTIAL`, `ERROR`, or `ABORTED` result.

Stop the matrix if the failure signature changes unexpectedly. Outdoor/Yagi
testing starts only after the basic-antenna repeated-cycle and focused recovery
cases pass.

## Verified Live Artifacts

Verified on the live basic-antenna bench on 2026-07-15:

- Branch/HEAD after the message-ID fix:
  `codex/c3m-rf-reliability-hardening` at
  `bdca6a31a314099be5e0161e3d14b276a278675f`.
- Ground Teensy: `usb:100000`, triple serial `11555330`; current per-channel-ID
  HEX SHA-256
  `ff5147205ca969072dff64e4b5e089c74481b2bcc4e649189e3d2eb9780fbd26`.
- Satellite Teensy: `usb:2100000`, serial `11556500`; current per-channel-ID
  HEX SHA-256
  `fc637b46591bbf4a236bd8b5730c14b411017805a72e93c08bc022ab94c1fc27`.
- C3M Pi after the bridge reflash/reboot epoch: boot ID
  `aa5c81f5-9a1c-4c8f-a5d5-324e1c256bfc`, service PID `256`, restart count
  `0`, `/dev/serial0 -> /dev/ttyS0`. The earlier nominal and focused-fault
  cases ran under boot ID `e85f55e7-f734-4d30-be91-b6c41b440ef1`, PID `706`,
  restart count `0`.
- ARMv6 Pi binary SHA-256:
  `e0176a21b21b40f5b4e0fba469f2d643c6dd9194de4963267867e86cc8ff814b`.
- GDS dictionary SHA-256:
  `e39e0c48023d180016ff17ca11011b14d36ef7e7c75b97e23134007a99fd508f`.
- Post-message-ID-fix local suite: 68 Python/emulation/bridge tests, fresh native
  build, 6/6 F Prime component suites, both Teensy builds, two independent
  three-cycle exact ground-copy runs, deterministic loss/repair, and the
  ARMv6KZ/VFPv2 cross-build passed.

Recompute hashes and rerun the complete local gate after any implementation.
Re-query live USB identities before every future flash.

## Evidence Log

| Date/time (HST) | Work | Result | Evidence/remaining gate |
| --- | --- | --- | --- |
| 2026-07-14 16:13 | Original indoor nominal | PASS HIL | Real product `1,100/1,100`, CRC/hash match, 58.336 s, no restart |
| 2026-07-14 16:29 | Ground unplug | PARTIAL | Failure retained; exposed USB and receiver recovery gap |
| 2026-07-14 16:31 | Duplicate start | FAIL reproduced | Active transfer reset; queue assertion and F Prime service restart |
| 2026-07-14 16:43 | USB write state machine | PASS local | `d6c14ed`; focused tests/build; physical proof open |
| 2026-07-14 19:10 | RF bounded retry | PASS local | `c62d0af`; timeout/recovery policy tests and both Teensy builds |
| 2026-07-14 19:30 | Persistent receiver | PASS local | `96d1e55`; restart/re-enumeration/partial tests |
| 2026-07-14 20:02 | Bounded N2 transfer | PASS local | `a1133a7`, `382ea51`, `9b163b2`; focused/full native and ARMv6 validation |
| 2026-07-14 20:28 | Remove progress events | PASS local | `d195d19`; explicit status fallback retained; full local validation |
| 2026-07-14 | Protocol rescope | ACTIVE | N2 retained; mission-grade v2 mechanisms deferred; repeated-cycle MVP is current target |
| 2026-07-14 21:01 | Three-cycle ground-copy proof | PASS local | `tools/logs/c3m_local_demo_20260714_205808`; products/transfers 1-3, three 38,480-byte/1,100-packet receiver files, exact source/ground SHA-256 pairs, 160x120 decode |
| 2026-07-14 21:05 | Deterministic DATA loss and N2 repair | PASS local | `tools/logs/c3m_local_demo_20260714_210435`; dropped index 100 once, receiver retry `start=100 count=1`, repair completed, CRC/content/hash/decode passed |
| 2026-07-14 21:10 | Complete post-integration local gate | PASS local | 67 Python/bridge tests; fresh native build; 6/6 F Prime suites; `tools/logs/c3m_local_demo_20260714_210708` three-cycle ground-copy proof; both Teensy builds passed |
| 2026-07-14 21:11 | Pi Zero W target build | PASS local | ARMv6KZ, VFPv2, `/lib/ld-linux-armhf.so.3`; binary SHA-256 `e0176a21...ff814b`; subsequently deployed and verified in the July 15 HIL campaign |
| 2026-07-14 21:15 | Mid-transfer receiver process restart | PASS local | `tools/logs/c3m_local_demo_20260714_211416`; replacement resumed 55/1,100, retried two handoff gaps, completed CRC/decode, source/ground SHA-256 `55392fb9...ec63ac` |
| 2026-07-14 21:27 | Permanent loss then clean next capture | PASS local | `tools/logs/c3m_local_demo_20260714_212447`; transfer 1 saved 1,099/1,100 partial with missing index 100 after bounded retries; transfer 2 completed 1,100/1,100 and exact source/ground SHA-256 `a0f7be74...a2bbad2` without process restart |
| 2026-07-15 10:15 | HIL-MVP-1 startup | PASS HIL | Ground `usb:100000`, satellite `usb:2100000`; direct UVC frame nonblank; GDS PING 4245 and SOH visible; Pi PID 706 / zero restarts |
| 2026-07-15 10:17 | HIL-MVP-2a single nominal | PASS HIL | Product/transfer 1, 38,480 bytes, 1,100/1,100, CRC 25776, 64.8 s, exact source/ground SHA-256 `d166dee7...e214`, 160x120 decode |
| 2026-07-15 10:23 | HIL-MVP-2b repeated nominal | PASS HIL | Products/transfers 2-4; 3/3 CRC-valid exact source/ground files; 64.7-65.2 s; one successful repair round; zero progress events; PING each cycle; PID 706 / zero restarts |
| 2026-07-15 10:24 | RF message-gap diagnostic audit | BUG FOUND | One global TX message ID is checked per-channel on RX, so normal channel interleaving produces false `rf_msg_id_gaps`; use payload missing map/CRC, reassembly, queue, ACK, and PING evidence until fixed |
| 2026-07-15 10:27 | HIL-MVP-3 duplicate active command | PASS HIL | Product/transfer 5; duplicate request at packet 0 emitted `DownlinkRequestDuplicate`, did not reset progress or emit a second start, completed 1,100/1,100 with CRC in 64.7 s; PID 706 / zero restarts |
| 2026-07-15 10:29 | HIL-MVP-5 payload receiver restart | PASS HIL | Product/transfer 6; replacement process loaded checkpoint at 253/1,100, completed after one repair round in 68.7 s; CRC/decode passed; PID 706 / zero restarts |
| 2026-07-15 10:30 | HIL-MVP-6 GDS reader backpressure | PASS HIL | Six-second channel-0 reader stop increased `usb0_backpressure`; recovery count advanced after restart with no new discards/queue drops; PING 37606 returned; PID 706 / zero restarts |
| 2026-07-15 10:35 | Per-channel RF message IDs | PASS local/build | Commit `bdca6a3`; message-ID allocator is per channel on both bridges; focused 5/5 regression, 68-test local gate, native build, 6/6 component suites, and both Teensy builds passed |
| 2026-07-15 10:42 | Post-fix one-photo HIL regression | PASS HIL | Fresh Pi epoch product/transfer 1; 38,480 bytes, 1,100/1,100, CRC 19401, 65.1 s, one repair, exact Pi/ground SHA-256 `102448ad...0227`, 160x120 decode, PING 39002, PID 256 / zero restarts; ground `rf_msg_id_gaps` changed only 1 to 3 with one real reassembly loss instead of climbing by hundreds from channel interleaving |
| 2026-07-15 10:44 | HIL-MVP-8 TX-completion policy | PASS focused injection / N/A physical | Both bridge policies pass injected SENT, timeout-then-success, and terminal-timeout cases; live constants bound each attempt to 500 ms with one retry. Peer removal would test ACK loss, not local TX completion, so no demo-only hardware injection hook was added |
| 2026-07-15 10:52 | HIL-MVP-4 fade attempt 1 | INCONCLUSIVE / nominal control | Product/transfer 2 completed 38,480 bytes and 1,100/1,100 with CRC 27932, zero repair rounds, 64.6 s, exact Pi/ground SHA-256 `eef83100...dcc63`, 160x120 decode, PID 256 / zero restarts. The five-second basic-antenna shield/mispoint produced no observable impairment, so it is not an RF-fade pass; retry only with safe far-field distance/off-axis antenna geometry |
| 2026-07-15 11:03 | Aluminum-near-antenna attempt | INVALID FADE / HARDWARE-STRESS INCIDENT | Conductive aluminum about five feet from the close-range 50-ohm monopole setup likely changed the near-field load/VSWR rather than creating a clean far-field fade. Receiver saved an honest partial at 332/1,100 with 768 missing after 17 repair rounds and a 90 s stall timeout, while satellite debug showed repeated local TX-completion timeouts/terminal failures and required a hard reset. Never repeat this method or count it as HIL-MVP-4 qualification |
| 2026-07-15 11:21 | Post-incident clean cycle | PASS HIL | After a clean satellite-stack reset, PING 39007 returned and a fresh product completed 38,480 bytes, 1,100/1,100, CRC 58576, 64.8 s, exact Pi/ground SHA-256 `a0ef606c...15d4f5`, 160x120 decode, zero repair rounds, zero satellite TX timeouts, PID 255 / zero restarts. Old partial state did not contaminate the reboot-reused product/transfer 1 identity |
| 2026-07-15 11:27 | HIL-MVP-7 ground USB reconnect | PASS HIL | Ground triple USB was removed during product/transfer 2 at 423/1,100. Receiver entered explicit `recovering` with no false completion. After replug, all ports re-enumerated, receiver and GDS auto-reconnected, and the same transfer completed 1,100/1,100 after two repair rounds in 94.3 s with CRC 15298 and exact Pi/ground SHA-256 `05171691...d8c6d`. PING 39008 returned; PID 255 / zero restarts |
| 2026-07-15 11:29 | HIL-MVP-7 clean next cycle | PASS HIL | Product/transfer 3 completed 38,480 bytes, 1,100/1,100, CRC 41214, 64.8 s, zero repairs, exact Pi/ground SHA-256 `b7448a77...70299`, 160x120 decode; satellite TX timeout/drop counters remained zero and PID 255 remained at zero restarts |
| 2026-07-15 11:35 | Portable/battery startup smoke | PASS HIL | Only the ground Teensy remained USB-connected to the Mac; the satellite ran from battery with both ends on normal monopoles. New Pi boot `b6768170...ed92`, deployment PID 254 / zero restarts, GDS and payload receiver remained connected, and PING 39009 traversed RF and completed |
| 2026-07-15 11:37-11:42 | Portable basic-monopole controls / safe fade retries | PASS nominal controls / INCONCLUSIVE fade | Three fresh products/transfers completed 1,100/1,100 in 64.9-65.4 s with CRC-valid exact Pi/ground SHA-256 values `469192d4...f5900`, `1b0d745a...8c99`, and `d7d721e5...7c2e`; all decoded 160x120. The first used one repair round and the next two used zero. PINGs 39010-39012 returned and PID 254 stayed at zero restarts. These operator-cued portable-GDS orientation/distance windows did not produce observable loss, so the fade gate remained open until the later eighth-floor walkaround |
| 2026-07-15 11:46 | HIL-MVP-4 eighth-floor walkaround fade | PASS HIL | Both ends retained normal monopoles; the battery-powered satellite remained inside the lab while the portable GDS was carried outside and around the eighth floor. Product/transfer 4 reached 1,076/1,100 before repair, recovered 24 missing packets in four rounds, and completed 1,100/1,100 in 70.3 s with CRC 61670, exact Pi/ground SHA-256 `56e4df09...c670f`, and 160x120 decode. PING 39013 returned after the walk; PID 254 / zero restarts |
| 2026-07-15 11:50 | HIL-MVP-4 restored-geometry clean cycle | PASS HIL | Back inside the lab at normal monopole geometry, fresh product/transfer 5 completed 38,480 bytes, 1,100/1,100, zero repairs, CRC 49037, and 64.6 s. Pi/ground SHA-256 matched exactly at `3abec65a...7d10c`, decode was 160x120, PING 39014 returned, and PID 254 remained at zero restarts |
| 2026-07-15 11:55 | Ground antenna swap / receiver reconnect | PASS transport / UI observation | Ground Teensy was powered down before replacing its monopole with the 50-ohm Yagi, then all three USB ports, GDS, and the payload receiver auto-reconnected. The receiver temporarily labeled completed product/transfer 5 as `receiving` even though it still showed 1,100/1,100, zero missing, and `crc_ok=true`; fresh product/transfer 6 replaced that stale presentation cleanly. Track reconnect-state reconciliation as a ground-UI follow-up, not RF data corruption |
| 2026-07-15 11:57 | Fixed-position handheld Yagi movement, 15-20 ft | PASS HIL | The operator remained about 15-20 ft from the battery-powered satellite but waved and mispointed the handheld ground Yagi during the transfer; the satellite retained its normal monopole. PING 39015 passed before capture; fresh product/transfer 6 completed 38,480 bytes, 1,100/1,100, zero repairs, CRC 32905, and 64.7 s. Pi/ground SHA-256 matched exactly at `15a5f0ff...43ecea`, decode was 160x120, PING 39016 returned, and PID 254 remained at zero restarts |
| 2026-07-16 07:29 | Receiver reconnect-state reconciliation | PASS local | Terminal complete/partial/failed states now survive a subsequent `ready`; incomplete checkpoints alone resume as receiving. Focused 16-test UI suite, broader 71-test transport/receiver suite, a receiver-restart capture/downlink/decode cycle, and the standard three-cycle exact-decode demo passed; evidence: `tools/logs/c3m_local_demo_20260716_072820` and `tools/logs/c3m_local_demo_20260716_073027` |
| 2026-07-16 07:31 | Archived thermal hover inspection | PASS local/browser | Selected History image reused its archived CSV and displayed `Column 79, row 59 · 19.54°C` as an on-image overlay; thumbnails remained passive and browser console had no warnings/errors |

The hashes above were verified against the live Pi and the exact locally built
HEX artifacts uploaded by physical Teensy IDs during this bench session.

## Completion Definition

MVP hardening is complete when:

- the simplified command flow completes three sequential local cycles and
  three sequential basic-antenna HIL cycles;
- every displayed image comes from the receiver-reconstructed ground artifact;
- packet loss and one ground-process/device interruption recover or terminate
  honestly without poisoning the next cycle;
- duplicate requests cannot reset an active transfer or restart F Prime;
- channel 0 remains usable and bridge faults are observable;
- an operator can repeat captures/downlinks until intentional shutdown; and
- logs identify exact software, firmware, products, CRCs, and failure outcomes.

The MVP target is not a link that never loses a packet. It is a clear,
repeatable F Prime bus demonstration that recovers when practical, fails
honestly when it cannot, and is immediately ready for the next photograph.
