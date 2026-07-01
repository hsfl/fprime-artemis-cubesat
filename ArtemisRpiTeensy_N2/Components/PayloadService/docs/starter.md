# Payload Starter

Owner: Aris, Piper, Kenoi

Target: v1.0 June 30, 2026

Status: wired into deployment.

## Purpose

Mission-facing payload control. No payload-board protocol details here.

Base case: no payload board yet. Use an emulated payload adapter running on the Raspberry Pi with F Prime.

Power note: the real payload will require 28 V. PDU owns rail control; Payload owns collection commands/state.

## Commands

- `REQUEST_PAYLOAD_STATUS`
- `CONFIGURE_PAYLOAD(sampleCount, periodMs)`
- `START_PAYLOAD_COLLECTION(collectionId)`
- `SCIENCE_CAPTURE(durationSeconds)`

## Abstraction Rule

- `PayloadService` owns commands, collection state, telemetry, and events.
- Current adapter behavior is emulated on the Raspberry Pi.
- Adapter choice is a topology/build profile decision, not a runtime command.
- Future real payload protocol goes in an adapter, not this service.

## Next Work

- Verify whether payload power is external.
- Confirm the 28 V payload power contract with PDU.
- Keep the emulated adapter usable until the board arrives.
- Keep generated science data small for the RFM23BP link.
- Replace emulated adapter behavior when the payload ICD is real.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
