# fprime-artemis-cubesat

F' implementation workspace for the Neutron 2 team.

## What this project is

This repository is building a split architecture:
- **Raspberry Pi:** runs F' flight software (`ArtemisRpiTeensy_N2`).
- **Teensy:** baremetal bridge (`ArtemisTeensy_N2_Baremetal`) that interfaces hardware-facing links and peripherals (PDU, GPS, IMU, radio sensors path) and relays data over UART to/from the RPi.

Current milestone is **Relay MVP**:
- one UART channel (`115200 8N1`)
- RPi F' deployment over UART driver
- Teensy relay bridge with link counters + minimal control commands

## Repository layout

- `ArtemisRpiTeensy_N2/`
  - Active F' project (promoted in place from starter sample)
  - Includes deployment and custom components (`TeensyLink`, `PingResponder`)
- `ArtemisTeensy_N2_Baremetal/`
  - Teensy relay firmware workspace (Arduino CLI workflow)
- `espcor_teensy_demo/`
  - Legacy/reference demo code (reference-only)
- `docs/`
  - Runbooks and integration notes
- `NOTES.md`
  - Current implementation status and next-agent guidance

## Build and run

### 1) Build F' (RPi side)
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

Run deployment:
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

### 2) Build Teensy bridge
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Upload (example port):
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## Status

Implemented:
- F' deployment migrated to Linux UART transport.
- Teensy relay baremetal skeleton implemented and compiling.
- MVP UART contract documented.

Not implemented yet:
- Generic payload component connected to F'.
- Full proxy components for PDU/GPS/IMU telemetry + commands.
- Post-MVP mission protocol integration and HIL automation.

## Notes

- Use `FPRIME_ARTEMIS_CUBESAT.md` for the original implementation plan.
- Use `docs/build_runbook.md` for operational command sequence.
