# fprime-artemis-cubesat

F' implementation workspace for the Neutron 2 team.

## What this project is

This repository is building a split architecture:
- **Raspberry Pi:** runs F' flight software (`ArtemisRpiTeensy_N2`).
- **Satellite Teensy:** baremetal bridge (`ArtemisTeensy_N2_Baremetal`) for RPi UART and RF23BP transport.
- **Ground Teensy:** baremetal RF bridge (`GDS_Teensy`) that reassembles RF segments to USB and packetizes simple USB uplink bursts back to RF.

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

## Notes

- Use `docs/FPRIME_ARTEMIS_CUBESAT.md` for the original implementation plan.
- Use `docs/build_runbook.md` for operational command sequence.
- Use `docs/GDS_TEENSY_RUNBOOK.md` for ground Teensy + UART GDS workflow and troubleshooting.
