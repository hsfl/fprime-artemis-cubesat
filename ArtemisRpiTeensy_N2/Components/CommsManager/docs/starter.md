# COMMS Starter

Owner: Dennis, Joe, Kenoi

Target: v1.0 June 30, 2026

Status: wired into deployment.

## Purpose

Mission-facing communications manager. It should survive swapping RFM23BP for SatNOGS.

Base case: SatNOGS is not available yet. Default to RFM23BP and keep data volume low.

Power note: SatNOGS rail control belongs in PDU, not COMMS. Expect 3.3 V, 5 V, and VBatt rail requirements.

## Commands

- `REQUEST_LINK_STATUS`
- `SELECT_RADIO_BACKEND(backend)`
- `REQUEST_SCIENCE_DOWNLINK`

Backend values:
- `0`: RFM23BP
- `1`: SatNOGS

## Abstraction Rule

- `CommsManager` owns link state, backend selection, and downlink requests.
- `CommsAdapter_TeensyRfm23` owns current RFM23BP behavior.
- Future SatNOGS protocol goes in a SatNOGS adapter, not this manager.

## Next Work

- Keep manager API radio-agnostic.
- Treat RFM23BP as the default backend until SatNOGS hardware is validated.
- Keep telemetry/science payloads slim on RFM23BP.
- Add SatNOGS adapter after the dev board is available.
- Increase byte budget only after SatNOGS is working.
- Avoid leaking RF MTU, UART/SPI, or packet details into mission logic.

## Build Check

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```
