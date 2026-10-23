# Mission Starter

Owner: Aris, Kenoi

Target: v1.0 June 30, 2026

Status: wired into deployment.

## Purpose

CONOPs coordinator. It decides when managers act; it does not implement hardware behavior.

Base case: coordinate RFM23BP downlink and RPi-emulated payload collection.

## Commands

- `ENTER_BASE_MODE`
- `PING(token)`
- `SCHEDULE_COLLECTION(delaySeconds)`
- `CANCEL_COLLECTION`
- `REPORT_MODE` (read-only: emits one `ModeReport` event with the current mode)

## Abstraction Rule

- `MissionApp` owns modes, scheduling, and high-level flow.
- Payload, COMMS, PDU, and ADCS managers own their own subsystem behavior.
- Application should call manager-level ports/commands only.

## Next Work

- Encode demo flow: base mode -> scheduled collect -> science product -> downlink.
- Keep base-case science products small enough for RFM23BP.
- Add explicit mission states only when the CONOPs needs them.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
