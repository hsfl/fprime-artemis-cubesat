# Thermal Starter

Owner: TBD

Target: v1.0 June 30, 2026

Status: wired into deployment; periodic run is disabled for RF MVP throttling.

## Purpose

Mission-facing thermal health, temperature, and control-mode service.

## Commands

- `REQUEST_THERMAL_STATUS`
- `SET_THERMAL_MODE`

## Abstraction Rule

- `ThermalService` owns broad thermal state, control-mode request telemetry, and mission-facing thermal commands.
- `ThermalAdapter_Artemis` owns Artemis thermal sensor/heater behavior and any hardware-specific translation.
- Do not put board protocol bytes or Teensy/PDU bus details in `ThermalService`.

## Next Work

- Replace the deterministic adapter model with real sensor/heater status once the thermal hardware path is stable.
- Keep thermal SOH small while using the RFM23BP path.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
