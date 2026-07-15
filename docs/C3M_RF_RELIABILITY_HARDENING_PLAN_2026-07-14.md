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
| Nominal single capture/downlink/decode | Passed | Passed with current artifacts: product/transfer 1, 38,480 bytes, 1,100/1,100, exact source/ground SHA-256, 64.8 s |
| Bounded Teensy RF TX timeout/retry/recovery | Passed host tests and builds | Needs bench injection |
| Honest ground USB writes and independent queues | Passed host tests and build | Current firmware flashed; channel-0 queue/drop/backpressure counters stayed flat after GDS opened; focused stop/restart remains |
| Persistent/re-enumerating ground receiver | Passed focused tests | Needs unplug/process-restart proof |
| Duplicate start and bounded F Prime payload work | Passed component tests | Needs target timing proof |
| Additive N2 repair | Passed component tests | Passed one natural one-round repair in the 3/3 nominal run; focused fade remains |
| Automatic progress-event removal | Passed component/full local validation | Passed observation: zero automatic progress events; five PING responses delivered during four bulk transfers |
| Ground-side local reconstruction | Passed: real receiver PTY/CRC/decode path | Passed physical channel-1 proof with four exact source/ground files and complete decode artifacts |
| Repeated capture/downlink cycles | Passed: three cycles, one uninterrupted session | Passed: three new products/transfers, 3/3 CRC-valid, 64.7-65.2 s, no process/hardware restart |
| Deterministic packet-loss repair | Passed: dropped DATA 100, retry/repair/CRC | Needs brief-RF-fade proof |
| Permanent loss then clean next cycle | Passed: honest 1,099/1,100 partial, next transfer exact | Needs sustained-fade proof |
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
| HIL-MVP-4 brief RF fade | Block/mispoint basic antennas briefly, then restore | Missing packets requested/repaired or honest partial; next cycle succeeds | Pending |
| HIL-MVP-5 ground receiver restart | Restart only payload receiver during transfer | State reloads and completes or remains honest partial; following cycle succeeds | **PASS** — replacement receiver resumed transfer 6 at 253/1,100; one repair round; CRC complete in 68.7 s; PID unchanged |
| HIL-MVP-6 channel-0 backpressure | Stop/restart only GDS reader | Short write/backpressure counted; no false delivery or watchdog reset; PING recovers | **PASS** — six-second reader stop increased backpressure, then recovery count; no new discards/queue drops; PING 37606 returned; PID unchanged |
| HIL-MVP-7 ground USB reconnect | Unplug/replug ground Teensy once | Ports rediscovered; no silent complete; current attempt resolves honestly; next cycle succeeds | Pending |
| HIL-MVP-8 RF TX timeout injection | Exercise the bounded `waitPacketSent` failure path with a focused host-injected test; optionally remove the peer only to exercise ACK retry | Bounded retry/recovery; no 12-second watchdog reset; do not mislabel no-peer ACK loss as a TX-completion timeout | **PASS focused injection / N/A physical** — both bridges use a 500 ms completion timeout and one recovered retry; injected timeout/terminal cases pass; no MVP-only hardware hook added |

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
| 2026-07-14 21:11 | Pi Zero W target build | PASS local | ARMv6KZ, VFPv2, `/lib/ld-linux-armhf.so.3`; binary SHA-256 `e0176a21...ff814b`; deployment/HIL still pending |
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
