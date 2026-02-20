# agents_notes.md

## Project Snapshot (2026-02-13)

This repo is the Neutron 2 team F' integration workspace:
- F' flight software runs on Raspberry Pi.
- Satellite Teensy provides UART wrapper + RF23BP bridge.
- Ground Teensy reassembles RF messages to USB and supports simple USB-burst uplink back to RF.

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
  - `./tools/cross_build_armhf.sh` passes (validated `arm-hf-linux`)
  - `./tools/package_armhf_release.sh` passes and produces signed release bundle

### 1b) ARMHF Release Pipeline (Mac -> RPi)
- Release-grade scripts now live in:
  - `ArtemisRpiTeensy_N2/tools/cross_build_armhf.sh`
  - `ArtemisRpiTeensy_N2/tools/package_armhf_release.sh`
  - `ArtemisRpiTeensy_N2/tools/rollback_armhf_release.sh`
  - Manual deploy + SCP instructions: `rpi_build.instructions`
- Validated behavior:
  - Cross-build emits ARM32 ELF (`arm-hf-linux`) binary + matching topology dictionary
  - Packager creates `releases/release-<timestamp>-<sha>-armhf/` with:
    - deployment binary
    - topology dictionary
    - `SHA256SUMS`
    - `RELEASE_INFO.txt`
  - Manual activation uses `active_release` / `previous_release` symlink pattern.

### 2) Satellite Teensy (`ArtemisTeensy_N2_Baremetal`)
- Source of truth:
  - `firmware/satellite_teensy/satellite_teensy.ino`
  - `firmware/satellite_teensy/src/relay_uart_rf.*`
  - `firmware/satellite_teensy/src/rf23_driver.*`
  - `firmware/satellite_teensy/src/link_protocol.hpp`
  - `firmware/satellite_teensy/src/link_counters.hpp`
- Transport behavior:
  - RPi<->Teensy UART uses custom framing (`0xD4 0xC3 + len + crc16`)
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
- Satellite UART framing is custom and remains required on the RPi<->satellite link.
- RF transport is now segmented and reassembled before UART egress.
- Contract documentation:
  - `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`
  - `GDS_Teensy/docs/transport_contract.md`

## Primary TODO

1. Run manual SCP + activation + smoke steps in `rpi_build.instructions` against live Pi host and record first successful remote release ID.
2. Run full HIL end-to-end tests with real `fprime-gds` UART traffic over RF (both directions).
3. Decide if segment ACK/retry is required for acceptable RF reliability.
4. Add deterministic packet boundary extraction for uplink beyond simple burst mode.
5. Build post-MVP mission/service multiplexing only after chain stability.

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
- ARMHF release runbook:
  - `rpi_build.instructions`

## Agent Reminders

- Prefer retrieval-led reasoning for F' tasks (per `AGENTS.md`).
- Activate venv before F' commands:
  - `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
- Prefer fresh configure before topology/component changes:
  - `fprime-util generate -f`
- For cross-compiling in `nasafprime/fprime-arm`, do not trust container-default `fprime-tools` version.
  - Use project-pinned versions from `lib/fprime/requirements.txt`.
- If container lacks Ninja, generate with `--make`.
