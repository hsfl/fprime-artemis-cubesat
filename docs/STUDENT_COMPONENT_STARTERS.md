# Student Component Starters

BLUF: service components stay mission-facing and hardware-agnostic. Hardware details belong in adapters.

Target handoff: v1.0 June 30, 2026

## Current Components

| Subsystem | Owners | Service | Adapter / hardware layer | Status |
| --- | --- | --- | --- | --- |
| Payload | Aris, Piper, Kenoi | `PayloadService` | RPi-hosted emulated payload adapter now, real payload adapter later | Wired |
| Mission | Aris, Kenoi | `MissionManager` | None | Wired |
| COMMS | Dennis, Joe, Kenoi | `CommsManager` | `CommsAdapter_TeensyRfm23` now, future SatNOGS adapter later | Wired |
| PDU | Dennis, Isaiah | `PduService` | Future PDU adapter | Builds, not wired |
| ADCS | Piper | `AdcsService` | `AdcsAdapter_D2S2` now, future ADCS adapter later | Wired |

## Rule

- Services own commands, state, telemetry, events, and CONOP-level behavior.
- Adapters own board protocols, buses, radios, packet formats, and hardware quirks.
- Mission talks to services, not directly to payload boards, radios, PDU firmware, or ADCS hardware.

## Base Case

- Payload board is not available yet.
- SatNOGS radio is not available yet.
- Default COMMS path is RFM23BP.
- Default payload path is an emulated adapter running on the Raspberry Pi with the F Prime deployment.
- Payload will require its own 28 V power.
- SatNOGS will use lower-voltage power rails such as 3.3 V, 5 V, and VBatt.
- Keep telemetry and science products small while using RFM23BP.
- Increase data budget only after SatNOGS hardware is available and validated.

## What Needs Work

- Payload: keep basic commands now; replace RPi-emulated adapter behavior after the board arrives.
- Mission: encode the CONOPs flow: base mode, scheduled collect, science product, downlink.
- COMMS: keep `CommsManager` radio-agnostic; use RFM23BP by default and add a SatNOGS adapter when the dev board is available.
- PDU: keep request-state commands ready now; these do not turn rails on. Model payload 28 V separately from SatNOGS 3.3 V/5 V/VBatt rails, then wire to a real adapter after the PCB firmware/ICD settles.
- ADCS: keep low priority; command/status skeleton is ready for later D2S2 or hardware implementation.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
