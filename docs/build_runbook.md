# Artemis Build + Bring-up Runbook (MVP)

## macOS Laptop

### Build Teensy Baremetal Relay

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Upload:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### Build Ground Teensy Bridge

```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Upload:
```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### Build F' RPi Project

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

### Run `fprime-gds` Over UART

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/run_gds_uart.sh --port "$PORT"
```

## Windows Laptop (WSL2)

Attach the USB device to WSL before upload or UART commands.

### Build Teensy Baremetal Relay

```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Upload:
```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### Build Ground Teensy Bridge

```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Upload:
```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### Build F' RPi Project

```bash
cd ~/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

### Run `fprime-gds` Over UART

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/run_gds_uart.sh --port "$PORT"
```

## Raspberry Pi Target

Use `rpi_build.instructions` for the full native setup. This path builds directly on the target Pi and avoids architecture mismatch issues on Pi Zero W.

Run deployment:
```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

Nominal MVP/HIL framing:
- `fprime-gds` uses `--framing-selection space-packet-space-data-link`.
- The F' deployment and GDS are the `ComCcsds` endpoints.
- The Pi <-> satellite Teensy UART is channelized below the F Prime/GDS endpoint layer.
- Channel 0 carries CCSDS/GDS over RF, channel 1 carries payload packets over RF, and channel 2 is satellite-local PDU/EPS RPC.
- The ground Teensy USB data port remains byte-pure CCSDS for `fprime-gds`.

Open dashboard:
- `http://127.0.0.1:5050`

Local laptop closed-loop emulation (no hardware) is documented in:
- `EMULATION.md`

## MVP Bring-up Checks
1. Verify process starts without initialization assertion failures.
2. Verify Teensy serial log prints relay-ready line.
3. In GDS, issue `teensyTransportService.LINK_STATUS` and verify event/telemetry updates.
4. In GDS, issue `missionManager.PING` and verify the pong event/telemetry path.

## Fault Handling Checks
1. Disconnect UART cable while app is running and verify app process remains alive.
2. Reconnect UART and verify `TeensyTransportService` telemetry continues updating.
3. If explicitly testing UART mux fault handling, send malformed wrapper bytes to Teensy UART and verify framing/CRC counters increase.

## Raspberry Pi Native Build + Run
For student-friendly setup over local Wi-Fi (find Pi IP + SSH + native build + run), use:

- `rpi_build.instructions`
