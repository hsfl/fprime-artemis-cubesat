# ADCS Starter

Owner: Piper

Target: v1.0 June 30, 2026

Status: wired into deployment.

## Purpose

Mission-facing ADCS status and command skeleton.

## Commands

- `REQUEST_ADCS_STATUS`
- `SET_ADCS_MODE(mode)`
- `REQUEST_ATTITUDE_UPDATE(requestKey)`

Mode values:
- `0`: idle
- `1`: detumble
- `2`: pointing

## Abstraction Rule

- `AdcsManager` owns ADCS command/state telemetry.
- `AdcsDriver_D2S2` owns current simulator behavior.
- Future physical ADCS protocol goes in a separate driver.

## Next Work

- Keep low priority for MVP unless CONOPs needs more ADCS detail.
- Expand D2S2 or hardware driver later.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
