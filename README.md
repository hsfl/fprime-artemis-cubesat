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
7. On the ground PC, use `fprime-gds` as the MVP demo ground tool to review the downlinked science data. Longer term, the end-goal ground presentation stack is `Yamcs` or another mission-control style analysis/display tool.

### Demo scope assumptions

- Demo timings are intentionally compressed relative to a real pass so the full flow fits into a live presentation window.
- `Base Mode`, data collection, and science downlink are the required user-visible states.
- Simulated payload data is acceptable until a real payload data source is stable enough for the demo.
- Ground-station presentation quality matters: live telemetry, command acknowledgement, and visible science-data review are part of the success criteria.

Current relay milestone:
- one UART channel (`115200 8N1`) on RPi<->satellite Teensy with custom wrapper
- segmented RF transport between satellite and ground Teensy
- raw reassembled F' bytes emitted on ground USB UART
- simple uplink burst packetization from ground USB UART to RF

## Repository layout

- `ArtemisRpiTeensy_N2/`
  - Active F' project (promoted in place from starter sample)
  - Includes deployment and custom components (`TeensyLink`, `PingResponder`)
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
  - Local Mac-only closed-loop emulation workflow (no hardware)

## Build and run

### 1) Build F' (RPi side)
```bash
cd <repo-root>
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

Run deployment:
```bash
cd ArtemisRpiTeensy_N2
./build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

### 2) Build satellite Teensy bridge
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Upload (example port):
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

### 3) Build ground Teensy bridge
```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh
```

Upload (example port):
```bash
cd GDS_Teensy
./tools/arduino-cli/upload.sh /dev/ttyACM1
```

## Status

Implemented:
- F' deployment migrated to Linux UART transport.
- Satellite Teensy relay with custom UART wrapper and RF segmentation/reassembly.
- Ground Teensy relay with RF reassembly to USB raw F' bytes.
- Ground Teensy simple uplink path (USB raw byte burst -> RF segmentation).
- Updated UART/RF transport contract documentation.

Not implemented yet:
- Generic payload component connected to F'.
- Full proxy components for PDU/GPS/IMU telemetry + commands.
- Full uplink robustness (deterministic packet-boundary extraction and retry/ack strategy).
- Full demo-state orchestration for `Base Mode` -> scheduled collection -> science downlink.
- Ground-side science-data analysis/presentation workflow finalized for the judges' demo.

## Notes

- Use `docs/FPRIME_ARTEMIS_CUBESAT.md` for the original implementation plan.
- Use `docs/build_runbook.md` for operational command sequence.
- Use `docs/GDS_TEENSY_RUNBOOK.md` for ground Teensy + UART GDS workflow and troubleshooting.
