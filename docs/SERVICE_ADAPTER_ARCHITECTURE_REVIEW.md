# Service/Adapter Architecture Review

Date: 2026-06-25

BLUF: the Neutron 2 FlatSat software architecture is fit for purpose. Do not
redesign it. Protect the service/adapter boundary, make current hardware truth
explicit, and remove misleading runtime-selection language before students copy
the pattern.

## Intent

This note preserves the principal-engineer architecture review for future
agents and students. It assumes the current project intent:

- build-time hardware adapter selection
- one active adapter per subsystem in the topology
- services are the durable mission-facing contract
- adapters are the disposable hardware/protocol layer
- mission operations define the service contract before the board-specific
  implementation does

## Load-Bearing Invariant

The service is a hardware-agnostic contract derived from mission needs. Every
hardware-specific fact lives below it, in exactly one adapter.

If this invariant holds, the team can develop on simulated or Artemis prototype
hardware now and replace adapters later without rewriting mission logic.

## Current Verdict

The structure is sound:

- `MissionManager`, `ScienceManager`, `SoHManager`, and `CommsManager` express
  mission behavior.
- subsystem services expose stable spacecraft-facing behavior.
- adapters hide Artemis, D2S2, RFM23BP, simulated payload, and future board
  details.
- topology bindings decide which concrete adapter is active for the build.

The remaining work is contract hygiene and honesty, not a system rewrite.

## Findings Against The Invariant

| Status | Item | Verdict |
| --- | --- | --- |
| Protects | Three-layer split: mission -> service -> adapter | Keep this as the project pattern. |
| Protects | Payload contract using `PayloadCaptureRequest` and `ScienceProductDescriptor` with `PayloadAdapter_NeutronSim` | Treat this as the reference model for new subsystem work. |
| Protects | `EpsService` command/telemetry surface now speaks generic EPS/rail terms | Artemis PDU protocol detail stays in `EpsAdapter_Artemis`. |
| Threatens | ADCS, GPS, thermal, and comms contracts are still thin | Acceptable while hardware is incomplete, but design their service contracts from mission operations instead of waiting for board details. |
| Protects | Mission services report mode results, and `MissionManager` validates transitions | Keep mode ownership in `MissionManager`; services should not own mission state. |
| Protects | Removed `SELECT_RADIO_BACKEND` and `SET_PAYLOAD_SIM_MODE` | Adapter selection is build-time topology work. |
| Protects | Transport headers are generated from `config/transport_constants.json` and checked by local validation | Run `./tools/validate_local.sh` before handoff. |
| Protects | Science product identity is a descriptor, not byte count plus `/tmp` convention | Downlink now receives product ID, bytes, source kind, source path, and CRC. |
| Gap | IMU ownership is undecided | Prefer placing IMU under ADCS unless it becomes an independently commanded/logged subsystem. |

## Real Vs Placeholder Map

| Area | Current contract | Current adapter/hardware | Student-facing truth |
| --- | --- | --- | --- |
| Mission flow | Real MVP story: base mode, scheduled collection, science ready, downlink | no hardware adapter | Students may work on clear mode/event/command behavior. |
| Payload | Real enough for demo: capture duration in, science product descriptor out | `PayloadAdapter_NeutronSim`; real payload adapter later | Copy this service/adapter shape for new work. Do not assume the simulator is the flight payload. |
| COMMS | Mission-level link/downlink state exists | `CommsAdapter_TeensyRfm23`; SatNOGS later | RFM23BP is the MVP path. Runtime radio switching is not real. |
| EPS | Generic service command/status surface | `EpsAdapter_Artemis` over channel 2 to Artemis PDU path | Keep safety guards on rail commands; PDU protocol terms belong in the adapter. Some PDU-shaped diagnostics near EPS are acceptable while the new PDU is tested through F Prime. |
| ADCS | Thin service placeholder | `AdcsAdapter_D2S2` | Define mission ops contract before real ADCS hardware integration. |
| GPS | Thin service/model path | `GpsAdapter_Artemis` | Keep GPS fix/time needs generic; do not bake in a kit-specific module. |
| Thermal | Thin service/model path | `ThermalAdapter_Artemis` | Keep SOH/status small until real sensor/heater path is stable. |
| Storage/downlink | Demo storage descriptor path works | adapter-owned source path plus CRC | Keep descriptor metadata intact through storage, comms, and downlink. |

## Sequenced Plan

### Phase 1 - Honesty Before Student Handoff

Low demo risk, high clarity:

1. Done: removed `SELECT_RADIO_BACKEND`; adapter choice is topology/build-time.
2. Done: removed `SET_PAYLOAD_SIM_MODE`; payload emulation is the wired adapter
   for the local profile, not a runtime toggle.
3. Done: student docs include a real-vs-placeholder table.
4. Done: student docs include `hil` and `local-demo` topology profiles.
5. Done: `PayloadAdapter_NeutronSim` is the canonical copy-me example;
   `PayloadAdapter_N1Legacy` is reference-only and not built by default.

### Phase 2 - Protect Service Contracts

1. Done: de-leak `EpsService`: move PDU/protocol wording and packet-specific detail
   into `EpsAdapter_Artemis`; make the service speak generic EPS concepts such
   as health, rails, faults, voltage/current, and safe operator requests.
2. Define one standard service-to-adapter port-pair template, modeled on the
   payload path.
3. Decide IMU ownership. Default answer: IMU feeds ADCS unless mission
   operations need a separate `ImuService`.

### Phase 3 - Make The Real Vertical Exemplary

1. Done: single-source transport constants in `config/transport_constants.json`
   and generated headers across:
   - `ArtemisRpiTeensy_N2/Components/LinkCfg`
   - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp`
   - `GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp`
2. Done: `tools/validate_local.sh` runs generated-header and transport drift
   checks as part of the standard no-HIL regression path.
3. Done: replace byte-count and `/tmp` coupling with a `ScienceProductDescriptor`,
   for example:

   ```text
   productId
   byteCount
   sourceKind
   sourcePath
   crc
   ```

4. Done: make payload capture active/async so the reference adapter does not block a
   mission/science thread on a shell command or file operation.

### Phase 4 - Correctness Foundations

1. Done: make `MissionManager` the only mode owner.
2. Done: let services report events/results; `MissionManager` validates transitions.
3. Add focused unit tests for `MissionManager`, `ScienceManager`, and
   `CommsManager` decision logic.

## Guardrails For New Subsystems

1. Services are hardware-agnostic. No vendor, board, bus, radio, or protocol
   terms in service ports or telemetry names. If the name says `PDU`, `RFM23`,
   `D2S2`, or a board-specific phrase, it probably belongs in an adapter.
2. One adapter per subsystem is wired at build time. To change hardware, swap
   the adapter in topology and rebuild.
3. Adapters own protocols, buses, packet formats, timing quirks, and hardware
   constants.
4. New subsystem work should copy the payload pattern first:
   `PayloadService` plus `PayloadAdapter_NeutronSim`.
5. Design the service from mission operations first. The adapter can wait for
   hardware; the contract should not.

## Implementation Notes

- Do Phase 1 first if the goal is student handoff clarity.
- For transport constants, update `config/transport_constants.json` first and
  regenerate. Do not hand-edit the generated headers.
- For `PayloadAdapter_N1Legacy`, relabel/archive is safer than deletion unless
  the team confirms nobody needs it as reference.
- EPS de-leak is architecturally important, but the MVP intentionally leaves the
  boundary pragmatic while the new PDU is tested through F Prime. If the PDU
  contract grows, build a fuller adapter or refactor/cull the service surface
  after the hardware behavior is proven.
