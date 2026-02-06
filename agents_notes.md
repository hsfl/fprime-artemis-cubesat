# agents_notes.md

## Project Snapshot (2026-02-06)

This repo is the Neutron 2 team F' integration workspace:
- F' flight software runs on Raspberry Pi.
- Teensy is a baremetal bridge/adapter for hardware-facing interfaces.
- Single UART between RPi and Teensy is the current MVP transport.

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

### 2) Teensy side (`ArtemisTeensy_N2_Baremetal`)
- Source of truth:
  - `firmware/satellite_teensy/satellite_teensy.ino`
  - `firmware/satellite_teensy/src/relay_uart_rf.*`
  - `firmware/satellite_teensy/src/rf23_driver.*`
  - `firmware/satellite_teensy/src/link_protocol.hpp`
  - `firmware/satellite_teensy/src/link_counters.hpp`
- Startup behavior now includes:
  - RPi enable pin asserted HIGH (`pin 36`)
  - Teensy LED asserted HIGH (`pin 13`)
- Build status:
  - `./tools/arduino-cli/build.sh` passes for `teensy:avr:teensy41`

## Important Clarification: Framing

- Current UART link uses a custom transport wrapper documented in:
  - `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`
- This means current chain behavior is **not pure end-to-end F' framing** between laptop GDS and RPi over Teensy.
- Current custom wrapper exists for MVP relay bring-up (bounded payload, CRC, timeout/drop counters, simple local control commands).

## GDS + Chain Test Notes

- `fprime-gds` locally confirms:
  - `--communication-selection {uart,ip,none}`
  - `--uart-device`, `--uart-baud`, `--uart-skip-port-check`
- Current feasibility:
1. GDS on RPi: works.
2. RPi <-> Teensy packet path visibility via USB logs: partial (needs explicit byte logging to observe packets).
3. RPi -> Teensy -> laptop direct GDS bytes (no radio): blocked until transparent bridge mode is added.
4. RPi -> Teensy -> RF23 -> laptop SDR: blocked until SDR byte recovery/adapter path exists.

## Primary TODO

1. Add transparent Teensy bridge mode (`Serial2 <-> USB`) for end-to-end laptop GDS UART testing.
2. Decide whether to keep custom UART wrapper as optional diagnostic mode or remove it.
3. Implement post-MVP mission/service multiplexing only after chain stability.
4. Build RF ground receive path (second RF23 node or SDR decoder + adapter).

## Important Paths

- F' project root:
  - `ArtemisRpiTeensy_N2`
- Teensy baremetal project:
  - `ArtemisTeensy_N2_Baremetal`
- Quick test guide:
  - `GET_STARTED_TESTING.md`
- Build runbook:
  - `docs/build_runbook.md`

## Agent Reminders

- Prefer retrieval-led reasoning for F' tasks (per `AGENTS.md`).
- Activate venv before F' commands:
  - `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
- Prefer fresh configure before topology/component changes:
  - `fprime-util generate -f`
