# agents_notes.md

## Project Snapshot (2026-02-13)

This repo is the Neutron 2 team F' integration workspace:
- F' flight software runs on Raspberry Pi.
- Satellite Teensy provides UART wrapper + RF23BP bridge.
- Ground Teensy reassembles RF messages to USB and supports simple USB-burst uplink back to RF.

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
  - `Components/TeensyLink`
  - `Components/PingResponder`
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

## Primary TODO

1. Record first successful non-crashing runtime on `/dev/serial0` using the real UART path.
2. Run full HIL end-to-end tests with real `fprime-gds` UART traffic over RF (both directions).
3. Implement the minimum demo-state flow for `Base Mode` -> scheduled data collection -> science-data downlink.
4. Decide and document the payload-data source for the demo: real payload path vs simulated temporary data.
5. Keep `fprime-gds` as the live MVP demo ground interface and treat `Yamcs` as the post-MVP target presentation/analysis stack.
6. Decide if segment ACK/retry is required for acceptable RF reliability during the live demo.
7. Add deterministic packet boundary extraction for uplink beyond simple burst mode if required by the selected demo flow.
8. Build post-MVP mission/service multiplexing only after chain stability.

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
