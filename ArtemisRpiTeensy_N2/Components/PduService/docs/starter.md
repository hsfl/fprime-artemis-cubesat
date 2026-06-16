# PDU Starter

Owner: Dennis, Isaiah

Target: v1.0 June 30, 2026

Status: builds, not wired into deployment yet.

## Purpose

Mission-facing PDU planning and telemetry placeholder. It records requested rail states only; it does not turn rails on.

Base case: payload needs a 28 V rail. SatNOGS will use lower-voltage rails such as 3.3 V, 5 V, and VBatt.

## Commands

- `REQUEST_PDU_STATUS`
- `SET_PAYLOAD_28V_REQUEST(requestState)`
- `SET_SATNOGS_POWER_REQUEST(requestState)`

## Abstraction Rule

- `PduService` owns request commands and request-state telemetry.
- Future PDU adapter owns PCB firmware protocol, bus details, and safety interlocks.
- Keep payload 28 V control separate from SatNOGS rail control.
- Do not wire real power switching until the PDU ICD is stable.

## Next Work

- Confirm PDU requirements with Junjie and Jarrel.
- Confirm payload 28 V behavior.
- Confirm SatNOGS 3.3 V, 5 V, and VBatt rail requirements.
- Add and wire a PDU adapter after PCB firmware settles.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
