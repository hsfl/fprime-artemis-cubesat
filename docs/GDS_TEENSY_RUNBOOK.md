# GDS Teensy Runbook

Ground-station Teensy workflow for RF bridge + laptop UART integration.

## Scope

- Build and upload `GDS_Teensy` firmware.
- Verify USB serial connectivity.
- Run `fprime-gds` over UART using the RPi-side launcher script.
- Troubleshoot common failures.

## Paths

- Ground Teensy workspace: `GDS_Teensy`
- Ground sketch: `GDS_Teensy/firmware/gds_teensy/gds_teensy.ino`
- Arduino config: `GDS_Teensy/tools/arduino-cli/arduino-cli.yaml`
- GDS launcher: `ArtemisRpiTeensy_N2/tools/run_gds_uart.sh`

## 1) Build Ground Teensy Firmware

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Manual equivalent:
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile --fqbn teensy:avr:teensy41 --build-path "$PWD/build/arduino-cli" "$PWD/firmware/gds_teensy"
```

## 2) Upload Ground Teensy Firmware

Find device:
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli board list
```

Upload:
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/upload.sh /dev/cu.usbmodemXXXX
```

Notes:
- On macOS, port is usually `/dev/cu.usbmodem*`.
- On Linux, port is usually `/dev/ttyACM*`.

## 3) Optional Serial Monitor Check

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli monitor -p /dev/cu.usbmodemXXXX -c baudrate=115200
```

## 4) Run `fprime-gds` over UART

Use repo launcher:
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_gds_uart.sh
```

Defaults in launcher:
- UART device: `/dev/cu.usbmodem115551201`
- UART baud: `115200`
- GUI port: `5050`

Open UI:
- `http://127.0.0.1:5050`

## Troubleshooting

1. Compile link errors with many `usb_serial_*` / `yield` undefined references
   - Cause: stale build cache.
   - Fix:
   ```bash
   cd /Users/sozodennis/Developer/fprime-artemis-cubesat/GDS_Teensy
   rm -rf build/arduino-cli
   ./tools/arduino-cli/build.sh
   ```
   - Or use `arduino-cli compile --clean ...`.

2. Upload fails with `Compiled sketch not found`
   - Cause: upload command searching default cache, not repo build dir.
   - Fix: use `./tools/arduino-cli/upload.sh` (it passes `--input-dir build/arduino-cli`).

3. Upload fails with `Unable find Teensy Loader`
   - Cause: `teensy.app` not running yet.
   - Fix: run upload again after loader opens, or open manually from Teensy tools.

4. Browser shows `403` at `127.0.0.1:5000`
   - Cause: another service bound to `5000` (commonly macOS Control Center/AirPlay Receiver).
   - Fix: use launcher default `5050` or set `--gui-port <port>`.

