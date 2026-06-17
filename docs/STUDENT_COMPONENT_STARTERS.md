# Student Component Starters

BLUF: service components stay mission-facing and hardware-agnostic. Hardware details belong in adapters.

Target handoff: v1.0 June 30, 2026

## Current Components

| Subsystem | Owners | Service | Adapter / hardware layer | Status |
| --- | --- | --- | --- | --- |
| Payload | Aris, Piper, Kenoi | `PayloadService` | `PayloadAdapter_NeutronSim` now, real payload adapter later | Wired |
| Mission | Aris, Kenoi | `MissionManager` | None | Wired |
| COMMS | Dennis, Joe, Kenoi | `CommsManager` | `CommsAdapter_TeensyRfm23` now, future SatNOGS adapter later | Wired |
| EPS/PDU | Dennis, Isaiah | `EpsService` | `EpsAdapter_Artemis` now, real PDU adapter behavior as ICD settles | Wired |
| ADCS | Piper | `AdcsService` | `AdcsAdapter_D2S2` now, future ADCS adapter later | Wired |
| Thermal | TBD | `ThermalService` | `ThermalAdapter_Artemis` now, real sensor/heater path later | Wired |

## Rule

- Services own commands, state, telemetry, events, and CONOP-level behavior.
- Adapters own board protocols, buses, radios, packet formats, and hardware quirks.
- Mission talks to services, not directly to payload boards, radios, EPS/PDU firmware, or ADCS hardware.

## Base Case

- Payload board is not available yet.
- SatNOGS radio is not available yet.
- Default COMMS path is RFM23BP.
- Default payload path is `PayloadAdapter_NeutronSim`, an emulated neutron-count adapter running on the Raspberry Pi with the F Prime deployment.
- Payload will require its own 28 V power.
- SatNOGS will use lower-voltage power rails such as 3.3 V, 5 V, and VBatt.
- Keep telemetry and science products small while using RFM23BP.
- Increase data budget only after SatNOGS hardware is available and validated.

## What Needs Work

- Payload: use `PayloadService.SCIENCE_CAPTURE(durationSeconds)` for the demo; replace RPi-emulated adapter behavior after the board arrives.
- Mission/Storage: encode the CONOPs flow: base mode, scheduled collect, science product, storage check, downlink.
- Storage: use `StorageService.REPORT_LATEST_DATASET`, `StorageService.REPORT_STORAGE_HISTORY`, and `StorageService.REMOVE_OLD_DATASETS(confirm=1)` for demo/debug visibility. Cleanup is for test/debug use until mission-ops rules are set with the system engineer.
- Ground: use `ground-station/neutron2-payload-viewer/` to review reconstructed neutron-count CSV products; keep this separate from the blob receiver and from `fprime-gds`.
- COMMS: keep `CommsManager` radio-agnostic; use RFM23BP by default and add a SatNOGS adapter when the dev board is available.
- EPS/PDU: keep request-state commands in `EpsService`; these do not turn rails on until adapter behavior is wired. Model payload 28 V separately from SatNOGS 3.3 V/5 V/VBatt rails, then implement the real adapter behavior after the PCB firmware/ICD settles.
- ADCS: keep low priority; command/status skeleton is ready for later D2S2 or hardware implementation.
- Thermal: keep status/model telemetry tiny for SOH; replace the adapter model with real sensor/heater behavior when the hardware path is stable.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
