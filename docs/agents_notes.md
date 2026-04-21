# agents_notes.md

## Project Snapshot (2026-02-13)

This repo is the Neutron 2 team F' integration workspace:
- F' flight software runs on Raspberry Pi.
- Satellite Teensy provides UART wrapper + RF23BP bridge.
- Ground Teensy reassembles RF messages to USB and supports simple USB-burst uplink back to RF.

## Demo Target Snapshot (2026-04-07)

The current top-level target is the shortened FlatSat FSR end-to-end demo shown in the team's planning slides. Treat this as the active demonstration narrative when making architecture, implementation, or documentation decisions.

### Demo story to support

1. System boots in `Base Mode`.
2. Operator uses `D2S2` planning inputs to determine the mock ground-pass contact duration.
3. During the simulated pass, the satellite remains in base mode and downlinks `SOH`/health telemetry for a live judge-facing display.
4. Operator sends a command to schedule data collection after a short delay, for example `10` seconds.
5. Flight software executes a data-collection action using payload data; simulated or temporary payload data is acceptable for the demo if the real payload path is not ready.
6. After collection, the system transitions into a science downlink path and sends payload/science data to the ground side.
7. Ground software on the laptop reviews, displays, or analyzes the downlinked science data. `fprime-gds` is the MVP demo tool and default ground interface for this phase. `Yamcs` is the longer-term end-goal ground presentation and analysis stack.

### What matters most for the demo

- A visible `Base Mode` / nominal-state bring-up.
- Live `SOH` telemetry reaching the ground station.
- An explicit scheduled command from the operator.
- A visible state transition into data collection.
- A visible result payload or science product, even if simulated.
- A visible science-data review step on the ground PC.

### Compression rule

- Real orbital timing is not the target for this demo.
- Time between "pass start", scheduled collection, and science downlink may be compressed to fit the live presentation.
- A `10`-second schedule delay is an acceptable operator-facing placeholder.

## Current State

### 1) F' side (`ArtemisRpiTeensy_N2`)
- Deployment uses Linux UART transport (`Drv.LinuxUartDriver`) on `/dev/serial0`.
- Main runtime binary accepts `-d <uart_device>`.
- MVP custom components in deployment:
  - `Components/TeensyLink`
  - `Components/PingResponder`
- Build status:
  - `fprime-util generate -f` passes
  - `fprime-util build` passes

### 1b) Raspberry Pi Build Path
- Source-of-truth runbook:
  - `rpi_build.instructions`
- Confirmed compatibility finding (2026-02-20):
  - Target hardware: `Raspberry Pi Zero W Rev 1.1` (`armv6l`)
  - ARMv7 binaries fail on Pi Zero W with `Illegal instruction`
  - Use native build on the Pi as source-of-truth runtime binary
- Cross-build update (2026-03-06):
  - Docker cross-build is now validated for Pi Zero W
  - Verified ARMv6 output using `file` and `readelf`
  - Verified remote smoke test on `/dev/null`
  - Detailed handoff:
    - `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`
  - Student guide:
    - `docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`

### 2) Satellite Teensy (`ArtemisTeensy_N2_Baremetal`)
- Source of truth:
  - `firmware/satellite_teensy/satellite_teensy.ino`
  - `firmware/satellite_teensy/src/relay_uart_rf.*`
  - `firmware/satellite_teensy/src/rf23_driver.*`
  - `firmware/satellite_teensy/src/link_protocol.hpp`
  - `firmware/satellite_teensy/src/link_counters.hpp`
- Transport behavior:
  - Default HIL path is transparent raw-byte bridge:
    - RPi UART raw bytes -> RF segment transport -> ground USB raw bytes
    - ground USB raw bytes -> RF segment transport -> RPi UART raw bytes
  - UART wrapper mode (`0xD4 0xC3 + len + crc16`) remains optional fallback only
  - UART payload max now `220` bytes
  - RF uses segmented message transport (`msg_id`, `seg_idx`, `seg_count`, `chunk_len`)
- Build status:
  - `./tools/arduino-cli/build.sh` passes for `teensy:avr:teensy41`

### 3) Ground Teensy (`GDS_Teensy`)
- Source of truth:
  - `firmware/gds_teensy/gds_teensy.ino`
  - `firmware/gds_teensy/src/relay_uart_rf.*`
  - `firmware/gds_teensy/src/rf23_driver.*`
- Behavior:
  - Receives RF segments and reassembles full message bytes
  - Streams reassembled bytes directly to USB serial (`Serial`) for laptop GDS UART input
  - Also packetizes raw USB byte bursts (`8 ms` idle flush or `220`-byte full buffer) for simple uplink
- Build status:
  - `./tools/arduino-cli/build.sh` passes for `teensy:avr:teensy41`

## Important Clarification: Framing

- End-to-end payload is still opaque F' bytes.
- Endpoints (GDS and F' app) use `ComCcsds` framing (`space-packet-space-data-link`).
- Teensy bridge default for HIL is transport-only and should not add an extra UART frame format in the main path.
- RF transport is now segmented and reassembled before UART egress.
- Contract documentation:
  - `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`
  - `GDS_Teensy/docs/transport_contract.md`

## HIL Framing Alignment (2026-04-15)

- Root issue:
  - ground Teensy was in raw mode while satellite Teensy was still defaulting to framed UART mode.
- Fix applied:
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/satellite_teensy.ino`
  - relay config now matches ground bridge for raw-byte tunnel mode:
    - `RelayConfig{true, false, false, false, 8}`
- Intended runtime contract:
  - laptop `fprime-gds` UART plugin sends raw bytes
  - ground Teensy relays raw bytes over RF segments
  - satellite Teensy reassembles and forwards raw bytes to RPi UART
  - RPi F' deployment handles the CCSDS/space-packet framing at endpoint level

## Local Emulation Findings (2026-04-08)

### Root Cause

- Local GDS was launched with the wrong framing (`fprime`) for this deployment.
- This deployment is wired through `ComCcsds`; local GDS must use:
  - `--framing-selection space-packet-space-data-link`
- Symptom when wrong framing is used:
  - GUI opens, but charts stay flat/empty
  - `GDS Error Log` shows endpoint errors (`channels`, `events`, `command_history`, etc.)
  - transport bytes may still flow, so it can look like link is "alive" but undecodable

### What Was Updated

- `ArtemisRpiTeensy_N2/tools/local_emulation_loop.py`
  - default framing changed to `space-packet-space-data-link`
- `ArtemisRpiTeensy_N2/tools/run_gds_uart.sh`
  - default framing changed to `space-packet-space-data-link`
  - added explicit `--framing <mode>` override
- `EMULATION.md`
  - manual GDS example updated to `space-packet-space-data-link`

### Build + Launch (Known-Good)

1. Build:
   - `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
   - `cd ArtemisRpiTeensy_N2`
   - `fprime-util generate -f`
   - `fprime-util build`
2. Launch local closed-loop emulation:
   - `./tools/run_local_emulation.sh`
3. Open:
   - `http://127.0.0.1:5050`

### Landmines

- Stale `fprime-gds`/Flask processes can survive interrupted runs and hold old ports/config.
- If port behavior looks inconsistent (`5050` works, another port fails, or old UI state remains):
  - kill stale local emulation + GDS processes
  - relaunch one clean instance only
- If charts are empty but app logs show active events:
  - verify framing first; do not assume transport is broken.

### Framing Guidance (Decision Rule)

- `space-packet-space-data-link` is stock F' `ComCcsds` framing, not custom framing.
- The custom Teensy UART wrapper (`0xD4 0xC3 + len + crc16`) is a separate link-layer mechanism.
- For demo and local/GDS validation, use `ComCcsds` framing as the primary/default path.
- Keep custom UART wrapper support only as an optional hardware fallback mode when physical-link behavior requires it.

## Laptop GDS <-> Ground Teensy USB Debug Chain (2026-04-20)

### Scope

- This section is for the `GDS_Teensy` debug sketch:
  - `GDS_Teensy/firmware/gds_usb_raw_dump/gds_usb_raw_dump.ino`
- Goal is quick validation of both directions:
  - `fprime-gds -> Teensy` uplink bytes (visible as `U0[...]`)
  - `Teensy -> fprime-gds` downlink bytes (visible as `D1[...]` or `RS[...]`)

### Known-good GDS launch

- Use GUI port `5050` and `ComCcsds` framing:
  - `fprime-gds -n --dictionary <...TopologyDictionary.json> --communication-selection uart --uart-device /dev/cu.usbmodemXXXX --uart-baud 115200 --uart-skip-port-check --framing-selection space-packet-space-data-link --gui-port 5050`
- Wrong framing (`fprime`) reproduces classic failure signature:
  - GDS pages load, but channels/events endpoints error and charts remain flat.

### Critical landmine: stale Arduino upload artifact

- Root cause seen during debug:
  - `arduino-cli upload` was reusing old build artifacts, so new replay bytes were not actually flashed.
- Reliable fix:
  1. compile with explicit `--build-path`
  2. upload using the same `--build-path` (or `--input-dir` that points to that folder)
- Known-good command pattern from `GDS_Teensy`:
  - `export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"`
  - `arduino-cli compile --clean --fqbn teensy:avr:teensy41:usb=serial2 --build-path build/arduino-cli-gds-usb-raw-dump firmware/gds_usb_raw_dump`
  - `arduino-cli upload -v --fqbn teensy:avr:teensy41:usb=serial2 -p /dev/cu.usbmodemXXXX --build-path "$PWD/build/arduino-cli-gds-usb-raw-dump" firmware/gds_usb_raw_dump`
- If behavior does not match edited sketch bytes, assume stale artifact first.

### What to expect in GUI and logs

- `GDS -> Teensy` path proven when sending any command (for example `missionManager.PING`) and seeing `U0[...]` hex lines on `SerialUSB1`.
- `Teensy -> GDS` requires actual downlink bytes:
  - from `Serial1` passthrough (`D1[...]`) or
  - from replay injector (`RS[...]`) in the debug sketch.
- Replay mode is static captured telemetry, not command-aware:
  - do not expect a deterministic `MissionManager.Pong` token match from replay alone.
  - expect decoded events/channels from the captured stream (for example version/comms/link events).

### Quick failure triage

1. If `U0[...]` appears but GDS events/channels stay empty:
   - problem is downlink source (no valid bytes returning), not uplink.
2. If downlink bytes are present but GDS shows endpoint errors:
   - verify `--framing-selection space-packet-space-data-link`.
3. If edited sketch behavior does not change after upload:
   - rebuild and upload with the same explicit build directory.
4. If port behavior is inconsistent:
   - stop stale `fprime-gds` or emulation processes and relaunch one clean instance on `5050`.
5. If using `usb=serial2` and two `/dev/cu.usbmodem*` ports appear:
   - one port is GDS data (`Serial`), the other is debug monitor (`SerialUSB1`); verify by checking where `U0[...]` lines appear.

## Adapter MVP Update (2026-04-15)

### What changed

- `GpsAdapter_Artemis` is no longer a static `+400` key offset stub.
  - It now models a deterministic GPS fix state machine and emits:
    - `FixState` (`0=no-fix`, `1=acquiring`, `2=2D`, `3=3D`)
    - `SatellitesTracked`
    - `FixQualityScore`
    - `RequestCount`
  - `statusOut` now reports normalized fix-state keys (`0..3`), so `GpsService.GpsFixState` is chart-friendly.
- `CommsAdapter_TeensyRfm23` is no longer a static `+500` key offset stub.
  - It now models deterministic RFM23 link behavior and emits:
    - `LinkState` (`0=down`, `1=acquiring`, `2=locked`, `3=degraded`)
    - `RssiDbm`
    - `RfRxPackets`
    - `RfTxPackets`
    - `RfTxDrops`
    - `RequestCount`
  - Output port behavior is now split intentionally:
    - `statusOut[0]` -> normalized link state for `CommsManager`
    - `statusOut[1]` -> `RfRxPackets` snapshot for `TeensyTransportService` downlink counter visibility

### Why this was needed

- Static offset stubs produced unrealistic telemetry values and made chart interpretation weak.
- Demo path needed visibly changing, semantically meaningful telemetry for GPS and comms subsystems without breaking current topology wiring.

### Demo-visible channels/events to watch

- GPS:
  - `ArtemisRpiTeensyDeployment.gpsAdapterArtemis.FixState`
  - `ArtemisRpiTeensyDeployment.gpsAdapterArtemis.SatellitesTracked`
  - `ArtemisRpiTeensyDeployment.gpsAdapterArtemis.FixQualityScore`
  - `ArtemisRpiTeensyDeployment.gpsService.GpsFixState`
  - Event: `ArtemisRpiTeensyDeployment.gpsAdapterArtemis.FixStateChanged`
- Comms:
  - `ArtemisRpiTeensyDeployment.commsAdapterTeensyRfm23.LinkState`
  - `ArtemisRpiTeensyDeployment.commsAdapterTeensyRfm23.RssiDbm`
  - `ArtemisRpiTeensyDeployment.commsAdapterTeensyRfm23.RfRxPackets`
  - `ArtemisRpiTeensyDeployment.commsManager.LinkState`
  - `ArtemisRpiTeensyDeployment.teensyTransportService.DownlinkFrames`
  - Event: `ArtemisRpiTeensyDeployment.commsAdapterTeensyRfm23.LinkStateChanged`

### Remaining gap (important)

- These two adapters are now mission-meaningful but still model-driven.
- They are not yet parsing live hardware status lines (for example Teensy `#LINK_STATUS` response fields or raw GPS sentence/fix data).
- Full hardware-backed adapter ingestion remains a follow-on item after the MVP demo chain is stable.

## Architecture Decision (2026-02-26)

- Evaluated running F' on Teensy 4.1 via Zephyr reference as an option.
- Team decision: keep Teensy nodes baremetal for MVP.
- Rationale:
  - Avoid additional Zephyr/F' integration risk during MVP schedule.
  - Current Teensy role is bridge/transport and does not require on-node F' autonomy.
  - Final flatsat hardware plan also does not require migrating Teensy nodes to F' at this time.

## OBC Demo Notes (2026-04-07)

- Treat the Artemis OBC as a split system for the demo:
  - `Raspberry Pi` = high-level flight/demo logic, payload handling, storage, and ground-facing behavior
  - `Teensy` = EPS/PDU interface, analog housekeeping, reset/power supervision, and other low-level control
- Current payload assumption for ideation:
  - `Neutron 1 Payload Board` connects to the Raspberry Pi through a USB serial adapter
  - Do not assume the Artemis camera is the active demo payload
- Demo-oriented recommendation:
  - strongly consider wiring `RFM23BPS` directly to the Raspberry Pi to remove the extra `RPi -> UART -> Teensy -> radio` bridge path
  - this better matches the likely long-term direction of wiring the `SatNOGS` radio directly to the Raspberry Pi
- If `RFM23BPS` is moved Pi-direct:
  - use Raspberry Pi SPI/GPIO for radio logic/control
  - do not power the radio from Raspberry Pi header pins
  - use switched OBC/PDU power for radio power and maintain common ground with the Raspberry Pi
- Keep the Teensy in the system even if radio moves Pi-direct:
  - PDU UART/control path
  - PDU current/power sensing over I2C
  - analog temperature channels
  - Pi enable/reset supervision
- Manual caveat:
  - some OBC pin tables conflict with the later radio pin mapping, so the physical board should be treated as source of truth before declaring Teensy pins "free"
- End-of-year demo rule:
  - prioritize a stable visible story over architecture completeness
  - acceptable flow is `Base Mode -> live SOH -> scheduled collect -> science product -> downlink -> ground review`
  - use simulated or fallback payload/science data if it materially improves demo reliability

## RPi-Teensy Service Path Decision (2026-04-15)

- Keep the existing `RPi <-> Teensy` UART path dedicated to raw `ComCcsds` packets only.
- Do not inject custom Teensy service RPC/control bytes into that same UART CCSDS stream for MVP.
- For Teensy-owned PDU telemetry/control (for example analog temperatures, INA219 current/power, switch commands), use a sideband bus between Raspberry Pi and Teensy.
- Recommended sideband for MVP: `I2C` (`RPi` master, `Teensy` slave with a small register/command map).
- Acceptable alternates if wiring or latency requires it:
  - `SPI` sideband
  - `GPIO` handshake/interrupt line in addition to `I2C`/`SPI`
- Manual caveat remains in force:
  - pin naming/labeling in the Artemis manual has conflicts; verify against board wiring/continuity before final pin assignment.
- F' integration implication:
  - keep `LinuxUartDriver` for the CCSDS link
  - add service-facing adapter/driver path separately (for example `LinuxI2cDriver`) when implementing real Teensy/PDU ingestion
- If no extra sideband wiring is available, defer to post-MVP single-UART multiplexing only after the CCSDS chain is stable.

## Primary TODO

1. Record first successful non-crashing runtime on `/dev/serial0` using the real UART path.
2. Run full HIL end-to-end tests with real `fprime-gds` UART traffic over RF (both directions).
3. Implement the minimum demo-state flow for `Base Mode` -> scheduled data collection -> science-data downlink.
4. Decide and document the payload-data source for the demo: real payload path vs simulated temporary data.
5. Keep `fprime-gds` as the live MVP demo ground interface and treat `Yamcs` as the post-MVP target presentation/analysis stack.
6. Add minimal segment ACK/retry for RF relay reliability after transparent raw-byte path is stable.
   - MVP target: command uplink delivery confidence and reduced telemetry burst loss during demo.
7. Add deterministic packet boundary extraction for uplink beyond simple burst mode if required by the selected demo flow.
8. Build post-MVP mission/service multiplexing only after chain stability.
9. Complete real file downlink path for the science demo flow:
   - current `REQUEST_SCIENCE_DOWNLINK` path is handshake-only (events/channels)
   - wire an actual `Svc::FileDownlink` transfer for science products
   - pass criteria: file transfer visible in GDS `#Downlink`, file can be downloaded locally, content/size matches expectation

## Important Paths

- F' project root:
  - `ArtemisRpiTeensy_N2`
- Satellite Teensy project:
  - `ArtemisTeensy_N2_Baremetal`
- Ground Teensy project:
  - `GDS_Teensy`
- Quick test guide:
  - `docs/GET_STARTED_TESTING.md`
- Build runbook:
  - `docs/build_runbook.md`
- Raspberry Pi native build runbook:
  - `rpi_build.instructions`

## Agent Reminders

- Prefer retrieval-led reasoning for F' tasks (per `AGENTS.md`).
- Activate venv before F' commands:
  - `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
- Prefer fresh configure before topology/component changes:
  - `fprime-util generate -f`
- Use project-pinned versions from `lib/fprime/requirements.txt` when creating venvs on target.
- When choosing scope, favor the shortest implementation that makes the end-to-end demo story credible in front of judges.
- Simulated payload data is acceptable if it unblocks the demo and is documented clearly.
