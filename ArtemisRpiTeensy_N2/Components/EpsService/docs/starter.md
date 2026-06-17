# EPS/PDU Starter

Owner: Dennis, Isaiah

Target: v1.0 June 30, 2026

Status: wired into deployment.

## Purpose

Mission-facing EPS/PDU health, telemetry, and power-control service.

## Commands

- `REQUEST_EPS_STATUS`
- Future PDU rail/switch request commands should be added here when the PDU command contract is ready.

## Abstraction Rule

- `EpsService` owns broad EPS/PDU health state, request-state telemetry, and mission-facing power commands.
- `EpsAdapter_Artemis` owns Artemis EPS/PDU behavior and any hardware-specific translation.
- Do not split PDU into a separate F Prime service; model PDU behavior under `EpsService` and keep hardware-specific work in the adapter.

## Next Work

- Keep EPS/PDU status stable for SOH telemetry.
- Keep PDU PCB protocol bytes in the adapter or external PDU firmware package, not in mission-facing service logic.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
