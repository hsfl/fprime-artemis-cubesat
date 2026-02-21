# Local Closed-Loop Emulation (MacBook)

This guide runs the full F' command/event/telemetry loop on one machine by emulating:

- RPi flight app UART endpoint
- Satellite Teensy UART wrapper
- RF segmentation/reassembly link
- Ground Teensy raw USB burst behavior
- Laptop `fprime-gds` UART endpoint

All traffic stays local via pseudo-terminals (`pty`).

## What this validates

- End-to-end F' data flow between flight app and `fprime-gds`
- Uplink/downlink through the same wrapper + segmentation contracts used in firmware
- Command/event/telemetry behavior in a closed software loop

## What this does not validate

- Physical UART electrical behavior, wiring, or power sequencing
- RF hardware behavior (interference, RSSI, packet loss on real radios)
- Teensy bootloader/runtime quirks

## Prerequisites

1. Build the F' deployment once:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

2. Ensure `fprime-gds` is available in the same venv.

## Quick start (launch everything)

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_local_emulation.sh
```

The launcher will:

- auto-detect `ArtemisRpiTeensyDeployment` binary
- auto-detect `ArtemisRpiTeensyDeploymentTopologyDictionary.json`
- create two local UART devices
- launch the flight app and `fprime-gds`
- bridge both directions through emulated wrapper + segmentation logic

Open GDS at:

- `http://127.0.0.1:5050`

Stop with `Ctrl-C`.

## Typical options

Use a different GDS port:

```bash
./tools/run_local_emulation.sh --gui-port 5060
```

Do not auto-launch app (manual app launch):

```bash
./tools/run_local_emulation.sh --no-app
```

Do not auto-launch GDS (manual GDS launch):

```bash
./tools/run_local_emulation.sh --no-gds
```

Change uplink burst flush timeout (ms):

```bash
./tools/run_local_emulation.sh --uplink-flush-ms 12
```

## Manual launch mode

When using `--no-app` and/or `--no-gds`, the emulator prints the generated UART device paths:

- `app UART device: /dev/ttys...`
- `gds UART device: /dev/ttys...`

Use those paths directly:

```bash
./build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment -d <app_uart_device>
```

```bash
fprime-gds -n \
  --dictionary build-artifacts/Darwin/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json \
  --communication-selection uart \
  --uart-device <gds_uart_device> \
  --uart-baud 115200 \
  --uart-skip-port-check \
  --framing-selection fprime \
  --gui-port 5050
```

## Suggested smoke checks in GDS

1. Send `TeensyLink.LINK_STATUS`
2. Send `TeensyLink.RESET_COUNTERS`
3. Verify `TeensyLink.LinkHeartbeat` telemetry increments over time
4. Verify command responses/events appear in the event stream

## Files added for emulation

- `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/tools/run_local_emulation.sh`
- `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/tools/local_emulation_loop.py`
