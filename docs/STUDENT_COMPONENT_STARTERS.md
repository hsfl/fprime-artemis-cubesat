# Student Component Starters

BLUF: manager components stay mission-facing and hardware-agnostic. Hardware details belong in drivers.

Target handoff: v1.0 June 30, 2026

## Architecture Invariant

The manager is a hardware-agnostic contract derived from mission needs. Every
hardware-specific fact lives below it, in exactly one driver.

If this holds, the team develops on simulated or Artemis prototype hardware now
and swaps drivers later without rewriting mission logic. Driver wiring is a
topology choice, not a runtime command.

Related docs:

- `docs/MISSION_OPS_QUICK_RUN.md` is the short operator-facing run/checklist.
- `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md` explains where to look first
  when a command, event, telemetry channel, payload downlink, viewer, or
  EPS/PDU path fails.
- `docs/archive/SERVICE_ADAPTER_ARCHITECTURE_REVIEW_2026-06-25.md` is the
  historical record of the cleanup that established these rules.

Topology rule:

- `Top/topology.fpp` is the single topology for laptop rehearsal and HIL.
- Keep RF-budget-sensitive periodic manager loops disabled unless they are
  needed by the actual demo path.

## Current Components

| Subsystem | Owners | Application / Manager | Driver / hardware layer | Status |
| --- | --- | --- | --- | --- |
| Payload | Aris, Piper, Kenoi | `PayloadManager` | `PayloadDriver_NeutronSim` now, real payload driver later | Wired |
| Mission | Aris, Kenoi | `MissionApp` | None | Wired |
| COMMS | Dennis, Joe, Kenoi | `CommsApp` | `CommsDriver_TeensyRfm23` now, future SatNOGS driver later | Wired |
| EPS | Dennis, Isaiah | `EpsManager` | `EpsDriver_Artemis` now, real EPS/PDU driver behavior as ICD settles | Wired |
| ADCS | Piper | `AdcsManager` | `AdcsDriver_D2S2` now, future ADCS driver later | Wired |
| GPS | TBD | `GpsManager` | `GpsDriver_Artemis` now, future GPS hardware driver later | Wired |
| Thermal | TBD | `ThermalManager` | `ThermalDriver_Artemis` now, real sensor/heater path later | Wired |

## Real vs Placeholder Map

| Area | Current contract | Driver / hardware | Student-facing truth |
| --- | --- | --- | --- |
| Mission flow | Real MVP story: base mode, scheduled collect, science ready, downlink | no hardware driver | Work on clear mode/event/command behavior. |
| Payload | Real enough for demo: capture duration in, `ScienceProductDescriptor` out | `PayloadDriver_NeutronSim`; real board later | Copy this manager/driver shape. The simulator is not the flight payload. |
| COMMS | Mission-level link/downlink state | `CommsDriver_TeensyRfm23`; SatNOGS later | RFM23BP is the MVP path. No runtime radio switching. |
| EPS | Generic manager command/status surface | `EpsDriver_Artemis` over channel 2 to Artemis PDU | Keep rail-command safety guards; PDU terms stay in the driver. |
| ADCS | Thin manager placeholder | `AdcsDriver_D2S2` | Define the mission-ops contract before real ADCS hardware. |
| GPS | Thin manager/model path | `GpsDriver_Artemis` | Keep fix/time needs generic; do not bake in a kit-specific module. |
| Thermal | Thin manager/model path | `ThermalDriver_Artemis` | Keep SOH/status small until the real sensor/heater path is stable. |
| Storage/downlink | Descriptor path works end to end | driver-owned source path + CRC | Keep descriptor metadata intact through storage, comms, and downlink. |

## Rule

- Managers own commands, state, telemetry, events, and CONOP-level behavior, in
  hardware-agnostic terms. Avoid vendor/board/bus/radio/protocol words (`PDU`,
  `RFM23`, `D2S2`) in manager ports or telemetry names.
- Drivers own board protocols, buses, radios, packet formats, timing quirks, and hardware constants.
- Applications talk to managers, not directly to payload boards, radios, EPS/PDU firmware, or ADCS hardware.
- One driver per subsystem, wired in the topology. Missing hardware gets a simulator driver in the topology, not a runtime command.
- New subsystem work copies the payload pattern first: `PayloadManager` plus `PayloadDriver_NeutronSim`.
- Design the manager from mission operations first. The driver can wait for hardware; the contract should not.

## Base Case

- Payload board is not available yet.
- SatNOGS radio is not available yet.
- Default COMMS path is RFM23BP.
- Default payload path is `PayloadDriver_NeutronSim`, an emulated neutron-count driver running on the Raspberry Pi with the F Prime deployment.
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
- channel 2 carries satellite-local subsystem RPC, such as EPS driver
  requests, and is not forwarded to the ground

The minimum technical story is:

```text
GDS command
-> CommsApp.REQUEST_SCIENCE_DOWNLINK
-> PayloadDownlinkApp packetizes the latest stored product
-> satellite Teensy sends channel 1 packets over RFM23BP
-> ground Teensy forwards channel 1 packets to the payload USB port
-> payload_receiver reconstructs and CRC-checks the file
-> payload viewer parses the file
```

Progress belongs in F Prime events. The current `PayloadDownlinkApp` emits
`PayloadDownlinkProgress` at nominal `10%` increments and completes with
`PayloadDownlinkComplete` plus `CommsApp.DownlinkFinished`.

APID sequence warnings in GDS mean the RF/GDS telemetry stream dropped packets.
They do not automatically mean the payload failed. The payload pass/fail check
is: receiver completes, local payload hash matches the Pi latest payload hash,
and the viewer parses the result.

## What Needs Work

- Payload: use `PayloadManager.SCIENCE_CAPTURE(durationSeconds)` for the demo; products now move through a `ScienceProductDescriptor` with product ID, byte count, source kind, source path, and CRC. Replace RPi-emulated driver behavior after the board arrives.
- Mission/Storage: encode the CONOPs flow: base mode, scheduled collect, science product, storage check, downlink.
- Storage: use `StorageManager.REPORT_LATEST_DATASET`, `StorageManager.REPORT_STORAGE_HISTORY`, and `StorageManager.REMOVE_OLD_DATASETS(confirm=1)` for demo/debug visibility. Cleanup is for test/debug use until mission-ops rules are set with the system engineer.
- Ground: use `fprime-gds` for commands/events/telemetry/progress, `tools/payload_receiver.py` for channel 1 reconstruction, and `ground-station/neutron2-payload-viewer/` for neutron-count CSV review.
- COMMS: keep `CommsApp` radio-agnostic; use RFM23BP by default and add a SatNOGS driver when the dev board is available.
- EPS: keep request-state commands in `EpsManager`; safety-confirmed rail commands are generic EPS commands while the Artemis/PDU protocol details stay in `EpsDriver_Artemis`. Model payload 28 V separately from SatNOGS 3.3 V/5 V/VBatt rails, then implement the real driver behavior after the PCB firmware/ICD settles.
- ADCS: keep low priority; command/status skeleton is ready for later D2S2 or hardware implementation.
- Thermal: keep status/model telemetry tiny for SOH; replace the driver model with real sensor/heater behavior when the hardware path is stable.

## Standard Local Validation

```bash
./tools/validate_local.sh
```

This is the no-HIL command to run before handing work to another student or
mission ops. It checks generated transport headers, local Python tests, the
native unified-topology build, component unit tests, and the automated local demo
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

- `EpsManager` stays mission-facing and speaks generic EPS/rail commands.
- `EpsDriver_Artemis` owns PDU v2 protocol mapping, channel 2 local RPC, and
  hardware response interpretation.
- If the PDU contract grows enough that the manager boundary becomes confusing,
  create a fuller driver or refactor/cull the manager surface later. Do not
  block the MVP on making the boundary perfect today.
