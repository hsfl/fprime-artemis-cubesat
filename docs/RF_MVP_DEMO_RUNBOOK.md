# RF MVP Demo Runbook

Date: 2026-04-24  
Purpose: freeze the known-good RF MVP demo path and provide a repeatable smoke test.

## BLUF

The demo path to freeze is:

```text
fprime-gds on Mac
-> ground Teensy USB data port
-> RFM23BP RF hop
-> satellite Teensy
-> Raspberry Pi /dev/serial0
-> F Prime ArtemisRpiTeensyDeployment
```

Known-good proof:

```text
missionManager.PING reaches the Pi
Pi journal prints MissionManager pong token=<token>
GDS receives valid CCSDS TM frames
```

## Known-Good Hardware Map

| Device | Port / address | Purpose |
| --- | --- | --- |
| Ground Teensy | `/dev/cu.usbmodem115551201` | byte-clean GDS data stream |
| Ground Teensy | `/dev/cu.usbmodem115551203` | debug counter stream |
| Ground Teensy | `usb:1100000` | Arduino CLI upload port |
| Satellite Teensy | `/dev/cu.usbmodem115502201` | debug counter stream |
| Satellite Teensy | `usb:100000` | Arduino CLI upload port |
| Raspberry Pi | `artemis-pi`, `192.168.0.152` | F Prime target |

Do not upload by `/dev/cu.usbmodem*` when both Teensys are connected. Use the physical Teensy upload ports.

## Known-Good Pi Runtime

Systemd service:

```sh
artemis-fprime.service
```

Expected service command:

```sh
/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0
```

Current deployment symlink target:

```text
/home/pi/artemis/current/ArtemisRpiTeensyDeployment
-> /home/pi/artemis/cross/ArtemisRpiTeensyDeployment
```

Check it:

```sh
ssh artemis-pi 'systemctl is-active artemis-fprime.service'
ssh artemis-pi 'readlink -f /home/pi/artemis/current/ArtemisRpiTeensyDeployment'
```

Expected:

```text
active
/home/pi/artemis/cross/ArtemisRpiTeensyDeployment
```

## Known-Good Local Dictionary

Use the Pi Zero W cross-build dictionary:

```text
ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Important current RF MVP config:

```text
ComCfg.TmFrameFixedSize = 128
FW_COM_BUFFER_MAX_SIZE = 96
FW_LOG_STRING_MAX_SIZE = 80
```

These are intentionally small for the RF demo. They are project-owned overrides in:

```text
ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/
```

## Why 128-Byte TM Frames

The 128-byte CCSDS TM frame size is a demo transport decision, not a spacecraft-standard requirement.

Current RF packet math:

```text
RF packet max length = 49 bytes
RF segment header = 5 bytes
Useful RF payload per packet = 44 bytes
128-byte TM frame = 3 RF packets: 44 + 44 + 40
1024-byte TM frame = about 24 RF packets
```

GDS validates a complete CCSDS TM frame with CRC. If one RF segment is lost inside that TM frame, the whole TM frame fails. A 1024-byte frame therefore made each GDS frame depend on about 24 RF packets arriving cleanly in order. A 128-byte frame lowers that reliability burden to 3 RF packets per GDS frame, which is why telemetry became decodable after the ACK/retry fixes.

The companion `FW_COM_BUFFER_MAX_SIZE = 96` keeps F Prime command/event/telemetry buffers small enough to fit comfortably inside a 128-byte TM transfer frame after CCSDS headers, idle fill, and trailer overhead. With the current F Prime CCSDS path:

```text
AggregationSize = TmFrameFixedSize - 6 - 6 - 1 - 2
AggregationSize = 128 - 15 = 113 bytes
```

So a 96-byte F Prime comm buffer leaves headroom inside the 113-byte aggregation payload. Going lower than 128 may work for tiny heartbeat-only telemetry, but it reduces available payload headroom fast and risks truncating normal command, event, file, or telemetry packets. Keep 128 for the RF MVP unless the dictionary, deployed binary, and Teensy fixed-frame chunk size are all changed together and retested.

## Start GDS

From repo root:

```sh
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
fprime-gds -n \
  --communication-selection uart \
  --uart-device /dev/cu.usbmodem115551201 \
  --uart-baud 115200 \
  --framing-selection space-packet-space-data-link \
  --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json \
  --gui-port 5051 \
  --log-to-stdout \
  --log-level-gds INFO
```

Open:

```text
http://localhost:5051
```

Notes:

- Use `5051` if `5050` or `5000` is already occupied.
- APID sequence warnings can still appear. That means GDS is decoding frames but some packets are being dropped.
- Repeated checksum spam means the byte stream is not frame-clean and the RF chain should be rechecked.

## Smoke Test Script

Script:

```text
ArtemisRpiTeensy_N2/tools/demo_rf_mvp_smoke.sh
```

### Check only

This verifies ports, dictionary, and Pi service without sending a command:

```sh
cd ArtemisRpiTeensy_N2
./tools/demo_rf_mvp_smoke.sh --no-command
```

### Start GDS and send ping

This starts GDS in the background, then sends the ping:

```sh
cd ArtemisRpiTeensy_N2
./tools/demo_rf_mvp_smoke.sh --start-gds --token 4245
```

GDS log path is printed by the script under:

```text
ArtemisRpiTeensy_N2/tools/logs/
```

### Send ping through already-running GDS

If GDS is already running:

```sh
cd ArtemisRpiTeensy_N2
./tools/demo_rf_mvp_smoke.sh --token 4245
```

Expected proof:

```text
MissionManager pong token=4245 count=1
Opcode 0x10006001 dispatched
Opcode 0x10006001 completed
[demo-smoke] PASS: command path verified with token=4245
```

## Manual Ping Command

If the script fails and you want the raw command:

```sh
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
fprime-cli command-send ArtemisRpiTeensyDeployment.missionManager.PING \
  --arguments 4245 \
  --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json \
  --log-level-gds ERROR
```

Then check Pi journal:

```sh
ssh artemis-pi 'journalctl -u artemis-fprime.service --since "30 seconds ago" --no-pager | egrep "MissionManager|PING|pong|OpCode|completed|ERROR|WARNING" | tail -100'
```

## Teensy Build/Upload Commands

Ground Teensy:

```sh
cd GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile --clean \
  --fqbn teensy:avr:teensy41:usb=serial2 \
  --libraries "$PWD/../ArtemisTeensy_N2_Baremetal/firmware/libs" \
  --build-path "$PWD/build/arduino-cli-gds-teensy-debug" \
  "$PWD/firmware/gds_teensy"
arduino-cli upload -v \
  --fqbn teensy:avr:teensy41:usb=serial2 \
  -p usb:1100000 \
  --input-dir "$PWD/build/arduino-cli-gds-teensy-debug" \
  "$PWD/firmware/gds_teensy"
```

Satellite Teensy:

```sh
cd ArtemisTeensy_N2_Baremetal
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile --clean \
  --fqbn teensy:avr:teensy41 \
  --libraries "$PWD/firmware/libs" \
  --build-path "$PWD/build/arduino-cli-satellite-debug" \
  "$PWD/firmware/satellite_teensy"
arduino-cli upload -v \
  --fqbn teensy:avr:teensy41 \
  -p usb:100000 \
  --input-dir "$PWD/build/arduino-cli-satellite-debug" \
  "$PWD/firmware/satellite_teensy"
```

Stop the Pi service before reflashing the satellite Teensy:

```sh
ssh artemis-pi 'sudo systemctl stop artemis-fprime.service'
```

Restart after upload:

```sh
ssh artemis-pi 'sudo systemctl start artemis-fprime.service'
```

## Demo Interpretation

Passing MVP means:

- GDS starts against the ground Teensy data port.
- `demo_rf_mvp_smoke.sh --token <token>` passes.
- Pi journal shows `MissionManager pong token=<token>`.
- GDS shows some telemetry/event decode.

Not required for this freeze:

- clean file downlink through stock F Prime `FileDownlink.SendFile`
- high-rate telemetry
- large payload downlink through GDS

For payload files, use the custom downlink guidance in:

```text
docs/PAYLOAD_DOWNLINK_PROTOCOL_ADVICE.md
```

## Do Not Change Before Demo Unless Needed

Avoid changing:

- RF packet size/header format
- ACK/retry timing
- `ComCfg.TmFrameFixedSize`
- F Prime topology telemetry throttling
- Teensy upload FQBNs
- GDS framing mode

If a change is required, rerun:

```sh
cd ArtemisRpiTeensy_N2
./tools/demo_rf_mvp_smoke.sh --start-gds --token 4245
```
