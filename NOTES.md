# NOTES.md

## Project Snapshot (2026-02-05)

This repo is the Neutron 2 team F' integration workspace:
- F' flight software runs on Raspberry Pi.
- Teensy acts as a baremetal bridge/adapter for hardware-facing interfaces.
- Single UART between RPi and Teensy is the current MVP transport.

## Current Implemented State

### 1) F' side (`ArtemisRpiTeensy_N2`)
- Promoted the sample project in place (not copied to a new root).
- Deployment transport was changed from TCP to Linux UART (`Drv.LinuxUartDriver`).
- Deployment binary now uses `-d <uart_device>` (default `/dev/serial0`).
- Added custom MVP components:
  - `Components/TeensyLink`
  - `Components/PingResponder`
- Topology wiring includes:
  - `teensyLink.run` in rate group 1
  - `comDriver.run` in rate group 3
  - health ping entries for `teensyLink` and `pingResponder`

### 2) Teensy side (`ArtemisTeensy_N2_Baremetal`)
- New baremetal workspace was created.
- Relay MVP code exists with modular layout:
  - `firmware/satellite_teensy/satellite_teensy.ino`
  - `src/relay_uart_rf.*`
  - `src/rf23_driver.*`
  - `src/link_protocol.hpp`
  - `src/link_counters.hpp`
- UART contract document:
  - `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`

### 3) Validation already completed
- F' build validated:
  - `fprime-util generate -f` passes
  - `fprime-util build` passes
- Teensy compile validated:
  - `./ArtemisTeensy_N2_Baremetal/tools/arduino-cli/build.sh` passes for `teensy:avr:teensy41`

## Design Intent

- Teensy currently serves as transport adapter/bridge and hardware owner.
- RPi/F' owns command/event/channel/parameter semantics.
- Payload integration to F' is planned but **not implemented**.

## Explicit TODO (not implemented yet)

1. Generic payload integration into F' components/topology.
2. GPS/IMU/PDU proxy components between Teensy and F'.
3. Mission-level packet/service multiplexing beyond current MVP framing.
4. Hardware-in-loop validation scripts and automated fault-injection checks.

## Important Paths

- Main F' project:
  - `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2`
- Teensy baremetal project:
  - `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal`
- Reference-only legacy demo (do not modify for MVP implementation work):
  - `/Users/sozodennis/Developer/fprime-artemis-cubesat/espcor_teensy_demo`
- Build runbook:
  - `/Users/sozodennis/Developer/fprime-artemis-cubesat/docs/build_runbook.md`

## Agent Guidance

- Prefer retrieval-led reasoning for F' tasks (per `AGENTS.md`).
- Activate venv before all F' commands:
  - `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
- Generate before component/deployment edits:
  - `fprime-util generate -f`
- Keep MVP assumptions unless explicitly changed:
  - single UART
  - Teensy bridge role
  - F' semantics on RPi
