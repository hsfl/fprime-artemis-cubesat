# fprime-artemis-cubesat

F' implementation workspace for the Neutron 2 team.

## What this project is

This repository is building the Neutron 2 FlatSat demo on Artemis prototype
hardware.

- **Satellite Raspberry Pi:** runs the F' flight-software deployment
  (`ArtemisRpiTeensy_N2`). This is the mission brain: modes, commands,
  telemetry, payload collection orchestration, storage, and science downlink
  requests.
- **Satellite Teensy:** runs baremetal bridge/control firmware
  (`ArtemisTeensy_N2_Baremetal`). This handles the microcontroller-side
  subsystem/radio work: the single Pi UART, local subsystem RPC such as EPS/PDU,
  and the RFM23BP link.
- **Ground Teensy:** runs baremetal RF/USB bridge firmware (`GDS_Teensy`). It
  reassembles RF packets to laptop USB streams and packetizes uplink bytes back
  over RF.
- **Ground laptop:** uses `fprime-gds` for the current MVP command, event, and
  telemetry surface, plus the Neutron 2 payload viewer for reconstructed science
  files.

## Target demo

The current target is a shortened FlatSat FSR end-to-end demo based on the team's system diagram and operator flow. The live demo is not a full mission implementation; it is a controlled proof-of-concept showing command, telemetry, timed data collection, and science-data downlink across the full Raspberry Pi -> satellite Teensy -> RF -> ground Teensy -> ground station chain.

### Target operator story

1. Boot the system into `Base Mode`.
2. On the ground side, use `D2S2` pass-planning inputs to determine the mock ground-contact window and duration for the demo.
3. As the demo "pass" starts, command the vehicle to downlink `SOH`/base-mode telemetry and show live health/status data to judges on the ground display.
4. While still in base mode, send a command that schedules a short data-collection action, for example `10` seconds from now.
5. Trigger the data-collection script/command using simulated or temporary payload data if real payload integration is not ready.
6. After collection completes, transition to a science-data transmit path and downlink the collected payload/science data.
7. On the ground PC, use `fprime-gds` for command/event/telemetry visibility and the Neutron 2 payload viewer for visual review of the downlinked science data. Longer term, the end-goal ground presentation stack is `Yamcs` or another mission-control style analysis/display tool.

### Demo scope assumptions

- Demo timings are intentionally compressed relative to a real pass so the full flow fits into a live presentation window.
- `Base Mode`, data collection, and science downlink are the required user-visible states.
- Simulated payload data is acceptable until a real payload data source is stable enough for the demo.
- Ground-station presentation quality matters: live telemetry, command acknowledgement, and visible science-data review are part of the success criteria.

Current relay milestone:
- one physical UART (`115200 8N1`) on RPi<->satellite Teensy with tagged virtual channels
- channel 0 carries normal F Prime/GDS CCSDS bytes over RF
- channel 1 carries payload/science packets over RF, sized for the current RFM23BP packet budget
- channel 2 carries satellite-Teensy-local subsystem RPC such as EPS/PDU; it is consumed by the satellite Teensy and is not forwarded over RF
- the Pi-side UART wrapper is `0xD4 0xC3 + channel + len + payload + crc16`
- ground USB exposes channel 0 to `fprime-gds`; payload channel 1 is received separately by the payload receiver tool when the ground Teensy triple-serial path is enabled

This UART mux exists because the current hardware implementation gives the Pi
one practical UART path to the satellite Teensy. Keep mission behavior in F'
components and keep UART/RF details behind adapters, `UartChannelMux`, and the
Teensy bridge firmware.

## Repository layout

- `ArtemisRpiTeensy_N2/`
  - Active F' project (promoted in place from starter sample)
  - Includes deployment and custom components such as `MissionManager`, `ScienceManager`, `SoHManager`, `ThermalService`, `UartChannelMux`, `PayloadDownlinkManager`, `EpsService`, and `EpsAdapter_Artemis`
- `ArtemisTeensy_N2_Baremetal/`
  - Satellite Teensy relay firmware workspace (Arduino CLI workflow)
- `GDS_Teensy/`
  - Ground-station Teensy firmware workspace (Arduino CLI workflow)
- `docs/`
  - Runbooks and integration notes
- `docs/agents_notes.md`
  - Current implementation status and next-agent guidance
- `EMULATION.md`
  - Local laptop closed-loop emulation workflow (no hardware)
- `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md`
  - Windows laptop setup for student developers and testing/viewer users

## Build and Run

For the Raspberry Pi Zero W, prefer the Docker cross-compile path for normal
iteration. Native Pi builds work, but they are slow. Use `rpi_build.instructions`
as the fallback/manual path and
`docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md` for the faster handoff path.

### macOS Laptop

Use this when the repo is cloned at `~/Developer/fprime-artemis-cubesat`.

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

Build satellite Teensy bridge:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Build ground Teensy bridge:
```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Run local laptop emulation:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_local_emulation.sh
```

### Windows Laptop (WSL2)

Use this when the repo is cloned inside Ubuntu/WSL at `~/fprime-artemis-cubesat`.

```bash
cd ~/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

Build satellite Teensy bridge:
```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Build ground Teensy bridge:
```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Run local laptop emulation:
```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_local_emulation.sh
```

### Raspberry Pi Target

Use this on the Pi after cloning the repo at `~/fprime-artemis-cubesat`. This is
the manual/native path; cross-compile is preferred for normal iteration.

```bash
cd ~/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

Windows note: use WSL2 for F' build/development work. Native Windows is fine for the browser/Python payload viewer path.

## F Prime Version

This branch is pinned to F Prime `v4.2.1`:

```bash
git -C ArtemisRpiTeensy_N2/lib/fprime describe --tags --dirty --always --long
```

Use `describe --tags` when checking the framework version. F Prime `v4.2.x` tags
are lightweight tags, so plain `git describe` or parent `git submodule status`
can misleadingly report a `v3.1.1-...` description for the same commit.

## Status

Implemented:
- F' deployment migrated to Linux UART transport.
- Satellite and ground Teensy relay firmware with channelized UART framing plus RF segmentation/reassembly.
- Channel 0 CCSDS/GDS path, channel 1 payload/science path, and channel 2 satellite-local EPS/PDU RPC path.
- Ground Teensy simple uplink path (USB raw byte burst -> RF segmentation for channels that cross RF).
- Updated UART/RF transport contract documentation.
- RPi-hosted neutron payload simulator wired through `PayloadService` and `PayloadAdapter_NeutronSim`, including a latest-capture handoff for downlink.
- File-backed `PayloadDownlinkManager` and payload receiver tooling for arbitrary payload bytes over channel 1.
- Artemis EPS/PDU command adapter over channel 2 using the PDU v2 protocol from `external/artemis-pdu`, with timeout/recovery handling.
- HIL proof of the shortened demo story over the real RPi UART, satellite
  Teensy, RFM23BP pair, ground Teensy, `fprime-gds`, payload receiver, and
  payload viewer path. See `docs/RF_MVP_DEMO_RUNBOOK.md`.

Not implemented yet:
- HIL validation of channel 2 against the real PDU.
- RF/GDS cleanup to reduce APID sequence-count warnings on lossy channel 0 traffic.
- Broader EPS/PDU telemetry beyond the current command/status path, plus thermal, GPS, and IMU telemetry + command adapter behavior.
- Full uplink robustness (deterministic packet-boundary extraction and retry/ack strategy).
- Full demo-state orchestration polish for `Base Mode` -> scheduled collection -> science downlink.
- Longer-term ground-side presentation beyond the current `fprime-gds` plus
  Neutron 2 payload viewer MVP.

## Notes

- Use `docs/archive/` for historical implementation plans, sizing memos, and RF debug notes.
- Use `docs/SYSTEM_ARCHITECTURE.md` for the current Neutron 2-on-Artemis architecture.
- Use `docs/RF_MVP_DEMO_RUNBOOK.md` for the real hardware demo flow.
- Use `EMULATION.md` and `docs/NEUTRON2_LOCAL_EMULATION_RUNBOOK.md` for laptop-only rehearsal.
- Use `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md` for Windows student setup.
