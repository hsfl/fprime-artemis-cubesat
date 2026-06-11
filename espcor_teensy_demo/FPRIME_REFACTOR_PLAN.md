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

## 2.5 Decision log (locked — implement as written)

These decisions are made. Future coding agents should implement them, not re-litigate them.

| # | Decision |
| --- | --- |
| D1 | One Pi↔Teensy UART, shared via commanded half-duplex mode switch (tunnel ↔ bulk). No second UART exists (§3.3). Start at 115200 baud; raising baud is a later tuning step, not a blocker. |
| D2 | The Teensy keeps the EPSCoR payload packetizer; the Pi pushes the image file over the UART bulk mode. Bulk payload bytes never ride the CCSDS/GDS stream. |
| D3 | All on-wire formats are frozen contracts: UART `DE AD BE EF` framing, 49-byte RF packets, CRC16-CCITT-FALSE, `0xBB` retry bitmap, retry constants (§5 Phase 3). Change nothing on the wire until everything runs under F´. |
| D4 | Camera integration starts as a managed Python child process (Phase 2a); native C++ (2b) only after 2a is demo-stable. |
| D5 | Assume current OBC wiring (version 4.24) is correct — the demo and N2 firmware both run on it. Serial2 (pins 7/8) is the Pi link. Do not design for the modular-radio pin table (§3.3 caveat) unless new hardware actually arrives. |
| D6 | Sensor sideband rides the relay link as a reserved message type (no spare UART). |
| D7 | PDU: skeleton component only. The code in `pdu_comm/` is **out of date** — the PDU is being actively refactored in a separate repo. Build `EpsService`/`EpsAdapter_Artemis` as typed skeletons with the port shapes from `pdu_protocol.h`, and integrate the real protocol implementation when the refactored PDU code lands. Do not port `pdu_comm.ino` as-is. |
| D8 | Mission/service/adapter layering and component names follow `docs/OPTIMAL_FPRIME_COMPONENT_TOPOLOGY_PLAN.md`; the only new component is `PayloadAdapter_C3MThermal`. |

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
| `pdu_comm/` test utility | **Out of date** — PDU is being refactored in a separate repo (D7). `EpsService`/`EpsAdapter_Artemis` stay skeletons until that lands |

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

**UART contention — verified against the Artemis User's Manual (April 2026): there is
exactly ONE Pi↔Teensy UART, and a second one cannot be added.** The constraint is on both
ends:

- *Pi side:* the Pi Zero W 40-pin header exposes a single UART pin pair (GPIO14/15,
  physical pins 8/10 — the "UART6" nets in the manual's RPi header table). The demo's
  `thermal_camera_controller.py` line 48 uses exactly this: `/dev/serial0`. The Pi's one
  USB OTG port is occupied by the thermal camera, so a USB-serial adapter is not an option
  either.
- *Teensy side:* on the Artemis OBC every UART-capable pin pair is already consumed:
  Serial1 (pins 0/1) = PDU UART, Serial2 (pins 7/8) = Pi link (used by both the EPSCoR
  demo and the N2 relay firmware), Serial7 (pins 28/29) = GPS, Serial8 (pins 34/35) =
  external breakout connector (routed off-board, not to the Pi). Serial3/Serial5 pins
  (14/15/20/21) are the TMP36 analog inputs AIN0/1/3/4; Serial4 pins (16/17) carry the
  Pi I2C; Serial6 pins (24/25) carry the Teensy I2C bus (`Wire2`, INA219s).

**Therefore: share the single UART with a commanded half-duplex mode switch.** This is
fine because the UART is not the bottleneck — the radio is. Moving the 38,400-byte image
Pi→Teensy at 115200 baud takes ~3.5 s (demo-measured); the RF downlink of ~854 packets
takes minutes. Sequence: `START_PAYLOAD_DOWNLINK` → relay firmware switches the UART to
bulk mode → Pi sends the `DE AD BE EF`-framed image (~3.5 s, CCSDS tunnel paused) → relay
returns to tunnel mode → Teensy transmits payload packets over RF while the UART carries
live progress telemetry again. The telemetry blackout is only the ~3.5 s handoff, and
raising the baud (Teensy 4.1 and the Pi PL011 both handle 921600; the demo code notes
this) shrinks it under 1 s. A channel-tagged mux on the relay's existing framing is the
later polish if even that gap matters.

Two wired alternatives exist on the OBC but are not recommended for MVP: the Pi↔Teensy
SPI0 link (manual pins 10–13; Teensy-as-SPI-slave is poorly supported on the 4.1) and the
Pi↔Teensy I2C (slow, Teensy-as-slave). Note them as future options only.

**Hardware-revision caveat (informational only — per D5, assume current v4.24 wiring is
fine):** the manual contains two different OBC pinouts. Exact references in
`docs/Artemis User's Manual - April 2026.txt` (ctrl+f these strings):

- `"OBC version 4.24 pinouts"` — the RFM23BP wiring table that matches the demo and N2
  firmware exactly (CS=38, NIRQ=40, RX_ON=30, TX_ON=31, SPI1 on 26/27/39). This is the
  hardware we have.
- `"Teensy 4.1 Pin Configuration"` — caption of the manual's main OBC pin table, which
  describes a newer modular-radio revision: ctrl+f `"Modular Radio Reset GPIO"` shows
  pin 7 reassigned to radio reset (and pin 8 to `"Modular Radio DIO0"`), which would
  conflict with Serial2.
- `"UART6 RX"` — the RPi 40-pin header table, showing the Pi's single UART (physical
  pins 8/10, GPIO14/15) as the only UART net to the Pi.

If a future kit ships with the modular-radio revision, re-verify the Pi UART routing
before reusing this plan's firmware. Until then, ignore this.

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
| `d` | dump RF23 RX FIFO (debug) | stays a Teensy-local debug hook in the relay firmware |
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

**An entire ad-hoc channel disappears:** today every satellite print statement travels as
`0xAA` "serial message" RF packets (with a continuation-flag bit, `SAT> ` prefixes, and
chunking), and the PC CLI runs a raw-buffer scanner to demux that text from binary `WRM!`
stream frames and CSV blocks on one serial stream — a recurring source of fragility. In
the refactor all of it is replaced by typed F´ events and channels; the only remaining
non-CCSDS traffic is the payload bulk packets, which are cleanly tagged by packet type.

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
  data chunks + CRC16 (CCITT-FALSE, poly 0x1021, init 0xFFFF), end packet, `0xBB` retry
  bitmap handling — byte-identical on air. Preserve the protocol quirks exactly: the
  retry-completion end packet reports the *resent* count (not the total), and `imageLength`
  is a `uint16` (payloads >65,535 bytes need a protocol rev — flag, don't silently extend).
- Add the half-duplex UART mode switch to the relay firmware (tunnel mode ↔ `DE AD BE EF`
  bulk-receive mode, entered on command, exited on end-marker or timeout — reuse the demo's
  15 s header / 30 s payload / 1 s end timeouts); add the Pi-side sender in
  `PayloadService`/`TeensyTransportService` (pause tunnel, frame, send, resume).
- Extend `GDS_Teensy` to classify payload packets and forward them tagged over USB
  alongside the raw CCSDS stream (reuse the demo's `WRM!`-style binary tagging idea).
- Write `payload_downlink_helper.py` on the PC: missing-packet bitmap, retry requests,
  reconstruction, CRC verify, CSV export, viewer — ported from the demo ground CLI +
  ground-Teensy reassembly logic (logic already exists; it moves from C++ on a Teensy to
  Python on the PC where it is testable). Keep the demo's verified retry behavior:
  2,500 ms grace after the end packet, 5,000 ms re-request timeout, max 2 retry rounds,
  skip auto-retry when ≤10 packets are missing (manual-inspection threshold), and up to 3
  bitmap requests per round each covering 360 packets starting at the first missing index.
- Add downlink progress telemetry through the control plane; `MissionManager` enters
  `ScienceTx` mode and throttles telemetry during transfer.
- Exit criteria: full demo sequence via GDS — schedule collection, watch mode transitions
  and progress telemetry, image reconstructs on PC with matching CRC, CSV matches the
  golden-format output from Phase 0.

### Phase 4 — Sensors and SOH (1–2 weeks, parallelizable with Phase 3)
- Port TMP36/INA219/GPS/IMU polling from the demo .ino into clean firmware modules; emit a
  compact binary sensor-status sideband as a reserved message type on the relay link (D6).
- Map into `EpsService`/`GpsService` telemetry; `SoHManager` aggregates the judge-facing
  health page.
- Exit criteria: live temperatures, currents, GPS, IMU in GDS channels during BaseMode.

### Phase 5 — Stretch + cleanup
- Livestream mode re-evaluated: it is a third protocol stack of its own (Pi downsamples
  to 80×60 8-bit, `CA FE BA BE` UART frames on request/response `FRAME\n` flow control,
  ~107 best-effort RF packets per frame with no CRC, ground Teensy outputs a 4,808-byte
  `WRM!` binary frame at ≥102 packets received, matplotlib viewer in a separate process).
  Likely stays a special relay mode triggered by an F´ command, or is dropped — it is demo
  candy, not mission-critical, and it conflicts with the tunnel for both UART and air time.
- Retire duplicated code: the EPSCoR satellite/ground .ino monoliths are now fully
  superseded; `espcor_teensy_demo/` is marked reference-only (it already is in README).
- Write the "new mission HOWTO": *to fly a new payload on the Artemis bus, implement
  `PayloadAdapter_<YourPayload>` against `PayloadService`'s ports and you are done.* This
  document is the deliverable that proves the reuse story for future teams.
- PDU (per D7): the code in `pdu_comm/` is **out of date** — the PDU is being refactored
  in a separate repo. Keep `EpsService`/`EpsAdapter_Artemis` as typed skeletons. Use
  `pdu_protocol.h` only as a shape reference for the port types (switch IDs incl. the
  `RPI` power switch — the eventual proper home for Pi power control instead of Teensy
  GPIO 36; Teensy `Serial1`; ASCII-offset encoding), and swap in the refactored protocol
  implementation when it lands.

## 6. Risks and open questions

| Risk | Mitigation |
| --- | --- |
| Single shared RFM23BP: telemetry vs. payload packets fight for air time | Explicit `ScienceTx` mode throttles control plane during bulk transfer (Phase 3); already anticipated in comms plan |
| Single Pi↔Teensy UART shared by CCSDS tunnel and payload bulk transfer (verified: no second UART exists on either side — see §3.3) | Commanded half-duplex mode switch; blackout is only the ~3.5 s UART handoff; raise baud toward 921600 to shrink it; channel-tagged mux as later polish |
| Pi Zero W performance with F´ + camera capture | Already validated F´ on Pi Zero W (ARMv6 cross-build, `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`); capture is burst CPU, schedule capture outside telemetry-heavy moments; 2a child-process isolates camera memory |
| Placeholder `Svc.Ping` ports across mission/service components mean "wiring exists, semantics don't" | Phase 2 defines the real FPP types once, on the payload path first — the highest-value path — before generalizing |
| Retry protocol moves from ground Teensy (C++) to PC helper (Python) — subtle behavior drift | Golden transcripts from Phase 0; keep retry constants identical (2.5 s grace, 5 s timeout, 2 rounds, ≤10-missing threshold, 3×360-packet bitmaps); replicate the resent-count end-packet quirk; unit-test bitmap handling against recorded packet logs |
| UART mode-switch deadlock (relay stuck in bulk mode if Pi dies mid-transfer) | Reuse the demo's proven timeouts (15 s header / 30 s payload / 1 s end) as the bulk-mode watchdog; always fall back to tunnel mode |
| Team bandwidth / brittle hardware time | Phases 1 and 4 are low-risk and parallelizable; Phase 3 is the only genuinely new integration and gets the buffer |

Open questions to settle before Phase 3:
1. Baud is decided (D1: start at 115200). Optional tuning: measure 460800/921600 error
   rate on the actual harness if the ~3.5 s handoff blackout bothers anyone (demo code
   asserts 921600 is fine on Teensy 4.1).
2. Decide where reconstructed images/CSVs live on the PC (GDS plugin vs. standalone helper
   window) for the judge-facing display.
3. Sensor sideband (D6 decides the transport): confirm the relay segmentation header has
   room for a channel/message-type ID, or define a new reserved type alongside the
   existing segment format.

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
