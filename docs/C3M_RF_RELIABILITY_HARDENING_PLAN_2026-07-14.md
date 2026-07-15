# C3M RF Reliability Hardening Plan — 2026-07-14

## BLUF

The EPSCoR C3M F Prime refactor has already passed the nominal indoor HIL demo:
real Lepton capture, RF downlink, ground reconstruction, final CRC, and thermal
display. Outdoor testing exposed the next requirement: the complete system must
behave predictably when RF, USB, a bridge, the receiver, or flight software
temporarily fails.

This plan moves the project from a **proven lab MVP** to a **recoverable,
observable, and honestly failing RF system**. It does not replace the F Prime
architecture with the legacy baremetal design. It preserves the F Prime
App-Man-Drv structure while borrowing the legacy system's strongest operational
traits: bounded radio calls, local transfer retention, phased half-duplex use,
and recovery driven by received-packet state.

This file is the monitoring source of truth for the hardening work. Update the
status table and evidence log after each completed gate. Do not mark a work
package complete from a build or happy-path run alone.

## Current Status

| Gate | Status | Required proof |
| --- | --- | --- |
| Investigation and code comparison | Complete | July 14 logs and current/legacy source reviewed |
| Reliability plan documented | Complete | This document |
| Exact indoor bench baseline | In progress | Provenance bundle plus one clean full transfer |
| Ground bridge fail-safe behavior | Not started | Bounded RF TX and honest USB-write behavior under HIL |
| Receiver persistence and reconnect | Not started | Interrupted transfer resumes after process/USB loss |
| Flight transfer completion contract | Not started | Ground CRC confirmation controls final completion |
| Explicit half-duplex operation | Not started | Deterministic payload, turnaround, repair, and control windows |
| Controlled failure matrix | Not started | All required cases have artifacts and explicit outcomes |
| Outdoor qualification | Not started | Three consecutive qualified transfers at each geometry |

### Live Bench At Plan Start

Recorded 2026-07-14 15:52 HST before hardening code changes:

| Item | Live value |
| --- | --- |
| Hardening branch | `codex/c3m-rf-reliability-hardening` |
| Baseline source HEAD | `7848022b415b8c3ade35c8436bfc72a4076b32a7` |
| F Prime submodule | `v4.2.1-0-ga75021941` |
| Ground Teensy | Triple Serial `SER=11555330`, live upload ID `usb:1100000` |
| Ground channel 0 | `/dev/cu.usbmodem115553301`, owned by the existing GDS process |
| Ground debug | `/dev/cu.usbmodem115553303` |
| Ground channel 1 | `/dev/cu.usbmodem115553305` |
| Satellite Teensy | USB Serial `SER=11556500`, upload ID `usb:2100000`, `/dev/cu.usbmodem115565001` |
| C3M Pi | `raspberrypi-c3m`, `192.168.0.234`, `armv6l`, alias `artemis-pi-c3m` |
| Pi boot ID | `fbf298b9-f2bc-4dd6-98d7-cf95c7368bd5` |
| Pi UART | `/dev/serial0 -> /dev/ttyS0` |
| Pi service | `active`, `NRestarts=0`, `LEPTON_CAMERA_BACKEND=uvc` |
| Deployed Pi release | `/home/pi/artemis/releases/c3m-hil-20260710T013058Z-4bf43c6` |
| Deployed Pi binary SHA-256 | `3be1a1af54c7a1f61aaf42385a603f0425794745807174cb8488a2e0212f9003` |

The Pi is still running the July 9 `4bf43c6` release, not current baseline HEAD
`7848022`. The checked-out Teensy build artifacts also cannot prove which exact
bytes are currently flashed. Gate 0 therefore remains open until current
baseline HEAD is clean-built, flashed/deployed with recorded hashes, and one
nominal full transfer passes.

## User Intent

We are no longer trying to prove that the demo works once. That proof already
exists. We are hardening the full path so ordinary outdoor problems do not turn
into silent corruption, false completion, wedged USB endpoints, watchdog
reboots, or unrecoverable receiver restarts.

The reliability target is:

- Temporary RF loss enters a repair state and completes after RF returns.
- USB backpressure never silently discards a complete packet.
- A disappearing and re-enumerating ground Teensy can be rediscovered.
- Restarting GDS or the payload receiver does not erase the transfer.
- Flight software does not call a downlink complete until ground verifies CRC.
- Radio and USB faults are bounded below the hardware-watchdog deadline.
- Permanent failures end in explicit `PARTIAL`, `EXPIRED`, or `ABORTED` states
  with retained evidence.
- Channel-0/GDS health, channel-1 payload health, USB health, and RF health are
  independently observable.

F Prime provides the component boundaries, telemetry, testability, and
maintainability needed for this. It does not automatically provide recovery
across every new physical and process boundary. Those contracts must be made
explicit across:

```text
F Prime -> Pi UART -> satellite Teensy -> RF
        -> ground Teensy -> USB -> ground application
```

## Evidence That Motivated This Plan

### Nominal path already proven

- Real C3M Lepton product: `38,480` bytes, `160x120`, `19,200` pixels.
- Current F Prime transport: `1,100` indexed payload packets.
- July 9 tabletop qualification: three complete transfers in approximately
  `56–59` seconds, final CRC and Pi/ground SHA-256 equality.
- July 14 indoor runs also completed in approximately `56–58` seconds with no
  retry rounds.

See:

- [C3M demo hardening baseline](C3M_DEMO_HARDENING_PLAN_2026-07-09.md)
- [HIL bench handoff](HIL_BENCH_HANDOFF_2026-07-09.md)
- [C3M RF MVP runbook](EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md)

### Outdoor failure evidence

- At about 40 feet with a Yagi antenna, valid GDS traffic required careful
  pointing and the payload downlink did not complete.
- One transfer stopped after flight reported about 70% progress, followed by a
  new F Prime startup/version/UART-open event group.
- Another nominal transmit pass sent `1,100` packets, but ground requested `288`
  missing packets. Repair recovered only `70`, leaving `218` missing.
- macOS and GDS logs showed earlier complete Triple Serial USB-device loss and
  re-enumeration, including a cadence consistent with the ground Teensy's
  12-second watchdog.
- A later channel-0 failure occurred while all three serial ports remained
  enumerated. GDS still owned the correct port, but its read offset froze. This
  separates a wedged USB transmit endpoint from a full device reset.
- Closing the MacBook lid caused additional real USB disruptions. Those explain
  part of the test-day instability, but not the no-disconnect channel-0 wedge.

### Confirmed design gaps

- Current RF transmit completion can wait without a finite timeout while both
  Teensys use a 12-second watchdog.
- Ground channel-0 and channel-1 forwarding do not treat zero or partial USB
  writes as incomplete delivery before removing queued data.
- The ground receiver keeps its active packet map in memory and exits on a
  serial exception.
- Flight `PayloadDownlinkComplete` currently means packets were emitted toward
  the UART, not that ground reconstructed and CRC-verified the product.
- Payload repair requests can replace an active repair list rather than merge
  idempotently with unfinished work.
- Payload, SOH, commands, ACKs, and repair requests lack a complete explicit
  half-duplex schedule.
- The current path uses `35` science bytes per RF packet versus `45` in the
  legacy path, increasing the full-product packet count from about `856` to
  `1,100` and increasing exposure time.

Both current and legacy radio code configure `433 MHz`,
`GFSK_Rb125Fd125`, and `30 dBm`. The outdoor difference is not explained by a
lower configured transmit power or bitrate in the F Prime refactor.

## Scope And Non-Goals

### In scope

- Ground and satellite Teensy RF timeout/recovery behavior.
- Ground USB zero/partial-write handling and port re-enumeration.
- Persistent ground packet/transfer state.
- F Prime payload-transfer lifecycle and final ground confirmation.
- Selective repair semantics.
- Explicit half-duplex phases and priorities.
- Link and recovery telemetry.
- Local-emulation regression tests, controlled HIL fault injection, and
  outdoor qualification.

### Not in scope unless new evidence requires it

- Replacing RFM23BP with SDR or SatNOGS.
- Replacing the F Prime App-Man-Drv architecture with the legacy monolith.
- Adding an ACK turnaround for every payload packet.
- Retuning bitrate, power, or validated pacing before collecting comparable
  RF-health evidence.
- Treating the GDS green indicator as an RSSI or physical-link measurement.
- Claiming outdoor readiness from a single successful transfer.

## System Invariants

The implementation and tests must preserve these rules:

1. Channel `0` is the bounded, reliable command/event/telemetry control plane.
2. Channel `1` is bulk payload with indexed packets, selective repair, and a
   final whole-product CRC.
3. Channel `2` remains satellite-local RPC and never crosses RF.
4. The primary ground GDS USB stream contains CCSDS bytes only; diagnostics use
   the debug USB interface.
5. A queue entry is removed only after complete delivery or an explicit,
   counted discard decision.
6. Ordinary RF/USB faults must return control before the 12-second hardware
   watchdog expires.
7. `TRANSMIT_COMPLETE` and `GROUND_VERIFIED_COMPLETE` are different facts.
8. Product bytes and received-packet positions are retained until verified
   completion or an explicit retention expiry.
9. Partial products preserve byte positions and missing-packet metadata; they
   never masquerade as CRC-valid complete products.
10. Every physical upload uses the Teensy `usb:*` identity, never an ambiguous
    serial-device path when both boards are attached.

## Target State Machines

The exact names may change during design review, but these behaviors are the
required contract.

### RF and Teensy bridge

```text
RX_IDLE
  -> TX_QUEUED
  -> TX_ACTIVE
  -> TX_WAIT_BOUNDED
  -> TURNAROUND
  -> RX_IDLE

TX_WAIT_BOUNDED
  -> TX_TIMEOUT
  -> RADIO_RECOVERY
  -> RX_IDLE or DEGRADED
```

Required behavior:

- Finite TX-completion deadline.
- Bounded local attempts.
- Radio/FIFO reinitialization after timeout.
- No queue pop on failed TX.
- Persistent or immediately reportable reset cause.
- Counters for TX timeout, recovery, failure, and queue high-water.

### Flight payload transfer

```text
IDLE
  -> PREPARE
  -> TRANSMIT
  -> WAIT_GROUND_STATUS
  -> REPAIR
  -> WAIT_GROUND_STATUS
  -> VERIFIED_COMPLETE

Terminal degraded outcomes:
  PARTIAL_EXPIRED
  ABORTED
  RECOVERING_AFTER_RESTART
```

Required behavior:

- Nominal transmit completion does not clear the product.
- Ground CRC confirmation is required for `VERIFIED_COMPLETE`.
- Header, transfer identity, END/status, and repair data remain available while
  the transfer is retained.
- Duplicate or overlapping repair requests are idempotent and additive.
- Transfer identity includes enough epoch/session information to distinguish a
  flight restart from an old request.

### Ground USB and receiver

```text
DISCONNECTED
  -> ENUMERATING
  -> OPENING
  -> READY
  -> RECEIVING
  -> REQUESTING_REPAIR
  -> VERIFYING
  -> COMPLETE

USB short write:
  RECEIVING -> BACKPRESSURED -> RECEIVING or FAILED

USB disappearance/process restart:
  RECEIVING -> RECOVERING -> OPENING -> RECEIVING

Transfer expiry:
  RECEIVING or REQUESTING_REPAIR -> PARTIAL_SAVED
```

Required behavior:

- Track the exact number of USB bytes accepted.
- Retain and resume the unwritten suffix after zero/partial writes.
- Rediscover the correct composite Teensy by stable physical identity.
- Persist header, bitmap, packets, retry round, and timestamps incrementally.
- Save an honest partial artifact immediately when recovery cannot continue.

### Explicit half-duplex phases

```text
CONTROL_WINDOW
  -> PAYLOAD_BURST
  -> TURNAROUND_QUIET
  -> GROUND_STATUS_WINDOW
  -> REPAIR_BURST
  -> GROUND_STATUS_WINDOW
  -> FINAL_CONFIRMATION
  -> CONTROL_WINDOW
```

Rules:

- Payload bursts have a defined size or duration.
- Ground transmits repair/status only during a ground-owned window.
- Critical channel-0 commands can interrupt only through a defined priority
  rule; periodic SOH does not transmit arbitrarily during bulk downlink.
- A lost status or confirmation message leads to bounded repetition, not an
  ambiguous permanent wait.
- Bulk payload keeps selective repair rather than per-packet ACK turnaround.

## Work Plan

### Gate 0 — Freeze and record the indoor baseline

- [ ] Keep the Mac awake with the lid open for all qualification runs.
- [ ] Re-enumerate both Teensys and record all serial roles.
- [ ] Record ground upload ID `usb:100000` and satellite upload ID
      `usb:2100000`; verify them live rather than assuming the old mapping.
- [ ] Record repository branch/HEAD, submodule SHAs, F Prime version, both
      Teensy firmware provenance, deployed Pi version, binary SHA-256, Pi boot
      ID, and service restart count.
- [ ] Confirm the C3M Pi through `artemis-pi-c3m`/`c3m-pi`, `uname -m`, and the
      active `/dev/serial0` service process.
- [ ] Capture ground data, ground debug, ground payload, satellite debug,
      payload-receiver logs, Pi journal, and macOS USB events into one run
      directory.
- [ ] Reset/replug the currently wedged ground Teensy, restart GDS cleanly, and
      prove that channel 0 moves again.
- [ ] Run one full basic-antenna transfer and require `1,100/1,100`, final CRC,
      source/ground SHA-256 equality, and no unexplained restart.

Gate passes only when the full provenance and proof bundle is saved.

### WP1 — Ground and satellite bridge fail-safe behavior

- [ ] Add a finite RF TX-completion timeout below the watchdog deadline.
- [ ] Add bounded retry followed by explicit radio/FIFO recovery.
- [ ] Preserve a queued RF message until success or counted terminal failure.
- [ ] Track actual bytes accepted by channel-0 and channel-1 USB writes.
- [ ] Retain unwritten data across zero/partial USB writes.
- [ ] Add USB short-write, backpressure, timeout, and discard counters.
- [ ] Route radio initialization and recovery diagnostics only to the debug USB
      interface.
- [ ] Verify that a satellite Teensy reset cannot unintentionally hard-cycle the
      Pi through unsafe `RPI_ENABLE_PIN` startup behavior.

Acceptance:

- A missing TX-done condition returns through `TX_TIMEOUT`/`RADIO_RECOVERY`
  without a Teensy watchdog reset.
- Stopping the Mac reader cannot produce a false full-write counter.
- Restarting GDS after backpressure restores channel 0 without requiring an
  unexplained sequence of direct-port opens.

### WP2 — Persistent and resumable ground receiver

- [ ] Persist transfer header and product identity when first accepted.
- [ ] Persist the received-packet bitmap and packet bytes incrementally.
- [ ] Reopen a re-enumerated port by stable device identity.
- [ ] Resume the same transfer after receiver/web-app restart.
- [ ] Preserve partial state when GDS remains alive but channel 1 disconnects.
- [ ] Save `.fdp.partial`, missing-packet map, and explicit failure reason when
      retention expires.
- [ ] Keep the CLI receiver as an engineering fallback while making the web app
      the normal operator surface.

Acceptance:

- Receiver restart or USB unplug at 25%, 50%, or 90% resumes and reaches the
  correct final CRC after the link returns.
- A permanent interruption produces a position-preserving partial artifact and
  never a false complete result.

### WP3 — Flight completion and repair contract

- [ ] Separate local `TRANSMIT_COMPLETE` from ground
      `VERIFIED_COMPLETE` events and telemetry.
- [ ] Retain the product, metadata, and packet source after nominal transmit.
- [ ] Add an explicit ground final-CRC confirmation message.
- [ ] Define confirmation timeout, repetition, expiry, and operator-visible
      failure behavior.
- [ ] Merge duplicate/overlapping repair requests idempotently.
- [ ] Pace retry requests according to actual repair throughput so a new
      request cannot overwrite unfinished repair work.
- [ ] Repeat header and END/status metadata during recovery windows.
- [ ] Define behavior for flight restart and stale transfer IDs.
- [ ] Keep UART work out of timing-critical rate-group execution where blocking
      or approximately one-second drain time can cause cycle slips.

Acceptance:

- Flight never reports verified completion before ground CRC proof.
- Lost END, lost confirmation, duplicate repair, overlapping repair, and flight
  restart all reach explicit deterministic states.

### WP4 — Explicit half-duplex arbitration

- [ ] Define ownership, duration, and priority for each half-duplex phase.
- [ ] Reserve ground status/repair windows between payload bursts.
- [ ] Define how critical channel-0 traffic interrupts or waits.
- [ ] Suppress or defer routine SOH/event bursts during bulk payload windows.
- [ ] Add telemetry for current link phase, phase age, owner, pending work, and
      last transition reason.
- [ ] Define bounded recovery if either side misses a phase transition.

Acceptance:

- Payload and repair traffic do not transmit simultaneously.
- Channel 0 remains predictably available according to the documented priority
  rule rather than succeeding opportunistically.
- Every phase timeout has one documented recovery transition.

### WP5 — Observability

- [ ] Export RF last/min/average RSSI and RadioHead good/bad counts.
- [ ] Export per-channel RF TX/RX, missing, retry, reassembly, and drop counts.
- [ ] Export TX-completion timeouts, radio recoveries, watchdog reset causes,
      USB short writes, and queue high-water marks.
- [ ] Keep GDS link health, payload-transfer health, USB health, and RF health
      as separate operator indicators.
- [ ] Record exact timestamps and state transitions in each run artifact.
- [ ] Make reset/recovery evidence survive long enough to be captured after
      USB re-enumeration.

Acceptance:

- A tester can distinguish RF fade, USB backpressure, USB device reset, GDS
  decoder loss, receiver restart, flight restart, and transfer expiry without
  inferring from the green GDS indicator alone.

### WP6 — Deterministic local and component tests

- [ ] Preserve the current successful local C3M flow.
- [ ] Add tests for USB zero writes, partial writes, delayed writes, and terminal
      write failure.
- [ ] Add tests for TX timeout and radio recovery.
- [ ] Add tests for persistent receiver restart/resume.
- [ ] Add loss cases for `1`, `10`, `100`, `300`, and approximately `550`
      payload packets.
- [ ] Add tests for lost header, lost END, lost final confirmation, duplicate
      repair, overlapping repair, and stale transfer identity.
- [ ] Add state-machine tests for every timeout and terminal state.

Acceptance:

- The failure matrix passes locally before reflashing hardware.
- Partial products remain honest and complete products retain exact CRC/hash
  equality.

### WP7 — Controlled indoor HIL qualification

Run one physical step and one confirmation at a time. Preserve logs for every
case.

| Test | Injection | Required outcome |
| --- | --- | --- |
| HIL-01 | Nominal full transfer | Final CRC/hash match; no retries or resets |
| HIL-02 | Stop reading channel 0 | Backpressure is counted; no silent success; recovery is bounded |
| HIL-03 | Stop reading channel 1 | Payload state persists; resumed reader completes or saves partial |
| HIL-04 | Restart GDS only | Channel 0 recovers without resetting transfer state |
| HIL-05 | Restart payload web app | Same transfer reloads and resumes |
| HIL-06 | Unplug/replug ground USB at 25%, 50%, 90% | Device rediscovered; transfer resumes |
| HIL-07 | Make RF peer unavailable during TX | Finite TX timeout; radio recovery; no watchdog reset |
| HIL-08 | Temporarily block/mispoint antennas | Repair state entered; completion after RF returns |
| HIL-09 | Overlap repair requests | Requests merge; no repair progress is overwritten |
| HIL-10 | Restart F Prime mid-transfer | New epoch recognized; deterministic resume or explicit restart |
| HIL-11 | Lose final ground confirmation | Flight repeats status and remains retained until timeout/confirmation |
| HIL-12 | Permanent RF loss | Explicit partial/expired outcome with complete evidence bundle |

Gate passes only after all cases meet their required outcome without manual
state repair that is not part of the documented operator procedure.

### WP8 — Controlled RF geometry and outdoor qualification

- [ ] Use identical boards, firmware, power, cables, and antenna polarization
      for current-versus-legacy comparisons.
- [ ] Qualify known-good basic antennas at bench, 10 feet, and 40 feet before
      introducing the Yagi.
- [ ] Mount the Yagi on a fixed tripod and record frequency band, front/back
      orientation, azimuth, elevation, and polarization.
- [ ] Record connector/adapter chain, feed-line, supply voltage during TX, and
      SWR/return loss when measurement equipment is available.
- [ ] Run a transfer-size ladder before the full `38,480`-byte product.
- [ ] Preserve RSSI, packet loss, retries, state transitions, USB events, Pi
      journal, and transfer artifacts for every run.
- [ ] Consider a lower-rate RF profile only after the identical-geometry
      baseline and RSSI/loss data exist.

Final acceptance at each intended geometry:

- Three consecutive full products reach ground-verified CRC completion.
- No USB re-enumeration, watchdog reset, or unexplained F Prime restart.
- Temporary RF fades recover without restarting the operator tools.
- Permanent loss produces the documented partial/expired result.
- Channel 0 remains available according to the half-duplex priority contract.

## Implementation Discipline

For each work package:

1. Record the current failing test and expected state transition.
2. Make the smallest coherent change.
3. Run focused unit/local tests.
4. Build both affected Teensy workspaces and/or F Prime deployment.
5. Flash only by verified physical `usb:*` identity.
6. Run the focused HIL failure case.
7. Record artifacts and update this document.
8. Stop if the failure signature changes unexpectedly; investigate before
   stacking another change.

Do not change RF PHY or pacing values in the same patch as state/recovery
semantics. That would make root-cause comparison ambiguous.

## Proof Bundle Per HIL Run

Each run directory should contain or identify:

- repository branch/HEAD and submodule SHAs
- both Teensy firmware hashes/build identities
- deployed Pi version and SHA-256
- Pi boot ID and `artemis-fprime.service` restart count
- serial-device identities and port-role mapping
- ground channel-0/GDS log
- ground debug-counter log
- payload receiver/web-app log and `run.json`
- satellite debug-counter log
- bounded Pi service journal
- macOS USB events for the run window
- RF/state-transition telemetry
- source product size/hash
- ground complete or partial product, missing map, CRC, and hash
- explicit result: `PASS`, `RECOVERED`, `PARTIAL`, `EXPIRED`, or `ABORTED`

## Completion Definition

The hardening effort is complete only when:

- all state machines and terminal outcomes are implemented and documented
- local failure tests pass
- the complete indoor HIL failure matrix passes
- three consecutive outdoor full-size transfers pass at the target geometry
- an independent operator can reproduce recovery using the runbook
- the final release bundle records exact firmware/deployment provenance

The target is not an RF link that never fails. The target is a system that
detects failure, preserves useful state, recovers when possible, and reports an
honest outcome when recovery is no longer possible.

## Running Evidence Log

Add entries after work begins. Keep them short and link the durable artifact.

| Date/time (HST) | Gate/test | Result | Evidence or blocker |
| --- | --- | --- | --- |
| 2026-07-14 | Investigation | Complete | Indoor nominal runs, outdoor GDS/USB logs, current-versus-legacy code comparison |
| 2026-07-14 | Planning | Complete | Reliability state model and phased work plan captured in this document |
| 2026-07-14 15:52 | Gate 0 provenance | In progress | Live USB identities, Pi identity/service, deployed release, binary hash, and version mismatch recorded |
