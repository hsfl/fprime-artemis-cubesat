# GDS USB Raw Dump Runbook

## Purpose
Use a Teensy debug sketch to view raw UART/USB traffic while `fprime-gds` is connected.

Sketch:
- `firmware/gds_usb_raw_dump/gds_usb_raw_dump.ino`

## What You See
- `U0[...]` = bytes from `fprime-gds -> Teensy` (uplink into Teensy USB `Serial`)
- `D1[...]` = bytes from `Serial1 -> fprime-gds` (downlink back to GDS)
- `RS[...]` = replayed sample bytes injected by Teensy back to GDS (debug replay mode)

## One-Time Build + Upload (Arduino CLI)
From `GDS_Teensy`:

```bash
cd GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"

arduino-cli compile \
  --fqbn teensy:avr:teensy41:usb=serial2 \
  --build-path build/arduino-cli-gds-usb-raw-dump \
  firmware/gds_usb_raw_dump

arduino-cli upload \
  --fqbn teensy:avr:teensy41:usb=serial2 \
  -p /dev/cu.usbmodemXXXX \
  --input-dir build/arduino-cli-gds-usb-raw-dump \
  firmware/gds_usb_raw_dump
```

Replace `/dev/cu.usbmodemXXXX` with your Teensy upload port.

## IDE/Host Setup
1. Build USB type as `Dual Serial` (`usb=serial2`).
2. Connect `fprime-gds` to Teensy USB serial interface #0 (`Serial`).
3. Open Arduino Serial Monitor on USB serial interface #1 (`SerialUSB1`) at `115200`.

Expected startup text:
- `[gds-usb-raw-dump] ready`
- `[gds-usb-raw-dump] U0=GDS->Teensy  D1=Serial1->GDS`

## Quick Test Modes

### A) Real bridge test
- Leave `ENABLE_LOCAL_ECHO_TO_GDS = false`.
- Send a GDS command.
- You should see `U0[...]` immediately.
- You will see `D1[...]` only if something is actually sending bytes into Teensy `Serial1`.

### B) Local echo sanity check
- Set `ENABLE_LOCAL_ECHO_TO_GDS = true` in the sketch.
- Rebuild + re-upload.
- Sent GDS bytes are echoed back to GDS.
- This confirms transport path only; echoed bytes are usually not valid CCSDS telemetry.

### C) CCSDS replay response test (simple)
- `ENABLE_REPLAY_SAMPLE_TO_GDS` is enabled in the sketch by default.
- On uplink activity, Teensy injects a captured sample downlink byte sequence to GDS.
- This sample is stored in:
  - `firmware/gds_usb_raw_dump/ccsds_replay_sample.hpp`
- In the debug monitor you will see `RS[...]` lines when replay occurs.
- Guard timer (`REPLAY_GUARD_MS`) prevents flooding replay responses.
- If you do not want replay behavior, set:
  - `ENABLE_REPLAY_SAMPLE_TO_GDS = false`
- If GDS does not decode the replay as expected on your setup, replace the sample bytes in:
  - `firmware/gds_usb_raw_dump/ccsds_replay_sample.hpp`
  with a fresh capture from your own known-good stream.

## Notes
- This is a debug sketch, not the normal RF relay firmware.
- For normal operation, reflash `firmware/gds_teensy/gds_teensy.ino`.
