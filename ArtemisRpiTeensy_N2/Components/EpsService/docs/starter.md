# EPS Starter

Owner: Dennis, Isaiah

Target: v1.0 June 30, 2026

Status: wired into deployment.

## Purpose

Mission-facing EPS health/status service.

## Commands

- `REQUEST_EPS_STATUS`

## Abstraction Rule

- `EpsService` owns broad EPS health state.
- `EpsAdapter_Artemis` owns Artemis EPS behavior.
- PDU switch commands belong in `PduService`, not here.

## Next Work

- Keep EPS status stable for SOH telemetry.
- Do not add PDU PCB protocol bytes here.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
