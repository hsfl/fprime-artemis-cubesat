# Student Component Starters

BLUF: service components stay mission-facing and hardware-agnostic. Hardware details belong in adapters.

Target handoff: v1.0 June 30, 2026

Related architecture review:

- `docs/SERVICE_ADAPTER_ARCHITECTURE_REVIEW.md` records the service/adapter
  invariant, current "real vs placeholder" map, and follow-on cleanup plan.
- `docs/MISSION_OPS_QUICK_RUN.md` is the short operator-facing run/checklist.
- `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md` explains where to look first
  when a command, event, telemetry channel, payload downlink, viewer, or
  EPS/PDU path fails.

Topology profiles:

| Profile | Use when | What it means |
| --- | --- | --- |
| `hil` | FlatSat/bench or target hardware path | Merge-safe default; keeps noisy local demo loops off. |
| `local-demo` | Laptop/RPi local emulation without bench access | Enables the scheduled science path and visible service loops for local testing. |

## Current Components

| Subsystem | Owners | Service | Adapter / hardware layer | Status |
| --- | --- | --- | --- | --- |
| Payload | Aris, Piper, Kenoi | `PayloadService` | `PayloadAdapter_NeutronSim` now, real payload adapter later | Wired |
| Mission | Aris, Kenoi | `MissionManager` | None | Wired |
| COMMS | Dennis, Joe, Kenoi | `CommsManager` | `CommsAdapter_TeensyRfm23` now, future SatNOGS adapter later | Wired |
| EPS | Dennis, Isaiah | `EpsService` | `EpsAdapter_Artemis` now, real EPS/PDU adapter behavior as ICD settles | Wired |
| ADCS | Piper | `AdcsService` | `AdcsAdapter_D2S2` now, future ADCS adapter later | Wired |
| Thermal | TBD | `ThermalService` | `ThermalAdapter_Artemis` now, real sensor/heater path later | Wired |

## Rule

- Services own commands, state, telemetry, events, and CONOP-level behavior.
- Adapters own board protocols, buses, radios, packet formats, and hardware quirks.
- Mission talks to services, not directly to payload boards, radios, EPS/PDU firmware, or ADCS hardware.
- Missing hardware gets a simulator adapter selected by topology profile, not a runtime command.

## Base Case

- Payload board is not available yet.
- SatNOGS radio is not available yet.
- Default COMMS path is RFM23BP.
- Default payload path is `PayloadAdapter_NeutronSim`, an emulated neutron-count adapter running on the Raspberry Pi with the F Prime deployment.
- Payload will require its own 28 V power.
- SatNOGS will use lower-voltage power rails such as 3.3 V, 5 V, and VBatt.
- Keep telemetry and science products small while using RFM23BP.
- Increase data budget only after SatNOGS hardware is available and validated.

## KISS Demo Data Path

Students should think of the demo as three separate jobs:

1. `fprime-gds` is for commands, events, telemetry, and payload-transfer
   progress.
2. `tools/payload_receiver.py` is for reconstructing the channel 1 payload file.
3. `ground-station/neutron2-payload-viewer` is for opening the reconstructed
   science file and checking the neutron-count data.

The radio is shared, but the software keeps the streams separate:

- channel 0 carries normal F Prime/GDS bytes
- channel 1 carries payload/science-product packets
- channel 2 carries satellite-local subsystem RPC, such as EPS adapter
  requests, and is not forwarded to the ground

The minimum technical story is:

```text
GDS command
-> CommsManager.REQUEST_SCIENCE_DOWNLINK
-> PayloadDownlinkManager packetizes the latest stored product
-> satellite Teensy sends channel 1 packets over RFM23BP
-> ground Teensy forwards channel 1 packets to the payload USB port
-> payload_receiver reconstructs and CRC-checks the file
-> payload viewer parses the file
```

Progress belongs in F Prime events. The current `PayloadDownlinkManager` emits
`PayloadDownlinkProgress` at nominal `10%` increments and completes with
`PayloadDownlinkComplete` plus `CommsManager.DownlinkFinished`.

APID sequence warnings in GDS mean the RF/GDS telemetry stream dropped packets.
They do not automatically mean the payload failed. The payload pass/fail check
is: receiver completes, local payload hash matches the Pi latest payload hash,
and the viewer parses the result.

## What Needs Work

- Payload: use `PayloadService.SCIENCE_CAPTURE(durationSeconds)` for the demo; products now move through a `ScienceProductDescriptor` with product ID, byte count, source kind, source path, and CRC. Replace RPi-emulated adapter behavior after the board arrives.
- Mission/Storage: encode the CONOPs flow: base mode, scheduled collect, science product, storage check, downlink.
- Storage: use `StorageService.REPORT_LATEST_DATASET`, `StorageService.REPORT_STORAGE_HISTORY`, and `StorageService.REMOVE_OLD_DATASETS(confirm=1)` for demo/debug visibility. Cleanup is for test/debug use until mission-ops rules are set with the system engineer.
- Ground: use `fprime-gds` for commands/events/telemetry/progress, `tools/payload_receiver.py` for channel 1 reconstruction, and `ground-station/neutron2-payload-viewer/` for neutron-count CSV review.
- COMMS: keep `CommsManager` radio-agnostic; use RFM23BP by default and add a SatNOGS adapter when the dev board is available.
- EPS: keep request-state commands in `EpsService`; safety-confirmed rail commands are generic EPS commands while the Artemis/PDU protocol details stay in `EpsAdapter_Artemis`. Model payload 28 V separately from SatNOGS 3.3 V/5 V/VBatt rails, then implement the real adapter behavior after the PCB firmware/ICD settles.
- ADCS: keep low priority; command/status skeleton is ready for later D2S2 or hardware implementation.
- Thermal: keep status/model telemetry tiny for SOH; replace the adapter model with real sensor/heater behavior when the hardware path is stable.

## Standard Local Validation

```bash
./tools/validate_local.sh
```

This is the no-HIL command to run before handing work to another student or
mission ops. It checks generated transport headers, local Python tests, the
`local-demo` native build, component unit tests, and the automated local demo
sequence.

Transport constants are generated from:

```text
config/transport_constants.json
```

If RF/UART constants need to change, update the manifest and regenerate:

```bash
python3 tools/generate_transport_constants.py
python3 tools/check_transport_constants.py
```

Do not hand-edit `LinkCfg.hpp` or either Teensy `link_protocol.hpp`.

## EPS/PDU MVP Boundary

The current EPS path intentionally has some Artemis PDU bleed-through because
the team plans to test the new PDU through F Prime. This is acceptable for the
MVP.

Current rule:

- `EpsService` stays mission-facing and speaks generic EPS/rail commands.
- `EpsAdapter_Artemis` owns PDU v2 protocol mapping, channel 2 local RPC, and
  hardware response interpretation.
- If the PDU contract grows enough that the service boundary becomes confusing,
  create a fuller adapter or refactor/cull the service surface later. Do not
  block the MVP on making the boundary perfect today.
