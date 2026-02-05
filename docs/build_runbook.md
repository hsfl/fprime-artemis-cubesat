# Artemis Build + Bring-up Runbook (MVP)

## 1) Build Teensy Baremetal Relay
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Upload (example port):
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## 2) Build F' RPi Project (in-place promoted sample)
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

## 3) Run Deployment
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

## 4) MVP Bring-up Checks
1. Verify process starts without initialization assertion failures.
2. Verify Teensy serial log prints relay-ready line.
3. In GDS, issue `TeensyLink.LINK_STATUS` and verify event/telemetry updates.
4. In GDS, issue health ping checks and verify `PingResponder` and `TeensyLink` participate.

## 5) Fault Handling Checks
1. Disconnect UART cable while app is running and verify app process remains alive.
2. Reconnect UART and verify `TeensyLink` telemetry continues updating.
3. Send malformed frame bytes to Teensy UART and verify framing/CRC counters increase.
