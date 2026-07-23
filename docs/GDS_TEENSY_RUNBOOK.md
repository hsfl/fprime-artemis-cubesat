# GDS Teensy/RFM23BP Cold-Fallback Runbook

This is the cold fallback for the C3M student ground station, and remains the
ground-adapter procedure for the separately qualified Neutron-2 `D2`/Windows
workflow. The primary C3M/macOS path is the fixed HackRF launcher in
[`HACKRF_GROUND_STATION_RUNBOOK.md`](HACKRF_GROUND_STATION_RUNBOOK.md):
`433 MHz`, TX gain `16`, RX LNA/VGA `8/8`, ACK mode, RF amplifier off, antenna
bias off, and no AGC or student tuning.

Use this runbook only after stopping the HackRF launcher and intentionally
connecting the known ground Teensy/RFM23BP node. Do not run both ground
adapters at once.

## Scope

- Recover, build, or upload `GDS_Teensy` firmware when the fallback needs it.
- Verify USB serial connectivity.
- Run `fprime-gds` over UART using the RPi-side launcher script.
- Troubleshoot common failures.

Normal fallback startup does not require a rebuild or reflash. Preserve the
known-good firmware unless recovery evidence points to a stale or wrong upload.

## Paths

- Ground Teensy workspace: `GDS_Teensy`
- Ground sketch: `GDS_Teensy/firmware/gds_teensy/gds_teensy.ino`
- Arduino config: `GDS_Teensy/tools/arduino-cli/arduino-cli.yaml`
- GDS launcher: `ArtemisRpiTeensy_N2/tools/run_gds_uart.sh`

## 1) Build Ground Teensy Firmware When Required

### macOS

```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Manual equivalent:
```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile --fqbn teensy:avr:teensy41 --build-path "$PWD/build/arduino-cli" "$PWD/firmware/gds_teensy"
```

### Windows Laptop (WSL2)

```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Manual equivalent:
```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile --fqbn teensy:avr:teensy41 --build-path "$PWD/build/arduino-cli" "$PWD/firmware/gds_teensy"
```

## 2) Upload Ground Teensy Firmware When Required

### macOS

Find the physical upload ID:
```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli board list
```

The current bench mapping is `usb:100000`, but confirm it before every upload.
Do not upload through `/dev/cu.usbmodem*` when more than one Teensy is attached.

Upload by confirmed physical ID:
```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/upload.sh usb:100000
```

### Windows Laptop (WSL2)

Attach the Teensy USB device to WSL first, then confirm the physical upload ID.
This documents the fallback workflow; it is not a claim that the current C3M
HackRF baseline is Windows-qualified.

```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli board list
./tools/arduino-cli/upload.sh usb:100000
```

Stop if `usb:100000` is absent or identifies the wrong board. Never guess
another ID.

## 3) Optional Serial Monitor Check

### macOS

```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
python3 -m serial.tools.list_ports -v
DEBUG_PORT=/dev/cu.usbmodem...
arduino-cli monitor -p "$DEBUG_PORT" -c baudrate=115200
```

Select the second port in the ground board's three-port group. The first is
binary GDS data; the third is payload data.

### Windows Laptop (WSL2)

```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
python3 -m serial.tools.list_ports -v
DEBUG_PORT=/dev/ttyACM...
arduino-cli monitor -p "$DEBUG_PORT" -c baudrate=115200
```

## 4) Run `fprime-gds` over UART

### macOS

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
python3 -m serial.tools.list_ports -v
GDS_DATA_PORT=/dev/cu.usbmodem...
./tools/run_gds_uart.sh --port "$GDS_DATA_PORT"
```

Select the first port in the confirmed ground triple-serial group. Do not use a
separately connected satellite Teensy port.

### Windows Laptop (WSL2)

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
python3 -m serial.tools.list_ports -v
GDS_DATA_PORT=/dev/ttyACM...
./tools/run_gds_uart.sh --port "$GDS_DATA_PORT"
```

Open UI:
- `http://127.0.0.1:5050`

## Troubleshooting

1. Compile link errors with many `usb_serial_*` / `yield` undefined references
   - Cause: stale build cache.
   - Fix:
   macOS:
   ```bash
   cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
   rm -rf build/arduino-cli
   ./tools/arduino-cli/build.sh
   ```
   Windows WSL2:
   ```bash
   cd ~/fprime-artemis-cubesat/GDS_Teensy
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
