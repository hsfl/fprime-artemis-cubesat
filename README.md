# fprime-artemis-cubesat

F' implementation workspace for the Neutron 2 team.

## What this project is

This repository is building a split architecture:
- **Raspberry Pi:** runs F' flight software (`ArtemisRpiTeensy_N2`).
- **Satellite Teensy:** baremetal bridge (`ArtemisTeensy_N2_Baremetal`) for RPi UART and RF23BP transport.
- **Ground Teensy:** baremetal RF bridge (`GDS_Teensy`) that reassembles RF segments to USB and packetizes simple USB uplink bursts back to RF.

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

## Repository layout

- `ArtemisRpiTeensy_N2/`
  - Active F' project (promoted in place from starter sample)
  - Includes deployment and custom components such as `MissionManager`, `ScienceManager`, `SoHManager`, `ThermalService`, `UartChannelMux`, `PayloadDownlinkManager`, `EpsService`, and `EpsAdapter_Artemis`
- `ArtemisTeensy_N2_Baremetal/`
  - Satellite Teensy relay firmware workspace (Arduino CLI workflow)
- `GDS_Teensy/`
  - Ground-station Teensy firmware workspace (Arduino CLI workflow)
- `espcor_teensy_demo/`
  - Legacy/reference demo code (reference-only)
- `docs/`
  - Runbooks and integration notes
- `docs/agents_notes.md`
  - Current implementation status and next-agent guidance
- `EMULATION.md`
  - Local laptop closed-loop emulation workflow (no hardware)
- `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md`
  - Windows laptop setup for student developers and testing/viewer users

## Build and Run

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

Use this on the Pi after cloning the repo at `~/fprime-artemis-cubesat`.

```bash
cd ~/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

Windows note: use WSL2 for F' build/development work. Native Windows is fine for the browser/Python payload viewer path.

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

Not implemented yet:
- HIL validation of channel 2 against the real PDU and HIL validation of channel 0/1 over the RFM23BP pair.
- Broader EPS/PDU telemetry beyond the current command/status path, plus thermal, GPS, and IMU telemetry + command adapter behavior.
- Full uplink robustness (deterministic packet-boundary extraction and retry/ack strategy).
- Full demo-state orchestration polish for `Base Mode` -> scheduled collection -> science downlink.
- Ground-side science-data analysis/presentation workflow finalized for the judges' demo.

## Notes

- Use `docs/archive/` for historical implementation plans, sizing memos, and RF debug notes.
- Use `docs/build_runbook.md` for operational command sequence.
- Use `docs/GDS_TEENSY_RUNBOOK.md` for ground Teensy + UART GDS workflow and troubleshooting.
