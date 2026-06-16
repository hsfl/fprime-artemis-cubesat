# agents_notes.md

## Project Snapshot (2026-02-13)

This repo is the Neutron 2 team F' integration workspace:
- F' flight software runs on Raspberry Pi.
- Satellite Teensy provides transparent raw-byte UART tunnel mode plus RF23BP segmentation/reassembly.
- Ground Teensy reassembles RF messages to USB raw bytes and supports simple USB-burst uplink back to RF.
- Legacy UART wrapper mode remains fallback-only.

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
  - `Components/PingResponder`
  - `Components/CommsAdapter_TeensyRfm23`
  - `Components/TeensyTransportService`
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
  - Queue-backed relay path enabled for burst tolerance:
    - `RAW_UART_FLUSH_MS = 12`
    - `UPLINK_QUEUE_DEPTH = 32`
    - `DOWNLINK_QUEUE_DEPTH = 32`
    - relay max queue cap `MAX_QUEUE_DEPTH = 32`
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
  - Packetizes raw USB byte bursts (`12 ms` idle flush or `220`-byte full buffer) for uplink
  - Queue-backed relay path enabled for burst tolerance:
    - `UPLINK_QUEUE_DEPTH = 32`
    - `DOWNLINK_QUEUE_DEPTH = 32`
    - relay max queue cap `MAX_QUEUE_DEPTH = 32`
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
    - `RelayConfig{true, false, false, false, RAW_UART_FLUSH_MS, UPLINK_QUEUE_DEPTH, DOWNLINK_QUEUE_DEPTH}`
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

## Queue Buffering Update (2026-04-21)

### What changed

- Mirrored queue-backed raw relay behavior on both Teensy projects:
  - `GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.*`
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.*`
- Queues now decouple:
  - `UART -> RF` (uplink queue)
  - `RF -> UART` (downlink queue)
- Both sketches now use the same generous demo defaults:
  - `RAW_UART_FLUSH_MS = 12`
  - `UPLINK_QUEUE_DEPTH = 32`
  - `DOWNLINK_QUEUE_DEPTH = 32`

### Why this matters

- Prevents immediate drops during short command/telemetry bursts when one side is briefly busy.
- Keeps the raw `ComCcsds` tunnel model unchanged while improving burst handling.

### Debug/observability

- `#LINK_STATUS` now includes queue drop counters:
  - `up_q_drops`
  - `down_q_drops`
- If either counter increments during demo traffic, first response is:
  1. reduce offered traffic burst rate, or
  2. increase flush window slightly (for example `12 -> 16 ms`) to improve batching.

### Build verification (known-good)

1. Ground Teensy:
   - `cd GDS_Teensy`
   - `./tools/arduino-cli/build.sh`
2. Satellite Teensy:
   - `cd ArtemisTeensy_N2_Baremetal`
   - `./tools/arduino-cli/build.sh`

### Memory landmine

- Queue buffers consume `RAM`, not flash.
- Approximate queue payload RAM per Teensy relay:
  - `2 queues * 32 entries * (220-byte payload + 2-byte length) ~= 14.2 KB`
- Teensy 4.1 remains within RAM headroom with this profile (validated by successful builds), but do not increase queue depth blindly without re-checking RAM report.

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

## Future Subsystem Submodule Plan (2026-06-15)

- Long-term integration direction: the top-level F' repo should pin whole subsystem implementation repos as submodules, not depend on a tiny protocol-constants-only repo as the main source of truth.
- Intended shape:
  - `ArtemisRpiTeensy_N2/` remains the active F' deployment and mission-facing component workspace.
  - `external/pdu-firmware/` can be a submodule pointing at the ATSAME51 PDU firmware repo, including firmware implementation, ICD, protocol header, Teensy/bench tooling, and notes.
  - `external/satnogs-radio/` can be added once the SatNOGS dev-board repo is real and should include radio firmware/protocol, MTU/data-budget constraints, setup scripts, and bench/test tools.
  - `external/payload/` can be added once the real payload-board repo exists. Until then, keep the RPi-hosted emulated payload adapter local to this repo.
  - `external/<other-subsystem>/` can be added later for other subsystem firmware/tools when the F' side needs implementation context.
- Rationale:
  - F' component and adapter work often needs actual subsystem behavior, not just enum values or packet constants.
  - Keeping firmware, ICD, bench scripts, and history together reduces drift between implementation, test tooling, and the F' adapter.
  - The F' repo can pin a known-good subsystem revision by submodule commit for demos and flight-like integration.
  - Student developers should not have to chase a constants-only repo plus a separate implementation repo to understand what is real today.
- Ownership rule:
  - The PDU firmware repo owns `pdu_protocol_v2.h`, `PDU_PROTOCOL_ICD.md`, the PDU implementation, and bench/test tooling.
  - This F' repo owns mission-facing EPS/PDU components, adapters, topology wiring, and the pinned subsystem revisions.
  - Do not treat the legacy `artemis-cubesat-protocols` protocol-only submodule as the ground truth for the PDU v2 runtime contract.
- Submodule decision rule:
  - Add a submodule only if the external repo owns firmware source, ICD/protocol docs, hardware test scripts, or release history that F Prime must pin to a known-good revision.
  - Do not add a submodule for only constants, copied headers, one markdown doc, or speculative future code.
  - Current priority is PDU first, SatNOGS second, payload only when real, and ADCS/GPS/IMU/thermal only if they become standalone firmware/tooling repos.
  - For the base case, default to RFM23BP and RPi-emulated payload, keep data products slim, and increase data budget only after SatNOGS hardware is available and validated.

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

## Design Plan Pointer (2026-06-09)

- New plan doc for the remaining payload-downlink work and the long-term radio-swap architecture:
  - `docs/RADIO_AGNOSTIC_COMMS_AND_PAYLOAD_DOWNLINK_PLAN.md`
- Read it before touching payload downlink, the Teensy link protocol, or comms topology.
- Core decisions captured there:
  - payload bulk data moves on a second stateless virtual channel over the existing RF bridge (per-frame channel tags, no link mode switching)
  - GDS keeps a byte-pure CCSDS stream on channel 0 (GUI cannot break)
  - all radio MTU knowledge is isolated at a single seam (`LinkCfg` + `link_protocol.hpp`) so the RFM23BP can later be swapped for a 256-byte UART radio or SatNOGS board without touching mission logic
  - custom payload protocol is tactical; graduation criteria to stock `Svc.FileDownlink` are defined in the plan

## Session Handoff (2026-04-23)

### Pi Zero W cross-build and versioning

- Revalidated Pi Zero W cross-build locally in Docker with ARMv6 verification.
- `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md` remains the source-of-truth rationale for sysroot/runtime object handling.
- Updated script:
  - `ArtemisRpiTeensy_N2/tools/docker_cross_compile_pi_zero_w.sh`
- Script updates applied:
  - added `--local-only` mode (skip SSH deploy/smoke and reuse local sysroot)
  - changed container mount to repo root (`/repo`) so F' version generation can see real git metadata
  - switched venv creation to `python3 -m venv --clear` to avoid stale shebang path issues after mount-path changes
- Result:
  - binary remains ARMv6-compatible (`Tag_CPU_arch: v6KZ`, `Tag_FP_arch: VFPv2`)
  - runtime version events no longer fall back to `v3.5.0`; they now report framework from git (`v4.2.1-*`)

### Pi SSH + deployment status

- Pi reachable and verified at:
  - host alias: `artemis-pi`
  - host/IP: `pi@192.168.0.152` (`raspberrypi.local`)
  - architecture: `armv6l`
- New deployment + dictionary copied to:
  - `/home/pi/artemis/cross/ArtemisRpiTeensyDeployment`
  - `/home/pi/artemis/cross/ArtemisRpiTeensyDeploymentTopologyDictionary.json`
- Current symlinks updated:
  - `/home/pi/artemis/current/ArtemisRpiTeensyDeployment`
  - `/home/pi/artemis/current/ArtemisRpiTeensyDeploymentTopologyDictionary.json`

### Auto-start on boot (RPi)

- Created and enabled systemd unit:
  - `/etc/systemd/system/artemis-fprime.service`
- Configured boot command:
  - `/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0`
- Service state at handoff:
  - `enabled`
  - `active (running)`
  - live telemetry/event logs visible via journald
- Useful checks on Pi:
  - `sudo systemctl status artemis-fprime.service --no-pager -l`
  - `sudo journalctl -u artemis-fprime.service -f`

### Smoke launch evidence on Pi

- Executed both with `timeout 8s` directly on Pi:
  - `/dev/null` smoke
  - `/dev/serial0` smoke
- Both returned status `124` (expected timeout), with active logs emitted.
- Log files:
  - `/home/pi/artemis/logs/smoke_null_20260423_115931.log`
  - `/home/pi/artemis/logs/smoke_serial0_20260423_115931.log`
- `/dev/serial0` smoke included version and subsystem events, including:
  - `FrameworkVersion : [v4.2.1-dirty]`
  - `ProjectVersion : [546f0fc-dirty]`
  - ongoing EPS/ADCS/GPS/COMMS activity

### Teensy IDE include fix (header resolution)

Problem seen in Arduino IDE for both Teensy sketches:
- `fatal error: artemis_rf23bp.hpp: No such file or directory`

Fix approach for IDE compatibility:
- keep `artemis_rf23bp.hpp` local to each sketch `src/` directory
- include with quotes (`"artemis_rf23bp.hpp"`) instead of angle brackets

Files updated:
- Ground Teensy:
  - `GDS_Teensy/firmware/gds_teensy/src/rf23_driver.hpp`
  - `GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp` (added)
- Satellite Teensy:
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/rf23_driver.hpp`
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/artemis_rf23bp.hpp` (added)

Build verification at handoff:
- `GDS_Teensy` compile succeeds
- `ArtemisTeensy_N2_Baremetal` satellite compile succeeds

### Immediate next step

- Proceed with full HIL validation using current flashed firmware and live `/dev/serial0` service on Pi.
- Use:
  - `docs/build_runbook.md`
  - `docs/GDS_TEENSY_RUNBOOK.md`

## Session Handoff (2026-04-23, HIL debug)

### Startup/boot duplicate fixed on Pi

- Root cause of double-start was two enabled services launching F':
  - `artemis-fprime.service` (intended)
  - `artemis-cross.service` (duplicate path)
- Actions taken on Pi:
  - disabled/stopped `artemis-cross.service`
  - kept `artemis-fprime.service` enabled as single startup path
  - restarted `artemis-fprime.service`
- Confirmed runtime command:
  - `/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0`
- Confirmed process state after fix:
  - one `ArtemisRpiTeensyDeployment` process only

### HIL/RF-chain status (current blocker)

- Pi app is healthy and logs continuously, but comms path is unstable/failing end-to-end:
  - repeated `commsManager LinkStateUpdated ... 0`
  - repeated `commsAdapterTeensyRfm23 RequestHandled ... status=0`
- After duplicate-start fix, link briefly showed up (`LinkStateUpdated ... 1`, `status=1`) right after restart, then returned to failure behavior.
- Direct serial probe on Pi while service stopped captured no UART bytes during sample window (`/dev/serial0` read = 0 bytes in probe run).
- Net result for next HIL session:
  - treat RF/UART chain as current failure point, not F' process bring-up.
  - debug should focus on physical link path and Teensy-side relay/radio chain stability.

## Session Handoff (2026-04-24, RF/GDS HIL debug)

### USB/SSH mapping confirmed

- Satellite Teensy:
  - `/dev/cu.usbmodem115502201`
  - physical Teensy upload port: `usb:100000`
  - current sketch role: satellite bridge, Pi data on `Serial2`, USB `Serial` debug counters
- Ground station Teensy:
  - `/dev/cu.usbmodem115551201` = GDS data stream (`Serial`)
  - `/dev/cu.usbmodem115551203` = debug stream (`SerialUSB1`)
  - physical Teensy upload port: `usb:1100000`
- Raspberry Pi:
  - SSH alias: `artemis-pi`
  - host/IP: `192.168.0.152`
  - key in `~/.ssh/config`: `~/.ssh/id_ed25519_artemis_pi`
  - verified active service:
    - `artemis-fprime.service`
    - runtime command: `/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0`

### Important upload lesson

- Do not upload by `/dev/cu.usbmodem*` when both Teensys are connected; Arduino CLI may auto-search and choose the wrong Teensy.
- Use the physical Teensy ports from `arduino-cli board list`:
  - ground: `-p usb:1100000`
  - satellite: `-p usb:100000`
- Use explicit build directories to avoid stale artifacts:
  - ground debug: `GDS_Teensy/build/arduino-cli-gds-teensy-debug`
  - satellite debug: `ArtemisTeensy_N2_Baremetal/build/arduino-cli-satellite-debug`

### Firmware debug instrumentation added

- Ground `gds_teensy.ino` now supports `usb=serial2`:
  - `Serial` remains byte-clean for `fprime-gds`
  - `SerialUSB1` prints periodic bridge counters and RF init status
- Satellite `satellite_teensy.ino` now prints periodic bridge counters on USB `Serial`.
- Relay debug counters used:
  - `uart_rx`, `uart_tx`
  - `rf_rx_pkt`, `rf_tx_pkt`
  - `rf_rx_msg`, `rf_tx_msg`
  - `rf_rx_seg`, `rf_tx_seg`
  - `rf_tx_drops`, `rf_reasm_drops`
  - `up_q_drops`, `down_q_drops`

### RF bridge findings

- Ground RF init reports OK:
  - `[GDS_Teensy] RF23 bridge ready (raw USB byte tunnel + RF segmentation)`
- Satellite RF bridge reports OK when boot text is captured.
- Ground-to-satellite uplink was proven with a raw test burst:
  - writing `CODX-UPLINK-TEST\n` to ground data port incremented:
    - ground `uart_rx`
    - ground `rf_tx_pkt/msg/seg`
    - satellite `rf_rx_pkt/msg/seg`
    - satellite `uart_tx`
- Satellite-to-ground downlink was proven with live F' bytes:
  - satellite `uart_rx` and `rf_tx_*` continuously increment
  - ground `rf_rx_*` and `uart_tx` continuously increment
- F' command path was proven:
  - sent `ArtemisRpiTeensyDeployment.missionManager.PING --arguments 4242`
  - Pi journald showed:
    - `OpCode 0x10006001 dispatched`
    - `MissionManager pong token=4242 count=1`
    - `OpCode 0x10006001 completed`

### Remaining blocker

- `fprime-gds` still prints repeated:
  - `[WARNING] Checksum validation failed.`
- This means the MVP command uplink is working and the raw bridge moves bytes both ways, but GDS-visible clean decode is not yet stable.
- Earlier ground counters showed `rf_reasm_drops` during continuous telemetry when 220-byte raw UART batches were segmented into multiple RF packets.
- Mitigations tried:
  - added `RF_INTER_SEGMENT_GAP_MS = 8`
  - changed raw UART batching to flush at `RF_SEGMENT_MAX_DATA` so each relay message fits in one RF packet
- After single-packet raw chunks, short counter windows showed clean RF movement with no `rf_reasm_drops`, but GDS checksum warnings still appeared.

### Current root-cause hypothesis

- Highest-probability remaining causes:
  1. byte loss/reordering still occurs under sustained downlink load even when debug windows look clean
  2. `fprime-gds` is decoding a stream that starts mid-frame and is not resynchronizing cleanly
  3. transport chunking is byte-stream transparent in concept, but the current RF relay lacks a stronger stream-order/ack layer and can silently lose RF packets
- The next test should be byte-level, not just counter-level:
  - capture bytes near Pi UART output and ground USB input for the same interval
  - compare length/order/content
  - if bytes differ, root cause is RF/Teensy transport integrity
  - if bytes match, root cause is GDS framing/config or dictionary/runtime mismatch

### Root-cause update after gap-counter instrumentation

- Added `rf_msg_id_gaps` to both Teensy relay counters to detect missing whole RF messages.
- This catches the failure mode that `rf_reasm_drops` misses after raw chunks were reduced to one RF packet each.
- Result from live downlink:
  - satellite side showed continuous `uart_rx` and `rf_tx_msg`
  - ground side showed `rf_msg_id_gaps=2` by `rf_rx_msg=91`
  - `rf_reasm_drops=0`
- Interpretation:
  - full RF packets/messages are being lost over the RF hop
  - GDS checksum failures are expected when even one RF packet is lost, because the CCSDS byte stream is no longer exact
  - current transparent RF tunnel is not reliable enough for clean `fprime-gds` telemetry without either reduced traffic, stronger RF settings, or ACK/retry

### Framing observation

- Raw ground bytes repeatedly show CCSDS-looking TM header candidates with dictionary SCID `68` and VCID `1`.
- GDS warning source is the CCSDS space-data-link CRC path, not the generic F Prime `0xDEADBEEF` framer.
- A quick `--frame-size 51` test did not fix decode; the stronger evidence is now packet loss via `rf_msg_id_gaps`.

### MVP status in plain terms

- Basically there for command uplink and Pi-side command execution.
- Not done for the judge-facing MVP until `fprime-gds` cleanly decodes telemetry/events without checksum spam.
- Most likely MVP unblocker:
  - add minimal RF ACK/retry or
  - throttle downlink to a tiny command-response/heartbeat stream that can survive the current packet-loss rate.

## Session Handoff Update (2026-04-24, RF ACK/retry + small TM frames)

### What changed

- Added minimal RF ACK/retry to both Teensy relays.
- Reduced demo downlink pressure in F Prime topology by disabling most periodic subsystem/service telemetry runs.
- Cross-compiled and deployed Pi Zero W ARM build to `/home/pi/artemis/cross`.
- Reduced F Prime CCSDS TM frame config for RF MVP:
  - `ComCfg.TmFrameFixedSize = 128`
  - `FW_COM_BUFFER_MAX_SIZE = 96`
  - `FW_LOG_STRING_MAX_SIZE = 80`
- Added satellite Teensy `Serial2` RX buffer:
  - `RPI_UART_RX_BUFFER_SIZE = 4096`
- Changed satellite raw UART batching to collect full 128-byte CCSDS TM frames before RF send:
  - `CCSDS_TM_FRAME_BYTES = 128`
- Fixed RF ACK semantics:
  - old ACK was message-level but sender waited after every RF segment
  - new ACK includes `msgId + segIdx`
  - receiver ACKs each accepted segment

### Current verified state

- Pi service starts cleanly with deployed cross-compiled binary.
- Local dictionary confirms:
  - `ComCfg.TmFrameFixedSize = 128`
- Raw ground capture after per-segment ACK shows valid 128-byte TM frames:
  - valid CRC frame offsets observed at `0, 135, 270, ...`
  - the extra 7 bytes between frames look like partial TM headers and still need cleanup
- `fprime-gds` now receives real frames:
  - GDS prints APID sequence warnings, which means it is decoding some space packets
  - GDS still prints checksum warnings due to residual partial fragments/loss
- Command MVP path still works:
  - sent `missionManager.PING --arguments 4244`
  - Pi journal showed `MissionManager pong token=4244 count=1`
  - command dispatched and completed

### Current root cause

- The original blocker was not only bandwidth.
- Main transport bug found:
  - 128-byte TM frames span 3 RF packets at 44 useful RF bytes/packet
  - previous ACK/retry waited after every segment but only ACKed at whole-message completion
  - this broke multi-segment reassembly and caused partial frame leakage/truncation
- After per-segment ACK, full valid 128-byte TM frames reach the ground.
- Remaining issue:
  - partial 7-byte TM header fragments are still interleaved between valid frames
  - likely caused by stale/partial raw UART batching or failed multi-segment sends dropping an in-progress frame

### Next best step

- Clean up the residual 7-byte partial fragments before calling GDS done:
  - on satellite raw UART input, discard partial stale chunks when `rawUartChunkBytes == 128` instead of flushing them
  - only enqueue exactly 128-byte downlink frames
  - keep ground uplink behavior at small raw chunks for TC command stream
- Then rerun:
  - raw capture CRC scan for only 128-byte valid frames with no 7-byte fragments
  - `fprime-gds`
  - `missionManager.PING`

### Follow-up result

- Implemented the stale-partial cleanup:
  - when fixed-frame raw chunking is enabled (`rawUartChunkBytes > RF_SEGMENT_MAX_DATA`), stale partial chunks are dropped and counted as `framingDrops`
  - ground default small-chunk uplink behavior is unchanged
- Rebuilt and reflashed both Teensys sequentially:
  - ground: `usb:1100000`
  - satellite: `usb:100000`
- Raw ground capture after cleanup:
  - captured exactly one 128-byte frame
  - CRC scan found `valid128_count=1` at offset `0`
  - no extra 7-byte TM header fragment in that sample
- Final GDS smoke:
  - launched GDS on GUI port `5051`
  - no checksum warnings observed in the final sample window
  - GDS still prints APID sequence warnings, which means it is decoding frames but packet loss/sequence discontinuities remain
- Final command smoke:
  - sent `missionManager.PING --arguments 4245`
  - Pi journal showed `MissionManager pong token=4245 count=1`
  - command dispatched and completed

### MVP interpretation

- MVP command uplink and Pi command execution are working.
- GDS downlink is now decoding valid CCSDS TM frames.
- Remaining issue is reliability/continuity, not complete link failure:
  - APID sequence warnings still indicate dropped telemetry packets/frames under sustained downlink.
  - For the live demo, keep telemetry tiny and avoid continuous high-rate events/logs.
  - Longer-term fix is stronger RF rate/link settings and/or a real stream protocol with end-to-end frame sequence/NAK/replay.

### Demo freeze cleanup

- Added project-owned RF MVP F Prime config overrides:
  - `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/ComCfg.fpp`
  - `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/FpConstants.fpp`
- Restored `ArtemisRpiTeensy_N2/lib/fprime/default/config` to upstream defaults so the F Prime submodule is clean.
- Added runbook:
  - `docs/RF_MVP_DEMO_RUNBOOK.md`
- Added smoke script:
  - `ArtemisRpiTeensy_N2/tools/demo_rf_mvp_smoke.sh`
- Validation after cleanup:
  - `fprime-util generate -f` passed
  - `fprime-util build` passed
  - `GDS_Teensy/tools/arduino-cli/build.sh` passed
  - `ArtemisTeensy_N2_Baremetal/tools/arduino-cli/build.sh` passed
  - live RF smoke passed with token `4320`
