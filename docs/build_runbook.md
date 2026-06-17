# Artemis Build + Bring-up Runbook (MVP)

## 1) Build Teensy Baremetal Relay
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Upload (example port):
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## 2) Build Ground Teensy Bridge
```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh
```

Upload (example port):
```bash
cd GDS_Teensy
./tools/arduino-cli/upload.sh /dev/cu.usbmodemXXXX
```

## 3) Build F' RPi Project (in-place promoted sample)
```bash
cd <repo-root>
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

### 3b) Native Build on Raspberry Pi (preferred)
Use:

- `rpi_build.instructions`

This path builds directly on the target Pi and avoids architecture mismatch issues on Pi Zero W.

## 4) Run Deployment
```bash
cd ArtemisRpiTeensy_N2
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

## 5) Run `fprime-gds` over UART
```bash
cd ArtemisRpiTeensy_N2
./tools/run_gds_uart.sh
```

Nominal MVP/HIL framing:
- `fprime-gds` uses `--framing-selection space-packet-space-data-link`.
- The F' deployment and GDS are the `ComCcsds` endpoints.
- Both Teensy bridges run transparent raw-byte tunnel mode and only segment/reassemble bytes for the RF hop.
- The custom UART wrapper (`0xD4 0xC3 + len + crc16`) is legacy/fallback only.

Open dashboard:
- `http://127.0.0.1:5050`

Local Mac-only closed-loop emulation (no hardware) is documented in:
- `EMULATION.md`

## 6) MVP Bring-up Checks
1. Verify process starts without initialization assertion failures.
2. Verify Teensy serial log prints relay-ready line.
3. In GDS, issue `teensyTransportService.LINK_STATUS` and verify event/telemetry updates.
4. In GDS, issue `missionManager.PING` and verify the pong event/telemetry path.

## 7) Fault Handling Checks
1. Disconnect UART cable while app is running and verify app process remains alive.
2. Reconnect UART and verify `TeensyTransportService` telemetry continues updating.
3. If explicitly testing legacy wrapper mode, send malformed wrapper bytes to Teensy UART and verify framing/CRC counters increase.

## 8) Raspberry Pi Native Build + Run (Manual)
For student-friendly setup over local Wi-Fi (find Pi IP + SSH + native build + run), use:

- `rpi_build.instructions`
