# EPSCoR C3M → F´ Refactor Plan

Branch: `EPSCOR_C3M_REFACTOR`
Status: PLANNING
Companion docs: [EPSCOR_TEENSY_DEMO.md](EPSCOR_TEENSY_DEMO.md) (current-system analysis),
`docs/OPTIMAL_FPRIME_COMPONENT_TOPOLOGY_PLAN.md`, `docs/PAYLOAD_DOWNLINK_PROTOCOL_ADVICE.md`,
`docs/RADIO_AGNOSTIC_COMMS_AND_PAYLOAD_DOWNLINK_PLAN.md`

---

## 1. Why we are doing this

The EPSCoR C3M flatsat demo works end to end and demos well: thermal capture on the Pi,
RF downlink with per-packet CRC + retry, CSV export, live stream viewer. The problem is the
shape of the code, not its behavior:

| File | Lines | Problem |
| --- | ---: | --- |
| `SatellitePayload/satellite_teensy/satellite_teensy.ino` | ~2,450 | Radio driver + payload protocol + UART framing + 4 sensor subsystems + command parser + power control in one file |
| `dev-teensyGroundStation/ground_station_teensy/ground_station_teensy.ino` | ~2,500 | RF reassembly + retry + CSV export + CLI + stream forwarding in one file |
| `SatellitePayload/thermal_camera_controller.py` | ~740 | Camera driver + capture logic + UART framing + state machine in one script |

Every new feature touches shared global state in a monolith, so changes are brittle and
regressions are easy. There is no commanding/telemetry framework, no dictionary, no
separation between mission logic and hardware.

**The strategic goal:** the Artemis CubeSat bus will be reused by future student
generations (open source). Neutron 2's F´ workspace (`ArtemisRpiTeensy_N2`) already defines
the reusable layered architecture: **Mission layer → Service layer → Hardware adapter
layer**. Between EPSCoR C3M and Neutron 2, *the bus is the same — only the payload and its
comms differ*. If we refactor C3M onto that architecture, we prove the claim that future
missions only need to write a new `PayloadAdapter_*` (and maybe a `CommsAdapter_*`), and
everything else is reused.

## 2. The key advantage: the demo is the spec

We are not designing from scratch. The working demo gives us the left and right bounds:

- The **behavior contract** is fully documented in [EPSCOR_TEENSY_DEMO.md](EPSCOR_TEENSY_DEMO.md)
  (UART framing, RF packet formats, retry bitmap, status strings, operator flow).
- Every refactor phase has a **golden reference**: if the F´ version can reproduce the
  single-capture sequence (§8 of that doc) and produce the same CSV/image output, the
  phase is done.
- `docs/PAYLOAD_DOWNLINK_PROTOCOL_ADVICE.md` already analyzed the hard question (bulk
  image bytes over RFM23BP) and concluded: **keep the EPSCoR packet protocol for bulk
  payload bytes; use F´ for commanding, mode transitions, progress telemetry, and
  events.** Do not push 40 KB images through stock `Svc.FileDownlink` over this radio.

Rule for the whole refactor: **treat the proven protocols (UART framing `DE AD BE EF`,
49-byte RF packets, CRC16, retry bitmap) as frozen contracts.** We move *who owns the
logic*, not *what goes over the wire*, until everything is under F´ and we choose to
evolve a protocol deliberately.

## 3. Target architecture

### 3.1 Where each piece of today's demo lands

| Today (baremetal demo) | Target (F´ refactor) |
| --- | --- |
| `thermal_camera_controller.py` capture/averaging logic | **`PayloadAdapter_C3MThermal`** (new F´ component on the Pi) |
| `thermal_camera_controller.py` UART image transfer to Teensy | Pi-local file write + handoff to downlink path (image no longer needs to cross UART just to reach the radio owner — see §3.3) |
| `satellite_teensy.ino` command listener (`u`,`r`,`v1`…) | F´ commands on `ScienceManager` / `PayloadService` (dictionary-defined, ACKed) |
| `satellite_teensy.ino` capture state machine / `RPI_IDLE_READY` | `PayloadService` (real implementation replacing the `Svc.Ping` placeholder) |
| `satellite_teensy.ino` RF packetization + retry resend | **Stays on the Teensy** as a payload-downlink module in `ArtemisTeensy_N2_Baremetal` firmware (proven, radio-matched) |
| `satellite_teensy.ino` TMP36 / INA219 / GPS / IMU polling | Stays on Teensy (sensors are wired to it); reported over a sideband status channel consumed by service-layer components → F´ telemetry (`EpsService`, `GpsService`) |
| `satellite_teensy.ino` Pi power control (`p1`/`p0`) | Stays baremetal on Teensy (you cannot run Pi power control *on* the Pi); exposed as a Teensy-local command, not part of nominal F´ flow |
| `ground_station_teensy.ino` RF reassembly → USB | `GDS_Teensy` relay firmware (already exists) + payload-packet pass-through |
| `ground_station_teensy.ino` retry tracking, CSV export | **Ground Python helper** (PC-side, where it belongs) |
| `ground_station_serial_cli_teensy.py` operator CLI | `fprime-gds` (commands, telemetry, events) + companion payload viewer/reconstructor script |
| `pdu_comm/` test utility | Folded into `EpsAdapter_Artemis` / `EpsService` later (out of MVP scope) |

### 3.2 Layered component view (reusing N2 components)

```
RPi (F´ deployment — reuse ArtemisRpiTeensyDeployment)
├── Standard F´ services: CdhCore, ComCcsds, rate groups, LinuxUartDriver  [REUSE]
├── Mission layer
│   ├── MissionManager   (modes: BaseMode → Collecting → ScienceTx)        [REUSE]
│   ├── ScienceManager   (orchestrates capture → product → downlink)      [REUSE, make real]
│   ├── SoHManager                                                        [REUSE]
│   └── CommsManager                                                      [REUSE]
├── Service layer
│   ├── PayloadService   (stable payload interface)                       [REUSE, make real]
│   ├── TeensyTransportService                                            [REUSE]
│   ├── EpsService / GpsService                                           [REUSE, make real]
│   └── StorageService                                                    [REUSE]
└── Adapter layer (the ONLY mission-specific part)
    ├── PayloadAdapter_C3MThermal   ← NEW: wraps PureThermal/UVC camera
    └── (PayloadAdapter_N1Legacy / future PayloadAdapter_N2 swap in here)

Satellite Teensy (baremetal — extend ArtemisTeensy_N2_Baremetal firmware)
├── Raw CCSDS tunnel UART ↔ RF relay                                       [EXISTS]
├── NEW: payload bulk-downlink module (EPSCoR packetizer + retry resend, ported from demo .ino)
└── NEW: sensor poll + status sideband (TMP36/INA219/GPS/IMU, ported from demo .ino)

Ground Teensy (baremetal — extend GDS_Teensy firmware)
├── RF reassembly → USB raw bytes for fprime-gds                           [EXISTS]
└── NEW: payload-packet classification → tagged binary pass-through to PC helper

Ground PC
├── fprime-gds            (command/telemetry/events — the operator console) [EXISTS]
└── payload_downlink_helper.py  ← NEW: retry requests, reconstruction, CRC,
    CSV export, viewer (port of demo CLI + ground-Teensy reassembly logic)
```

### 3.3 The two-plane design (the most important decision)

Per `docs/PAYLOAD_DOWNLINK_PROTOCOL_ADVICE.md` Option 1, traffic splits into two planes:

1. **Control plane (F´/CCSDS):** commands up, telemetry/events down, through the existing
   transparent Teensy tunnel. Low rate, dictionary-defined, visible in `fprime-gds`.
2. **Payload bulk plane (EPSCoR protocol):** indexed 49-byte RF packets with per-packet
   CRC16, whole-image CRC16, and retry bitmap — exactly what the demo proved works on the
   RFM23BP.

In the demo, the image crossed the Pi→Teensy UART because the Teensy owned the only radio
protocol. That stays true: when `ScienceManager` commands a downlink, the Pi pushes the
image file to the Teensy over UART (existing `DE AD BE EF` framing) and the Teensy runs the
proven packetizer.

**UART contention risk and mitigation:** the CCSDS tunnel and the payload bulk transfer
must not interleave on one UART. Teensy 4.1 has 8 hardware UARTs and the demo already used
`Serial2` (pins 7/8) for the image path while N2 relay uses its own UART. **Use two
UARTs:** UART-A = CCSDS tunnel (always on), UART-B = payload bulk (`DE AD BE EF` framing,
only active during transfer). This keeps the GDS link alive during downlink so progress
telemetry streams while the bulk transfer runs. If wiring forces a single UART, fall back
to a half-duplex mode switch commanded by F´ (downlink telemetry pauses during transfer) —
acceptable but strictly worse for the demo story.

Radio-time contention still exists (one RFM23BP): `CommsManager`/`MissionManager` enter a
`ScienceTx` mode that throttles telemetry to a slow heartbeat while payload packets own
the air, mirroring the radio-agnostic comms plan.

## 4. Command and telemetry contract (old → new)

| Demo RF cmd | Meaning | F´ replacement |
| --- | --- | --- |
| `u` | trigger Pi capture | `ScienceManager.START_COLLECTION` → `PayloadService` → `PayloadAdapter_C3MThermal` |
| `r` | transmit image | `PayloadService.START_PAYLOAD_DOWNLINK` (Pi pushes file to Teensy, Teensy packetizes) |
| `0xBB` bitmap | retry request | unchanged on-air; generated by PC helper, handled by Teensy module |
| `v1`/`v0` | stream start/stop | `PAYLOAD_STREAM_START/STOP` commands — **stretch goal, Phase 5** |
| `p1`/`p0`/`ps` | Pi power | stays Teensy-local (hardware function below F´) |
| `s*` | GPS/IMU queries | replaced by continuous F´ telemetry channels via sensor sideband |
| `g` | ping | `Svc.Health` / existing `PingResponder` |
| `~` | reset satellite Teensy | Teensy-local; optionally a `TeensyTransportService` command later |
| `capture`/`request`/`export` CLI | operator flow | `fprime-gds` commanding + helper auto-reconstruct/export |

New telemetry (replaces ad-hoc `STATUS:*` strings and serial prints):

- `PayloadService`: `CaptureState`, `LastImageSize`, `LastImageCrc`, `FramesAveraged`
- Downlink progress: `PacketsSent`, `RetryRound`, `MissingCount` (sourced from Teensy
  sideband status so judges watch progress live in GDS)
- `EpsService`/`GpsService`: temperature[7], current/voltage[5], GPS fix, IMU — from the
  Teensy sensor sideband

The Pi-side `STATUS:*` strings (`CAM_READY`, `CAPTURE_DONE`, …) become F´ **events** of
the adapter — same semantics, now timestamped and dictionary-defined.

## 5. Phased migration (every phase ends demo-able)

Never break the demo: `espcor_teensy_demo/` stays untouched as the frozen reference; all
new work happens in the F´ workspace and firmware folders.

### Phase 0 — Freeze the baseline (½ day)
- Tag current `main` (e.g. `epscor-c3m-baremetal-demo`) so the working demo is always
  recoverable.
- Capture golden artifacts into `espcor_teensy_demo/golden/`: a known-good
  `thermal_data_###.csv`, a serial transcript of one full capture→request→export run.
- Exit criteria: a teammate can re-run the baremetal demo from the tag + runbook.

### Phase 1 — F´ control plane on the C3M flatsat (1–2 weeks)
- Bring up the existing N2 stack unchanged on EPSCoR hardware: `ArtemisRpiTeensyDeployment`
  on the Pi, `ArtemisTeensy_N2_Baremetal` relay on the satellite Teensy, `GDS_Teensy` on
  the ground Teensy, `fprime-gds` on the PC.
- No payload yet. This validates that "the bus is the same" claim with zero new code.
- Exit criteria: ping, `NO_OP`, and SOH telemetry round-trip over the C3M RF chain.

### Phase 2 — Real payload capture under F´ (2–3 weeks)
- Write `PayloadAdapter_C3MThermal`: replace the placeholder `Svc.Ping` ports on
  `PayloadService`/adapter with real FPP ports/types (capture request, capture result
  {size, crc, path}, adapter status).
- Camera integration decision (recommended: **2a first, 2b later**):
  - **2a (fast):** adapter manages `thermal_camera_controller.py` stripped to
    capture-only (no UART code) as a child process; image lands as a file on the Pi;
    adapter reports result. Lowest risk — reuses the proven uvctypes capture/averaging
    code verbatim.
  - **2b (clean):** native C++ capture via libuvc inside the adapter. Do this only after
    2a is demo-stable.
- Wire `MissionManager → ScienceManager → PayloadService → adapter`, including the
  scheduled-collection story (`START_COLLECTION delay=10s`).
- Exit criteria: GDS command produces a 160×120×16-bit averaged image file on the Pi,
  with capture events/telemetry visible in GDS. (Verifiable over SSH; no downlink yet.)

### Phase 3 — Payload bulk downlink (2–3 weeks, highest risk)
- Port the packetizer/retry-resend out of `satellite_teensy.ino` into a clean module in
  `ArtemisTeensy_N2_Baremetal/firmware` (`payload_downlink.{h,cpp}`): header packet, 45-byte
  data chunks + CRC16, end packet, `0xBB` retry bitmap handling — byte-identical on air.
- Add UART-B receive path (`DE AD BE EF` framing) on the Teensy; add Pi-side sender in
  `PayloadService`/`TeensyTransportService` (open `/dev/ttyAMA*`, frame, send, watch acks).
- Extend `GDS_Teensy` to classify payload packets and forward them tagged over USB
  alongside the raw CCSDS stream (reuse the demo's `WRM!`-style binary tagging idea).
- Write `payload_downlink_helper.py` on the PC: missing-packet bitmap, retry requests,
  reconstruction, CRC verify, CSV export, viewer — ported from the demo ground CLI +
  ground-Teensy reassembly logic (logic already exists; it moves from C++ on a Teensy to
  Python on the PC where it is testable).
- Add downlink progress telemetry through the control plane; `MissionManager` enters
  `ScienceTx` mode and throttles telemetry during transfer.
- Exit criteria: full demo sequence via GDS — schedule collection, watch mode transitions
  and progress telemetry, image reconstructs on PC with matching CRC, CSV matches the
  golden-format output from Phase 0.

### Phase 4 — Sensors and SOH (1–2 weeks, parallelizable with Phase 3)
- Port TMP36/INA219/GPS/IMU polling from the demo .ino into clean firmware modules; emit a
  compact binary sensor-status sideband (either as a reserved message type on the relay
  link or on UART-B between transfers).
- Map into `EpsService`/`GpsService` telemetry; `SoHManager` aggregates the judge-facing
  health page.
- Exit criteria: live temperatures, currents, GPS, IMU in GDS channels during BaseMode.

### Phase 5 — Stretch + cleanup
- Livestream mode (80×60 8-bit) re-evaluated: likely stays a special Teensy mode triggered
  by an F´ command, or is dropped — it is demo candy, not mission-critical.
- Retire duplicated code: the EPSCoR satellite/ground .ino monoliths are now fully
  superseded; `espcor_teensy_demo/` is marked reference-only (it already is in README).
- Write the "new mission HOWTO": *to fly a new payload on the Artemis bus, implement
  `PayloadAdapter_<YourPayload>` against `PayloadService`'s ports and you are done.* This
  document is the deliverable that proves the reuse story for future teams.
- Fold `pdu_comm/` into `EpsAdapter_Artemis` when EPS work starts.

## 6. Risks and open questions

| Risk | Mitigation |
| --- | --- |
| Single shared RFM23BP: telemetry vs. payload packets fight for air time | Explicit `ScienceTx` mode throttles control plane during bulk transfer (Phase 3); already anticipated in comms plan |
| Single-UART wiring on current flatsat harness | Strongly prefer adding UART-B (Teensy 4.1 has 8 UARTs; demo already used Serial2 pins 7/8). Half-duplex mode switch is the fallback |
| Pi Zero W performance with F´ + camera capture | Already validated F´ on Pi Zero W (ARMv6 cross-build, `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`); capture is burst CPU, schedule capture outside telemetry-heavy moments; 2a child-process isolates camera memory |
| Placeholder `Svc.Ping` ports across mission/service components mean "wiring exists, semantics don't" | Phase 2 defines the real FPP types once, on the payload path first — the highest-value path — before generalizing |
| Retry protocol moves from ground Teensy (C++) to PC helper (Python) — subtle behavior drift | Golden transcripts from Phase 0; keep retry constants identical (grace period, threshold, max 2 rounds); unit-test bitmap handling against recorded packet logs |
| Team bandwidth / brittle hardware time | Phases 1 and 4 are low-risk and parallelizable; Phase 3 is the only genuinely new integration and gets the buffer |

Open questions to settle before Phase 3:
1. Confirm physical availability of a second Pi↔Teensy UART on the C3M flatsat harness.
2. Decide where reconstructed images/CSVs live on the PC (GDS plugin vs. standalone helper
   window) for the judge-facing display.
3. Decide whether sensor sideband rides the relay link (reserved msg type) or UART-B.

## 7. Definition of done

The refactor is complete when the original demo §8 sequence runs entirely through F´:

1. Operator opens `fprime-gds`, sees BaseMode SOH telemetry (temps, currents, GPS, IMU).
2. Operator sends `START_COLLECTION` with a 10 s delay; watches mode transition events.
3. Capture completes; `ScienceProductReady` event with size + CRC.
4. Operator sends `START_PAYLOAD_DOWNLINK`; watches progress telemetry while the helper
   reconstructs the image, auto-retries missing packets, verifies CRC, exports CSV, and
   opens the viewer.
5. **Swap test (the reuse proof):** replace `PayloadAdapter_C3MThermal` with
   `PayloadAdapter_N1Legacy` (or a stub) in the topology, rebuild, and everything else
   still runs. That swap is the demo for future Artemis-bus teams.
