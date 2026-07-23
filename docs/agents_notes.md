# agents_notes.md

## Project Snapshot (2026-02-13)

This repo is the Neutron 2 team F' integration workspace:
- F' flight software runs on Raspberry Pi.
- Satellite Teensy provides a channelized Pi UART bridge plus RF23BP segmentation/reassembly.
- Channel 0 is the normal F Prime/GDS CCSDS stream, channel 1 is payload/science packets, and channel 2 is satellite-Teensy-local subsystem RPC.
- Ground Teensy reassembles RF channel 0 to the laptop GDS USB serial stream and RF channel 1 to payload USB when triple-serial mode is enabled.

## Demo Target Snapshot (2026-04-07)

The current top-level target is the shortened FlatSat FSR end-to-end demo shown in the team's planning slides. Treat this as the active demonstration narrative when making architecture, implementation, or documentation decisions.

### Demo story to support

1. System boots in `Base Mode`.
2. Operator uses `D2S2` planning inputs to determine the mock ground-pass contact duration.
3. During the simulated pass, the satellite remains in base mode and downlinks `SOH`/health telemetry for a live judge-facing display.
4. Operator sends a command to schedule data collection after a short delay, for example `10` seconds.
5. Flight software executes a data-collection action using payload data; simulated or temporary payload data is acceptable for the demo if the real payload path is not ready.
6. After collection, the system transitions into a science downlink path and sends payload/science data to the ground side.
7. Ground software on the laptop reviews, displays, or analyzes the downlinked
   science data. `fprime-gds` remains the command/event/telemetry authority. For
   the refined EPSCoR C3M demo, the C3M payload receiver web app is the primary
   channel-1 receive/CRC/decode/history surface; raw receiver and decoder CLIs
   are engineering fallbacks. `Yamcs` is the longer-term end-goal ground stack.

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
- `UartChannelMux` wraps/unwraps the single Pi <-> satellite Teensy UART into tagged channels.
- Main runtime binary accepts `-d <uart_device>`.
- MVP custom components in deployment:
  - `Components/LinkCfg`
  - `Components/UartChannelMux`
  - `Components/PayloadDownlinkApp`
  - `Components/TeensyTransportManager`
  - `Components/MissionApp`
  - `Components/ScienceApp`
  - `Components/SoHApp`
  - `Components/CommsApp`
  - `Components/PayloadManager`
  - `Components/PayloadDriver_NeutronSim`
  - `Components/ThermalManager`
  - `Components/EpsManager`
  - `Components/EpsDriver_Artemis`
  - `Components/CommsDriver_TeensyRfm23`
- Build status:
  - `fprime-util generate -f` passes
  - `fprime-util build` passes

### 1b) Raspberry Pi Build Path
- Source-of-truth runbook:
  - `docs/RPI_BUILD.md`
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
  - Default HIL path is channelized:
    - channel 0 CCSDS bytes -> RF segment transport -> ground USB raw bytes
    - channel 1 payload packets -> RF segment transport -> ground payload USB when enabled
    - channel 2 Teensy-local RPC -> satellite `PduProxy` -> PDU UART, no RF forwarding
  - Pi <-> satellite UART frames use `0xD4 0xC3 + channel + len + crc16`
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
  - Receives RF segments and reassembles full message bytes for RF channels 0 and 1
  - Streams reassembled channel 0 bytes directly to USB serial (`Serial`) for laptop GDS UART input
  - Streams reassembled channel 1 bytes to `SerialUSB2` when triple-serial USB is enabled
  - Packetizes raw USB byte bursts (`12 ms` idle flush or 44-byte channel 0 burst) for uplink
  - Queue-backed relay path enabled for burst tolerance:
    - `UPLINK_QUEUE_DEPTH = 32`
    - `DOWNLINK_QUEUE_DEPTH = 32`
    - relay max queue cap `MAX_QUEUE_DEPTH = 32`
- Build status:
  - `./tools/arduino-cli/build.sh` passes for `teensy:avr:teensy41`

### 4) Live HIL Bench Status (2026-06-26)
- Current bench smoke test is working after reflashing both Teensys with
  explicit physical upload IDs:
  - ground Teensy: `usb:100000`
  - satellite Teensy: `usb:2100000`
- Current USB map after the successful channel-1 smoke:
  - ground channel 0 / GDS: `/dev/cu.usbmodem115551201`
  - ground debug: `/dev/cu.usbmodem115551203`
  - ground channel 1 / payload receiver: `/dev/cu.usbmodem115551205`
  - satellite debug: `/dev/cu.usbmodem115502201`
- Confirmed smoke evidence:
  - Pi service runs `/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0`
  - `fprime-gds` over ground channel 0 can command the Pi through RF
  - `missionApp.PING` dispatches, logs `MissionApp pong`, and completes
  - retried full-flow commands can reach the Pi over the lossy RF path
  - Pi-side demo flow can produce and store a simulated science payload
  - Pi-side `PayloadDownlinkApp` can report `PayloadDownlinkComplete` and
    `DownlinkFinished` for the staged payload
  - `payload_receiver.py` reconstructs channel-1 RF payload files on the laptop
- RF looks significantly healthier than the earlier wedged state, but it is
  still lossy; use command retries and journal/GDS confirmation instead of
  assuming a single command send landed.
- Channel-1 receiver issue fixed/verified:
  - Root cause candidate was channel 1 using best-effort RF sends while channel 0
    used per-segment ACK/retry. Payload channel 1 now uses the same RF
    segment ACK/retry path.
  - Successful run: 2026-06-26 11:28 HST, receiver output
    `complete: product=2 transfer=1 bytes=67 packets=2 crc=0x890e`
    at `/tmp/neutron_hil/rf_ack_payload_20260626_112746/payload_5s.bin`.
  - Local reconstructed SHA-256 matched Pi
    `/tmp/neutron_payload_captures/latest_payload.bin`:
    `be92e314c7f40c8b708920ac882c9eb0a1a9f4efccd1ef5d95684d99d822dfa1`.
  - Channel-specific counters confirmed the route:
    satellite `payload_uart_rx=110 payload_rf_tx_msg=4 payload_rf_tx_seg=4`;
    ground `payload_rf_rx_msg=4 payload_rf_rx_seg=4 payload_uart_tx=110`.
- Useful fallback for demo display only:
  - a Pi-copied payload can be shown in
    `ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py`
  - label that as a Pi-side science-product fallback, not a verified channel-1
    RF reconstruction

## Important Clarification: Framing

- End-to-end payload is still opaque F' bytes.
- Endpoints (GDS and F' app) use `ComCcsds` framing (`space-packet-space-data-link`).
- The Pi <-> satellite Teensy hop is channelized below the F Prime/GDS endpoint layer; `fprime-gds` still sees a byte-pure CCSDS stream.
- RF transport carries only channel 0 and channel 1. Channel 2 is consumed locally by the satellite Teensy.
- Transport constants are generated from `config/transport_constants.json`.
  Regenerate with `python3 tools/generate_transport_constants.py`; do not
  hand-edit `LinkCfg.hpp` or either Teensy `link_protocol.hpp`.
- `./tools/validate_local.sh` checks generated headers, transport drift, local
  Python tests, F Prime unified-topology build, component UTs, and the automated
  local demo sequence.
- `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md` is the software triage map for
  GDS/dictionary, mission services, payload capture, storage, channel 1
  downlink, Teensy/RF counters, viewer files, and EPS/PDU channel 2.
- Contract documentation:
  - `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`
  - `GDS_Teensy/docs/transport_contract.md`

## HIL Framing Alignment (2026-04-15, superseded by channel mux on 2026-06-18)

- Historical note: the raw-tunnel alignment fixed the earlier mismatch between ground and satellite bridge modes.
- Current runtime contract is channelized on the Pi <-> satellite UART:
  - laptop `fprime-gds` still sends/receives raw CCSDS bytes on the ground Teensy USB data port
  - ground Teensy maps those bytes to RF channel 0
  - satellite Teensy maps RF channel 0 to a tagged Pi UART frame
  - RPi `UartChannelMux` unwraps channel 0 back into `ComCcsds`
  - PDU/EPS requests use channel 2 and never traverse RF

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
- The Teensy UART channel wrapper (`0xD4 0xC3 + channel + len + crc16`) is a separate link-layer mechanism below `ComCcsds`.
- For local GDS validation, use `ComCcsds` framing as the primary/default endpoint path.
- For hardware HIL, `UartChannelMux` applies the channel wrapper on the Pi <-> satellite Teensy UART while GDS still sees normal `ComCcsds`.

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

## Driver MVP Update (2026-04-15)

### What changed

- `GpsDriver_Artemis` is no longer a static `+400` key offset stub.
  - It now models a deterministic GPS fix state machine and emits:
    - `FixState` (`0=no-fix`, `1=acquiring`, `2=2D`, `3=3D`)
    - `SatellitesTracked`
    - `FixQualityScore`
    - `RequestCount`
  - `statusOut` now reports normalized fix-state keys (`0..3`), so `GpsManager.GpsFixState` is chart-friendly.
- `CommsDriver_TeensyRfm23` is no longer a static `+500` key offset stub.
  - It now models deterministic RFM23 link behavior and emits:
    - `LinkState` (`0=down`, `1=acquiring`, `2=locked`, `3=degraded`)
    - `RssiDbm`
    - `RfRxPackets`
    - `RfTxPackets`
    - `RfTxDrops`
    - `RequestCount`
  - Output port behavior is now split intentionally:
    - `statusOut[0]` -> normalized link state for `CommsApp`
    - `statusOut[1]` -> `RfRxPackets` snapshot for `TeensyTransportManager` downlink counter visibility

### Why this was needed

- Static offset stubs produced unrealistic telemetry values and made chart interpretation weak.
- Demo path needed visibly changing, semantically meaningful telemetry for GPS and comms subsystems without breaking current topology wiring.

### Demo-visible channels/events to watch

- GPS:
  - `ArtemisRpiTeensyDeployment.gpsDriverArtemis.FixState`
  - `ArtemisRpiTeensyDeployment.gpsDriverArtemis.SatellitesTracked`
  - `ArtemisRpiTeensyDeployment.gpsDriverArtemis.FixQualityScore`
  - `ArtemisRpiTeensyDeployment.gpsManager.GpsFixState`
  - Event: `ArtemisRpiTeensyDeployment.gpsDriverArtemis.FixStateChanged`
- Comms:
  - `ArtemisRpiTeensyDeployment.commsDriverTeensyRfm23.LinkState`
  - `ArtemisRpiTeensyDeployment.commsDriverTeensyRfm23.RssiDbm`
  - `ArtemisRpiTeensyDeployment.commsDriverTeensyRfm23.RfRxPackets`
  - `ArtemisRpiTeensyDeployment.commsApp.LinkState`
  - `ArtemisRpiTeensyDeployment.teensyTransportManager.DownlinkFrames`
  - Event: `ArtemisRpiTeensyDeployment.commsDriverTeensyRfm23.LinkStateChanged`

### Remaining gap (important)

- These two drivers are now mission-meaningful but still model-driven.
- They are not yet parsing live hardware status lines (for example Teensy `#LINK_STATUS` response fields or raw GPS sentence/fix data).
- Full hardware-backed driver ingestion remains a follow-on item after the MVP demo chain is stable.

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

- Superseded implementation decision (2026-06-18): use tagged channels on the one available `RPi <-> satellite Teensy` UART.
- Reason:
  - The Raspberry Pi has one active UART to the satellite Teensy in the current FlatSat layout.
  - The satellite Teensy is the microcontroller that can talk to PDU, radio, GPS, and other local subsystems.
  - A second Pi-side PDU UART should not be assumed.
- Current contract:
  - channel 0: CCSDS/GDS stream over RF
  - channel 1: payload/science packet stream over RF
  - channel 2: Teensy-local subsystem RPC, currently PDU/EPS
- Design rule:
  - Keep channel 2 bounded request/response traffic only.
  - Do not send channel 2 over RF.
  - Do not let mission components know about UART/RF framing; keep it behind drivers and `UartChannelMux`.
  - If future hardware adds a real sideband bus, it can replace channel 2 behind `EpsDriver_Artemis` without changing `EpsManager`.

## Future Subsystem Submodule Plan (2026-06-15)

- Long-term integration direction: the top-level F' repo should pin whole subsystem implementation repos as submodules, not depend on a tiny protocol-constants-only repo as the main source of truth.
- Intended shape:
  - `ArtemisRpiTeensy_N2/` remains the active F' deployment and mission-facing component workspace.
  - `external/pdu-firmware/` can be a submodule pointing at the ATSAME51 PDU firmware repo, including firmware implementation, ICD, protocol header, Teensy/bench tooling, and notes.
  - `external/satnogs-radio/` can be added once the SatNOGS dev-board repo is real and should include radio firmware/protocol, MTU/data-budget constraints, setup scripts, and bench/test tools.
  - `external/payload/` can be added once the real payload-board repo exists. Until then, keep the RPi-hosted emulated payload adapter local to this repo.
  - `external/epscorc3m/` is the most up-to-date full Artemis CubeSat baremetal demo reference currently pinned in this repo. It documents many practical footguns, but it is not the target F Prime architecture.
  - `external/artemis-cubesat-examples/` is reference-only legacy code from the original general-purpose low-cost Artemis 1U CubeSat bus. Use it as subsystem sample code, not as mission software to copy.
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
- Reference-code warning:
  - Legacy Artemis examples often assume Teensy as the main flight computer, while this repo uses Raspberry Pi as the host for the F Prime deployment and Teensy only for bridge/control duties.
  - Treat handwritten baremetal examples as useful interface references but review carefully for bugs, memory-safety issues, and mission mismatch before adapting anything.

## Current Demo / HIL Status

- 2026-06-18: `./tools/run_neutron2_local_demo.sh --gui-port 5070 --viewer-port 8070 --delay 3 --capture-seconds 3 --exit-after-sequence --skip-build`
  passed with the channelized local emulator.
- Verified runtime events included:
  - `PayloadDriver_NeutronSim.CaptureComplete`
  - `StorageManager.ScienceStored`
  - `PayloadDownlinkApp.PayloadDownlinkComplete`
  - `CommsApp.DownlinkFinished`
- The local emulator observed channel 1 payload bytes and kept them off the GDS channel 0 stream.
- `run_neutron2_local_demo.sh` always starts GDS and the Neutron 2 payload viewer, then opens/refocuses the viewer after verified downlink completion.
- 2026-06-23 HIL proved the shortened demo story over the real RPi UART,
  satellite/ground Teensy firmware, and RFM23BP channel 0/1 path:
  - three full capture/downlink/viewer passes completed with local-vs-Pi hash
    match and viewer summaries
  - a later progress-log redeploy smoke completed with a 72 byte payload hash
    match and viewer summary
  - Pi journal showed `PayloadDownlinkProgress` at nominal 10% increments plus
    `PayloadDownlinkComplete` and `DownlinkFinished`
- Current HIL USB / upload map from the progress-smoke run:
  - satellite Teensy debug: `/dev/cu.usbmodem115502201`
  - satellite Teensy upload ID: `usb:2100000`
  - ground Teensy GDS / channel 0 data: `/dev/cu.usbmodem115553301`
  - ground Teensy debug: `/dev/cu.usbmodem115553303`
  - ground Teensy payload / channel 1: `/dev/cu.usbmodem115553305`
  - ground Teensy upload ID: `usb:100000`
- Current deployed Pi progress-smoke artifacts:
  - binary hash: `fd8e260f042407545620936405e3b35f7026b404d1d8c26cd103afd7d478d670`
  - dictionary hash: `9a744f4343623d136236d9e10427c7c5fedd2457c773fd951c21bab131215f92`
  - payload hash: `094338d54bf52f0defee9dfa101d03bba7712ad20a877d51e2ff3202f468114f`
  - payload bytes: `72`
  - viewer rows: `6`
- Earlier three-pass gate evidence:
  - pass 1: `83` bytes, SHA-256 `eabd0e9f1ddbff5224617affffc72d187b53bffe5bf83426714f5f5359157165`, viewer rows `6`
  - pass 2: `83` bytes, SHA-256 `b4cd40d47c8b7cef9fc407ab7013fdc8831e2d31651e322921592e7f97fe792c`, viewer rows `6`
  - pass 3: `83` bytes, SHA-256 `00a1f94fa03521938757e4e6f85fde76b81ca82fe04099f74fa0c177ca7ccfa7`, viewer rows `6`
- HIL pass criteria now live in `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`:
  - GDS receives live F Prime events/telemetry
  - payload receiver writes the reconstructed file
  - local payload hash matches Pi `/tmp/neutron_payload_captures/latest_payload.bin`
  - payload viewer parses the result
  - Pi journal shows `PayloadDownlinkProgress` and `DownlinkFinished`
- Remaining hardware proof: real PDU response behavior over satellite Teensy
  `Serial1` and RF/GDS cleanup to reduce APID sequence-count warnings.
- Known caveat: GDS APID sequence-count warnings mean channel 0 is lossy, not
  dead. The channel 1 payload receiver retry/CRC path recovered the tested
  science products.

## Primary TODO

1. Keep the HIL demo repeatable: `Base Mode` -> scheduled data collection -> science-data downlink -> viewer summary.
2. HIL-test channel 2 against a real PDU through satellite Teensy `Serial1`.
3. Reduce RF/GDS APID sequence-count warnings without regressing the payload retry path.
4. Keep rehearsing the neutron simulator product path through the selected HIL downlink/review path:
   - `PayloadDriver_NeutronSim` stages the latest capture for downlink.
   - `REQUEST_SCIENCE_DOWNLINK` starts the channel 1 `PayloadDownlinkApp` transfer.
   - Use `tools/payload_receiver.py` on the ground channel 1 serial endpoint for HIL payload reconstruction.
5. Keep `fprime-gds` as the live MVP demo ground interface and treat `Yamcs` as the post-MVP target presentation/analysis stack.
6. Add minimal segment ACK/retry for RF relay reliability after the channelized CCSDS path is stable.
   - MVP target: command uplink delivery confidence and reduced telemetry burst loss during demo.
7. Add deterministic packet boundary extraction for uplink beyond simple burst mode if required by the selected demo flow.
8. Decide whether the MVP stays on the custom channel 1 payload receiver or graduates to stock F Prime file downlink:
   - current channel 1 path transfers real staged payload bytes but does not appear as a stock GDS `#Downlink` file transfer
   - future migration target is `Svc::FileDownlink` once the link MTU/loss behavior can carry the stock file-transfer path cleanly
   - pass criteria for the current MVP path: reconstructed file exists on the laptop, content/size/CRC match the source capture, and channel 0 GDS traffic stays healthy during transfer

## Important Paths

- F' project root:
  - `ArtemisRpiTeensy_N2`
- RPi-hosted neutron payload simulator:
  - `external/payload-neutron-simulation`
- Satellite Teensy project:
  - `ArtemisTeensy_N2_Baremetal`
- Ground Teensy project:
  - `GDS_Teensy`
- Quick test guide:
  - `docs/GET_STARTED_TESTING.md`
- Build runbook:
  - `docs/build_runbook.md`
- Raspberry Pi native build runbook:
  - `docs/RPI_BUILD.md`

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
  - `docs/archive/RADIO_AGNOSTIC_COMMS_AND_PAYLOAD_DOWNLINK_PLAN.md`
- Read it before touching payload downlink, the Teensy link protocol, or comms topology.
- Core decisions captured there:
  - payload bulk data moves on a second stateless virtual channel over the existing RF bridge (per-frame channel tags, no link mode switching)
  - GDS keeps a byte-pure CCSDS stream on channel 0 (GUI cannot break)
  - PDU/EPS uses channel 2 as a satellite-Teensy-local RPC path and is not forwarded over RF
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
  - previously used `python3 -m venv --clear` to avoid stale shebang path issues after mount-path changes
  - 2026-06-23: cross-build script now defaults to cached iteration and adds `--clean` for deliberate full refresh; normal runs reuse `.cross-venv-linux` when `fprime-util --help` succeeds and avoid forced F Prime regenerate, so repeated Python dependency downloads and unnecessary full rebuilds are avoided
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

## Historical Session Handoff (2026-04-24, RF/GDS HIL debug)

Current 2026-06-23 upload IDs supersede the older IDs in this historical
section:

- ground Teensy upload ID: `usb:100000`
- satellite Teensy upload ID: `usb:2100000`

### USB/SSH mapping confirmed

- Satellite Teensy:
  - `/dev/cu.usbmodem115502201`
  - historical physical Teensy upload port: `usb:100000`
  - current sketch role: satellite bridge, Pi data on `Serial2`, USB `Serial` debug counters
- Ground station Teensy:
  - `/dev/cu.usbmodem115551201` = GDS data stream (`Serial`)
  - `/dev/cu.usbmodem115551203` = debug stream (`SerialUSB1`)
  - historical physical Teensy upload port: `usb:1100000`
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
  - ground: `-p usb:100000`
  - satellite: `-p usb:2100000`
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
  - historical ground ID at that time: `usb:1100000`
  - historical satellite ID at that time: `usb:100000`
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
  - `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`
- Added smoke script:
  - `ArtemisRpiTeensy_N2/tools/demo_rf_mvp_smoke.sh`
- Validation after cleanup:
  - `fprime-util generate -f` passed
  - `fprime-util build` passed
  - `GDS_Teensy/tools/arduino-cli/build.sh` passed
  - `ArtemisTeensy_N2_Baremetal/tools/arduino-cli/build.sh` passed
  - live RF smoke passed with token `4320`

## EPS/PDU driver notes

- F Prime now exposes the mission-facing EPS/PDU path through `EpsManager` and `EpsDriver_Artemis`.
- The EPS/PDU boundary is intentionally pragmatic for MVP because the new PDU
  is planned for F Prime-driven testing. Keep generic mission-facing commands
  in `EpsManager`, keep PDU v2 protocol details in `EpsDriver_Artemis`, and
  refactor/cull the manager surface later if the proven hardware contract
  demands a sharper split.
- The driver uses the PDU v2 framed UART protocol from `external/artemis-pdu/src/pdu_protocol_v2.h`.
- The driver no longer opens a separate Pi serial device for the PDU.
- EPS/PDU requests are wrapped as channel 2 local RPC packets over the existing Pi <-> satellite Teensy UART.
- Satellite `PduProxy` writes the inner PDU v2 frame to `Serial1` at 9600 baud and returns the PDU response over channel 2.
- Channel 2 local status values are `0=OK`, `1=BAD_REQUEST`, `2=BUSY`, `3=TIMEOUT`, `4=TARGET_ERROR`.
- `TransportFailureCount` tracks bad local envelopes, timeouts, target errors, malformed PDU frames, and busy/not-connected send attempts.
- Operator-safe commands currently exposed:
  - `REQUEST_EPS_STATUS`
  - `PING_EPS_ADAPTER`
  - `REQUEST_EPS_ADAPTER_INFO`
  - `REQUEST_EPS_RAIL`
  - `SET_EPS_RAIL_STATE` with `confirm=1`
  - `POWER_CYCLE_EPS_RAIL` with `confirm=1`
  - `REQUEST_CHARGER_STATUS`
  - `SET_CHARGER_STATE` with `confirm=1`
- Missing confirmation or unsafe/out-of-range PDU command arguments return
  `VALIDATION_ERROR` in GDS command history and emit `EpsCommandRejected`.
- Burn-wire and torque-coil commands are intentionally not exposed through `EpsManager` yet; add those only with a dedicated HIL/runbook procedure.

## Service/Adapter Cleanup — Open Follow-ups (2026-06-25, historical vocabulary)

The service/adapter architecture cleanup landed before the App-Man-Drv rename:
de-leaked `EpsService`, `ScienceProductDescriptor` threaded end to end,
active/async payload adapter, `MissionManager`-validated mode transitions with a unit test, and
single-source transport constants (`config/transport_constants.json` +
`tools/generate_transport_constants.py` + `tools/check_transport_constants.py`).
Durable rules now live in `docs/STUDENT_COMPONENT_STARTERS.md`; the dated review
snapshot is archived at
`docs/archive/SERVICE_ADAPTER_ARCHITECTURE_REVIEW_2026-06-25.md`.

Still open (not demo-blocking):
- Define one standard service-to-adapter port-pair template, modeled on the
  payload path, so the thin subsystems (ADCS, GPS, thermal, comms) get a
  consistent contract when they are built out.
- Optional hardening: wire `tools/check_transport_constants.py` into CI or a
  pre-commit hook so generated transport headers cannot drift from the manifest.

## Architecture ↔ F´ App-Man-Drv Cross-Reference (2026-06-29)

- `docs/SYSTEM_ARCHITECTURE.md` now states explicitly that our
  Application → Manager → Driver HAL **is** F´'s built-in
  Application-Manager-Driver (App-Man-Drv) pattern, not a bespoke invention.
  See the new "This is F´'s Application-Manager-Driver pattern" subsection
  (mapping table + vocabulary warning) and the "Implementation notes"
  subsection under the Application → Manager → Driver section.
- Current active naming uses F´ vocabulary directly: `*App` for applications,
  `*Manager` for subsystem contracts, and `*Driver_<Hardware>` for hardware or
  simulator glue.
- Implementation guidance added: use stock `Drv.LinuxI2cDriver` /
  `LinuxSpiDriver` / `LinuxGpioDriver` for subsystems on the Pi's own bus
  (our channel-2 Teensy-RPC drivers are bespoke for a hardware reason), and
  check `fprime-sensors` for ready-made device managers before writing a new
  `*Driver_*`.

## Demo Hardening Sprint (2026-07-06)

- Sprint source of truth: `docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md`;
  worker evidence lives under `docs/hardening-reports/`.
- Scheduling hardening landed: `SCHEDULE_COLLECTION` rejects invalid delay
  bounds, capture duration is bounded/defaulted to 30 s, `SCIENCE_CAPTURE` is
  one-shot, and `CANCEL_COLLECTION` plus `ENTER_BASE_MODE` clear pending
  collection state.
- Command-driven mode changes now go through the same validation path as port
  mode updates; command rejection responses stay operator-visible.
- Race fix landed after adversarial review: state-mutating `ScienceApp`,
  `MissionApp`, and `CommsApp` inputs are async so cancel/mode/status
  updates are serialized on each active component queue.
- Throttles are split intentionally: storm-capable warning/event paths remain
  throttled, but human command rejections are unthrottled in GDS command
  history/events.
- Topology de-fork landed: the old demo-only topology fork is removed; local
  emulation and HIL now build/run the same unified topology with
  RF-budget-sensitive periodic loops still off.
- `ScienceApp.CAPTURE_DURATION_SECONDS` is now a `PrmDb`-backed parameter;
  `CONFIGURE_CAPTURE_DURATION` remains a volatile runtime override, while
  durable default changes use `PRM_SET` then `PRM_SAVE`.
- FPP ops pass landed: selected events have throttles, many stable channels use
  `update on change`, and comms RSSI channels have low warning limits.
- Both Teensy bridge sketches now arm a 12 s hardware watchdog, feed it through
  normal relay paths, and print boot lines for normal arming or WDT-caused
  reset detection.
- `tools/validate_local.sh` now includes a shared Teensy drift guard before
  build/cache work, including the shared watchdog helper.
- Pi provisioning was versioned under `deploy/pi/` with
  `artemis-fprime.service`, `Restart=always`, release-symlink layout guidance,
  and `ln.service` migration notes.
- Operator docs were updated for cancel behavior, command bounds, parameter
  persistence, `PrmDb.dat` runtime-location caveat, watchdog boot logs, and the
  moved downlink-reliability research doc.
- Validation evidence: final orchestrator-run `./tools/validate_local.sh`
  passed end-to-end on the unified topology, including drift checks, generated
  transport checks, Python tests, F Prime build, 6/6 component UT executables,
  and the automated local demo CSV path.
- Validation evidence: both Arduino CLI builds passed for satellite and ground
  Teensy firmware after watchdog changes.
- Still open for the next bench/target session: ARMv6 cross-build verification
  of the hardened code, HIL RF smoke on the unified topology, deliberate WDT
  trip test, live `ln` to `artemis-fprime.service` migration, and `PRM_SAVE`
  round-trip on the Pi filesystem/release-symlink layout.

## Native App-Man-Drv rename (2026-07-06)

The active repo now uses native F´ Application-Manager-Driver vocabulary. Old
hardening logs, archive docs, and pre-rename notes may still use the legacy
Manager/Service/Adapter terms; use this table to decode them.

Application tier, formerly repo "Manager":

| Legacy name | Native name | Legacy instance | Native instance |
| --- | --- | --- | --- |
| `MissionManager` | `MissionApp` | `missionManager` | `missionApp` |
| `ScienceManager` | `ScienceApp` | `scienceManager` | `scienceApp` |
| `CommsManager` | `CommsApp` | `commsManager` | `commsApp` |
| `SoHManager` | `SoHApp` | `sohManager` | `sohApp` |
| `PayloadDownlinkManager` | `PayloadDownlinkApp` | `payloadDownlinkManager` | `payloadDownlinkApp` |

Manager tier, formerly repo "Service":

| Legacy name | Native name | Legacy instance | Native instance |
| --- | --- | --- | --- |
| `EpsService` | `EpsManager` | `epsService` | `epsManager` |
| `PayloadService` | `PayloadManager` | `payloadService` | `payloadManager` |
| `GpsService` | `GpsManager` | `gpsService` | `gpsManager` |
| `AdcsService` | `AdcsManager` | `adcsService` | `adcsManager` |
| `ThermalService` | `ThermalManager` | `thermalService` | `thermalManager` |
| `StorageService` | `StorageManager` | `storageService` | `storageManager` |
| `TeensyTransportService` | `TeensyTransportManager` | `teensyTransportService` | `teensyTransportManager` |

Driver tier, formerly repo "Adapter":

| Legacy name | Native name | Legacy instance | Native instance |
| --- | --- | --- | --- |
| `EpsAdapter_Artemis` | `EpsDriver_Artemis` | `epsAdapterArtemis` | `epsDriverArtemis` |
| `PayloadAdapter_NeutronSim` | `PayloadDriver_NeutronSim` | `payloadAdapterNeutronSim` | `payloadDriverNeutronSim` |
| `PayloadAdapter_N1Legacy` | `PayloadDriver_N1Legacy` | `payloadAdapterN1Legacy` | `payloadDriverN1Legacy` |
| `CommsAdapter_TeensyRfm23` | `CommsDriver_TeensyRfm23` | `commsAdapterTeensyRfm23` | `commsDriverTeensyRfm23` |
| `GpsAdapter_Artemis` | `GpsDriver_Artemis` | `gpsAdapterArtemis` | `gpsDriverArtemis` |
| `ThermalAdapter_Artemis` | `ThermalDriver_Artemis` | `thermalAdapterArtemis` | `thermalDriverArtemis` |
| `AdcsAdapter_D2S2` | `AdcsDriver_D2S2` | `adcsAdapterD2S2` | `adcsDriverD2S2` |

## C3M Lepton RF HIL Acceptance (2026-07-09)

- Both live acceptance gates pass with a real UVC Lepton product over the full
  Pi -> satellite Teensy -> RFM23BP -> ground Teensy -> laptop path.
- Final product: 38,480-byte `.fdp`, 1,100 data packets, full `160x120` image.
- Command-to-file time: `58.557 s`; receiver required no retry request.
- Pi source and ground file SHA-256 matched:
  `87b61b387647a4e732918b93a071fe51bf64b9b1a55ede6ff30e99289465ac26`.
- A channel-0 ping returned in the same second during channel-1 downlink; final
  satellite CRC, framing, parser-timeout, RF-TX, and queue-drop counters were
  all zero.
- Root cause was producer/consumer flow control at the shared Pi UART/RF
  boundary, not a new UART device or baud selection. `/dev/serial0` remains
  `115200 8N1` and resolves to `/dev/ttyS0` on this Pi.
- Validated generated transport settings: 37 ms base UART drain margin, 40 ms
  additional channel-0 margin, 22 payload/retry packets per 1 Hz run, 15 ms
  payload RF gap, ACKed ground-to-satellite CCSDS, and unACKed satellite
  telemetry/payload.
- Repeat procedure and evidence live in
  `docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`; the investigation log is
  `docs/C3M_LEPTON_RF_HIL_SCRATCHPAD_2026-07-09.md`.

## C3M Refined Mission Operations (2026-07-09)

- Normal operator surfaces are now only F Prime GDS on channel 0 and
  `ground-station/c3m-payload-receiver-ui/` on channel 1.
- The web app starts in `Ready — awaiting downlink`, listens continuously,
  writes unique runs under repo-root `data/`, verifies CRC, decodes the exact
  current `.fdp`, and keeps older runs visibly separate in History.
- Receiver protocol, retries, and CLI fallback remain implemented once in
  `ArtemisRpiTeensy_N2/tools/payload_receiver.py` through structured events.
- Local deterministic replay covers success, CRC failure, serial failures, and
  consecutive transfers. It does not replace the required final `3/3` HIL
  rehearsal at demo geometry.
- The obsolete Pi legacy rollback binary was deleted; the supported active
  release remains `/home/pi/artemis/releases/c3m-hil-uartflow37-channel`.
- Durable work plan: `docs/C3M_DEMO_HARDENING_PLAN_2026-07-09.md`.

## C3M Best-Effort Thermal Reception (2026-07-10)

- The channel-1 web receiver preserves the complete CRC-verified happy path
  and finalizes incomplete transfers at the 120-second demo cutoff. The UI
  shows `75 s` as nominal and approximately `90 s` as longer than target.
- Partial products are position-preserving `.fdp.partial` files. Missing
  channel-1 packets are never collapsed out of the byte stream.
- The Lepton partial decoder treats pixels intersecting missing packet bytes as
  unknown, exports CSV `NaN` / JSON `null`, and renders them white.
- Partial results are explicitly non-CRC (`crc_ok: false`) and retain the
  packet map, timeout reason, counts, and thermal statistics in `run.json`.
- The UI supports pixel hover temperature inspection and reports `No data` on
  white unknown samples.
- Deterministic local replay supports repeatable `--replay-drop-packet` fault
  injection. Packet-500 evidence recovered 19,182/19,200 pixels without
  shifting valid data.
- `./tools/validate_local.sh --skip-demo` passes after the change, including
  Python tests, native F Prime build, and 6/6 component UT executables.
- HIL packet-loss qualification passed with the portable-GDS eighth-floor
  walkaround: 24 missing packets recovered in four selective-repair rounds,
  followed by a zero-repair clean cycle at restored inside-lab geometry.

## RF Mission Traffic Isolation (2026-07-10)

- C3M now assigns RadioHead's existing CRC-protected header as a strict mission
  identity: network `0xC3`, ground `0xA1`, satellite `0xA2`, version `1`.
- Neutron 2 network ID `0xD2` is reserved in `config/rf_networks.json`; its
  branch must deliberately select `rf.network: neutron2` and rebuild both
  Teensys before use.
- Wrong network, role direction, or version is rejected before ACK handling,
  reassembly, UART/USB forwarding, GDS, or payload decode.
- Dedicated `rf_wrong_network`, `rf_wrong_address`, and `rf_wrong_version`
  counters appear in periodic debug output and `#LINK_STATUS`.
- There is no new packet overhead: RadioHead already sends these four bytes, so
  the 49-byte RF packet and 44-byte Artemis segment capacity remain unchanged.
- This protects against accidental nearby-booth cross-talk, not RF collisions,
  intentional spoofing, encryption, or authentication.
- Local generator/isolation tests and both Teensy builds pass. No firmware was
  flashed; same-mission and cross-mission HIL qualification remains deferred.

## C3M RF Reliability HIL (2026-07-15)

- Active evidence and remaining fault cases live in
  `docs/C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md`.
- Current ground/satellite HEX and ARMv6 Pi binary hashes were verified live.
  The initial nominal/fault campaign remained on PID `706` with zero systemd
  restarts; the post-bridge-flash epoch booted as PID `256` with zero restarts.
- HIL-MVP-1 startup, the separate one-capture gate, and the independent 3/3
  repeated-capture gate passed on basic antennas. All four new 38,480-byte
  ground files matched their Pi sources, CRC-checked, and decoded as 160x120.
- Current downlink timing was 64.7-65.2 seconds. One repeat cycle completed a
  one-round selective repair; the other three transfers required none.
- The false mixed-channel `rf_msg_id_gaps` diagnostic was fixed in `bdca6a3`
  by allocating rolling message IDs independently per channel on both bridges.
  Focused tests, the 68-test local gate, both firmware builds, and a live
  1,100-packet post-fix transfer passed. During that HIL transfer the ground
  gap counter changed only 1 to 3 alongside one real reassembly loss/repair,
  rather than climbing by hundreds from ordinary channel interleaving.
- Focused HIL recovery gates also passed: duplicate active request, receiver
  restart from a 253/1,100 checkpoint, a six-second GDS-reader stop with
  subsequent PING recovery, and a full ground-USB unplug/replug with automatic
  receiver/GDS reconnection and a clean next cycle. Physical RF-fade
  qualification also passed with the later portable eighth-floor walkaround.
- The bounded RF TX-completion policy remains 500 ms per attempt with one retry
  and passed focused injected timeout/recovery cases on both bridges. Simply
  removing the peer does not trigger this local completion timeout, so the HIL
  plan does not claim peer-offline ACK loss as proof of `waitPacketSent()`.
- The first operator-cued five-second basic-antenna fade attempt did not
  measurably impair the link: product/transfer 2 completed 1,100/1,100 with
  zero repair rounds and exact source/ground SHA-256. It remains an
  inconclusive nominal control, not HIL-MVP-4 qualification.
- Do not use aluminum or another conductor close to the 1 W monopole setup to
  force loss. The later aluminum attempt detuned/stressed the RF path, produced
  an honest 332/1,100 partial, and coincided with persistent satellite-local TX
  completion timeouts that required a hard reset. Treat it as an invalid fade
  and hardware-stress incident; retry MVP-4 only with safe far-field distance
  or off-axis antenna geometry.
- The clean post-reset cycle passed 1,100/1,100 with exact source/ground SHA,
  zero repairs, zero satellite TX timeouts, and no stale partial contamination.
- Implemented on 2026-07-16: Pi/F Prime owns bounded satellite-local channel-2
  radio status and re-enable policy. The Teensy boots the RFM23BP safely `OFF`,
  asserts `RPI_ENABLE` independently, exposes factual `OFF`/`READY` status, and
  performs one SDN/POR initialization attempt per request. F Prime retries at
  `30 s`, `120 s`, then capped `900 s` intervals without depending on RF or
  toggling Pi power.
- HIL-MVP-7 ground USB reconnect passed. Removing the ground Teensy during
  product/transfer 2 made all three ports disappear at 423/1,100 and put the
  receiver into explicit recovery. Replug auto-restored the ports, payload
  receiver, and GDS; the transfer completed after two repair rounds with exact
  source/ground SHA, PING 39008 returned, and the next fresh transfer completed
  1,100/1,100 with zero repairs. Pi PID 255 stayed at zero restarts and the
  satellite reported zero TX timeouts/drops.
- Portable/battery HIL also passed as a nominal control: only the ground Teensy
  was USB-connected to the Mac, the satellite ran from battery, and both ends
  used normal monopoles. PING 39009 passed after the new satellite boot, then
  three fresh 38,480-byte products completed with exact Pi/ground hashes and
  160x120 decode; PINGs 39010-39012 passed and Pi PID 254 stayed at zero
  restarts. Initial short distance/orientation fade windows caused no
  observable loss and remain nominal controls rather than qualification.
- HIL-MVP-4 subsequently passed using real building distance/attenuation with
  no antenna manipulation: the portable GDS was carried outside the lab and
  around the eighth floor while both ends retained normal monopoles and the
  satellite remained battery-powered. Product/transfer 4 accumulated 24
  missing packets, recovered all of them in four selective-repair rounds, and
  completed exact in 70.3 s; PING 39013 returned. Back inside, fresh
  product/transfer 5 completed exact in 64.6 s with zero repairs and PING
  39014. Both decoded 160x120, Pi PID 254 stayed at zero restarts, and their
  exact SHA-256 values are recorded in the core reliability plan.
- A separate handheld Yagi comparison passed indoors at about 15-20 ft. The
  operator stood in one location but waved and mispointed the ground Yagi
  during the transfer; the battery-powered satellite retained a normal
  monopole. PINGs 39015 and 39016 returned, and fresh product/transfer 6
  completed exact in 64.7 s with zero repairs and 160x120 decode. Pi PID 254
  remained at zero restarts. After the ground USB power cycle used for the
  antenna swap, the payload UI temporarily presented completed product 5 as
  `receiving` despite 1,100/1,100 and `crc_ok=true`; product 6 replaced the
  stale presentation cleanly. Treat that label reconciliation as a ground-UI
  follow-up, not evidence of RF corruption.

## C3M RFM23BP Pi-Owned Recovery Finalization (2026-07-16)

- The authoritative plan/evidence is
  `docs/C3M_RFM23BP_KISS_CONTROL_PLAN_2026-07-16.md`.
- Teensy steady states remain only `OFF` and `READY`; factual fault metadata is
  separate. F Prime owns desired-enabled policy and retries after `30 s`,
  `120 s`, then at a capped `900 s` cadence. Channel 2 remains responsive and
  radio recovery never toggles `RPI_ENABLE`.
- The final local-TX refinement forces `OFF + LOCAL_TX_FAULT` through real SDN
  shutdown only after the relay's one bounded FIFO recovery retry also fails.
  RSSI is invalidated on shutdown. Peer silence and ordinary ACK loss do not
  trigger the local hardware reset path.
- Full validation passed 81 Python tests, 7/7 F Prime CTest executables
  (`CommsApp` 12 cases and `CommsDriver_TeensyRfm23` 3 cases), both Teensy
  builds, native F Prime build, and 3/3 exact local C3M demo cycles.
- Satellite firmware was flashed to physical upload ID `usb:2100000`. The
  final ARMv6KZ/VFPv2 release is
  `/home/pi/artemis/releases/c3m-rf-recovery-20260716T213916Z-9e392fb3`, SHA-256
  `9e392fb31ea7e4751a5e18898bf83f55b46f2ae0519de05b9b3b7db3b13dee84`.
  Dictionary SHA-256 is
  `5a961fb5d301097cb0ad0dcd01d6ef2a27709f3156c5f7ed96084b0c4b54716d`.
- Final controlled HIL probe acknowledged `OFF + NONE`; fresh F Prime startup
  then observed `OFF`, issued one `SET_ENABLED`, and reached `READY + NONE` in
  the same second. Post-recovery PING `47164` and `PING_LINK_RSSI` passed.
  Satellite status showed two init attempts and zero terminal TX failures;
  Pi service `NRestarts` remained zero.
- A stale-GDS case was reproduced and explained: local emulation reused the
  global `/tmp/fprime-server-in` and `-out` IPC endpoints while the hardware
  GDS remained open. The web UI stayed HTTP 200 but command UART writes stopped.
  Browser refresh cannot repair that backend; restart the complete GDS process
  tree after local emulation. The final GDS and payload receiver remain in
  detached screens `c3m-gds` and `c3m-payload-hil`.
- Physical-only gates remain: meter/scope proof of SDN, pin 37,
  `RPI_ENABLE`, VCC/current/backfeed/brownout behavior; five true cold cycles
  per node; a safely induced physical init stall/watchdog case; and a physical
  mid-transfer Teensy reset.

## Payload Ground Cancel, Retained Retry, and UVC Guard (2026-07-16)

- `CommsApp` retains the latest successfully captured science descriptor after
  a completed downlink. A later `REQUEST_SCIENCE_DOWNLINK` therefore sends the
  same product with a new transfer ID; a new capture replaces it. Retention is
  intentionally volatile across an F Prime process restart.
- The payload receiver's **Stop & save partial** remains ground-only. Operators
  must wait for `commsApp.DownlinkFinished` / `DownlinkActive = 0` before
  requesting the retained product again because the spacecraft cannot see the
  ground cancel and continues its first transfer.
- HIL exposed a startup UVC frame with a zero-filled tail. The camera callback
  had ignored `data_bytes`, and its validity rule allowed nearly 80% impossible
  pixels. The conservative fix requires exact 160x120 dimensions, at least
  38,400 received bytes, and no more than 1% samples below 1,000 centikelvin.
  The Cubeternet/libuvc Y16 path reports `step=0` even for valid full frames, so
  stride is deliberately not used as a validity gate.
- Full validation passed 87 Python tests, the native deployment build, and 7/7
  F Prime component test executables. The final Pi build verified as ARMv6KZ +
  VFPv2 with `/lib/ld-linux-armhf.so.3`.
- Active release:
  `/home/pi/artemis/releases/c3m-payload-retry-camera-20260716T224440Z-7c23d355`,
  SHA-256
  `7c23d3554e4d6abb0b5c81180190c1113c779fc1271b74ebfb7f687edf5d9e79`.
- Final HIL: product 1 transfer 1 was canceled on the ground at 253/1,100 and
  saved under `data/c3m_20260716_224646_transfer_1/`. After spacecraft
  `DownlinkFinished`, the same product was requested without another capture
  and completed as transfer 2, 1,100/1,100, CRC OK, zero repair rounds, 64.8 s,
  with PING 37121 passing during the transfer. Evidence:
  `data/c3m_20260716_224901_transfer_2/`.
- Spacecraft source and complete ground `.fdp` matched SHA-256
  `1babc1aa35ed840d12b6353cf44cabbd1face542d69cd544384ed9a33ffb472c`.
  Decode was 160x120 / 19,200 pixels, 14.83–24.69 C, with zero invalid pixels.

## HackRF Ground Adapter Qualification (2026-07-22)

- `ground-station/hackrf-rf22/` is a direct-libhackrf ground adapter for the
  existing RFM23BP/RadioHead contract. It exposes stable virtual serial ports
  for channel 0 (`A5`, F Prime GDS) and channel 1 (`A6`, payload receiver);
  channel 2 remains satellite-local. No F Prime, satellite firmware, or system
  architecture refactor was required, and the ground Teensy remains fallback.
- Exact RF filtering requires downlink header `A1 A2 C3 01` and rejects other
  headers; uplink uses `A2 A1 C3 01`. This filters nearby nonmatching nodes but
  cannot distinguish another transmitter deliberately using the same contract.
- The current functional demo qualification is specific to the connected
  HackRF One, 9--10 inch vertical monopole with no attenuator, unchanged
  separation/geometry, USB path, TX gain `16`, RX LNA/VGA `8/8`, and `100 ms`
  zero-IQ settle lead. RF amplifier and antenna bias remain off. The historical
  `50FFD-010`/dipole and TX-gain-47 result is a separate prior geometry.
- Three consecutive fresh scheduled-collection/downlink runs passed while the
  satellite was already verified in Base Mode with live SOH. Product/transfer
  `7/8` completed `1100/1100` in `81.448 s` after four repair rounds and one
  controlled mid-transfer `PING(49104)`. Quiet runs `8/9` and `9/10` completed
  in `64.892 s` and `64.912 s` with zero repairs. All passed CRC, automatic
  160x120 decode, and exact Pi/ground SHA matching.
- The strict product 9 / transfer 10 proof passed the ordered full demo chain
  and matched SHA-256
  `4fa042fab6383c5cfde5687d3b457336ae8251c0400f02166f4416b178dd219e`.
  It was collected after clean supervisor shutdown at
  `/private/tmp/c3m-sdr-monopole/runs/20260722_160907/proof-summary.md` and also
  gates RF path/gain/interlock provenance. The hardware-free suite passed
  `76/76` tests.
- Operator procedures and requalification gates are in
  `docs/HACKRF_GROUND_STATION_RUNBOOK.md`.
- The `codex/hackrf-antenna-requalification` branch changes the launcher to
  fail-closed RX-only operation. TX now requires `--enable-tx`,
  `--tx-safety-confirmed`, and a descriptive `--rf-path-label`; gain above zero
  additionally requires `--allow-elevated-tx-gain`. RX-only mode forces gain
  zero, and the device layer rejects RF-amplifier or antenna-bias enablement.
  Runtime status now records the physical path, TX/RX gains, interlock state,
  and live cs8 rail-clipping/headroom metrics.
- Initial RX measurement on the 9–10 inch monopole/no-attenuator path found
  `1.18%` clipped complex samples at the prior `LNA 16 / VGA 20` setting.
  Gain sweeps at `0/0`, `0/8`, and `8/8` had zero clipped samples; the launcher
  now defaults to and records `LNA 8 / VGA 8`. TX gains `0` and `8` exhausted
  bounded ACK retries without Pi execution; gain `16` succeeded and is the
  minimum qualified step. Final `PING(49105)` received its exact ACK on retry
  2, executed once, and returned GDS Pong count `11` with no TX failure.
- This is functional/demo qualification at the exact tested geometry. Zero
  cs8 clipping cannot prove incident power below HackRF's `-5 dBm` maximum,
  and antenna length cannot prove a 50-ohm match; VNA/SWR and power/link-budget
  measurement remain the electrical-characterization gate.

## Fixed C3M Ground Baseline Decision (2026-07-22)

- The primary student/operator C3M ground path is now the fixed HackRF
  supervisor. The normal command is only:
  `run_hackrf_ground_station.py --enable-tx --tx-safety-confirmed`.
- The supervisor pins the qualified configuration in code: `epscorc3m`, ACK
  mode, TX gain `16`, RX LNA/VGA `8/8`, `100 ms` zero-IQ lead, path label
  `monopole-9to10in-no-attenuator`, RF amplifier off, and antenna bias off.
  The student CLI no longer exposes profile, mode, gain, or path controls.
- There is no AGC, automatic TX power, adaptive profile, or runtime gain
  tuning. If the exact physical path or tested geometry changes, students stop;
  a lead owns requalification.
- Channel-0 blind/degraded repeat is no longer accepted. Channel 0 always uses
  bounded ACK/retry. Channel 1 remains RF-ACK-free because its application
  protocol owns CRC, missing-packet detection, and selective repair.
- `rf22_tx.py` is offline waveform generation only and cannot radiate around
  the supervisor safety gate. The low-level device defaults now match RX
  `8/8`, and strict proof rejects nonbaseline gain, path, settle timing,
  automatic gain control, or channel-0 mode.
- `GDS_Teensy` plus the ground RFM23BP is the cold fallback. Changing ground
  adapters does not change the Pi binary, satellite Teensy firmware, F Prime
  dictionary, RF headers, mission commands, or payload protocol.
- Current HackRF proof is C3M/macOS-specific. Do not claim Neutron-2 `D2` or
  Windows qualification. The historical qualification notes immediately above
  explain how the fixed values were established; they are not current
  student-facing launch instructions.
- Hardware-free HackRF regression after the simplification passes `80/80`
  tests.
