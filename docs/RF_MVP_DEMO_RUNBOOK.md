# RF MVP Demo Runbook

Date: 2026-06-23
Purpose: freeze the known-good RF MVP demo path and provide a repeatable HIL smoke test.

## BLUF

The command/telemetry path to freeze is:

```text
fprime-gds on Mac
-> ground Teensy USB data port / channel 0
-> RFM23BP RF hop
-> satellite Teensy
-> Raspberry Pi /dev/serial0
-> F Prime ArtemisRpiTeensyDeployment
```

The payload/science path to freeze is:

```text
F Prime latest science product
-> PayloadDownlinkManager channel 1 packets
-> RFM23BP RF hop
-> ground Teensy payload serial port
-> tools/payload_receiver.py
-> payload .bin/.csv
-> Neutron 2 payload viewer
```

Known-good proof is no longer just a ping. A valid smoke must show:

```text
GDS receives live F Prime events/telemetry
payload receiver writes the reconstructed file
local payload hash matches Pi /tmp/neutron_payload_captures/latest_payload.bin
payload viewer parses the result
Pi journal shows PayloadDownlinkProgress and DownlinkFinished
```

## Known-Good Hardware Map

Observed on 2026-06-23. Recheck with `arduino-cli board list` and
`ls /dev/cu.usbmodem*` each session.

| Device | Port / address | Purpose |
| --- | --- | --- |
| Ground Teensy | `/dev/cu.usbmodem115553301` | byte-clean GDS channel 0 data stream |
| Ground Teensy | `/dev/cu.usbmodem115553303` | debug counter stream |
| Ground Teensy | `/dev/cu.usbmodem115553305` | payload channel 1 stream |
| Ground Teensy | `usb:100000` | Arduino CLI upload port |
| Satellite Teensy | `/dev/cu.usbmodem115502201` | debug counter stream |
| Satellite Teensy | `usb:2100000` | Arduino CLI upload port |
| Raspberry Pi | `artemis-pi`, `192.168.0.152` | F Prime target |

Do not upload by `/dev/cu.usbmodem*` when both Teensys are connected. Use the
physical Teensy upload ports from `arduino-cli board list`.

## Current HIL Proof

Latest progress-log redeploy smoke on 2026-06-23:

```text
Pi binary hash: fd8e260f042407545620936405e3b35f7026b404d1d8c26cd103afd7d478d670
Pi dictionary hash: 9a744f4343623d136236d9e10427c7c5fedd2457c773fd951c21bab131215f92
local payload: /tmp/neutron_hil/progress_smoke/payload.bin
payload bytes: 72
payload hash: 094338d54bf52f0defee9dfa101d03bba7712ad20a877d51e2ff3202f468114f
viewer rows: 6
```

Pi journal showed:

```text
PayloadDownlinkStarted
PayloadDownlinkProgress percent=10
PayloadDownlinkProgress percent=20
...
PayloadDownlinkProgress percent=90
PayloadDownlinkComplete
DownlinkFinished
```

Caveat: GDS still showed APID sequence-count warnings during the smoke. That
means the RF/GDS stream is lossy, not dead. The payload receiver retry/CRC path
recovered the tested science product.

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

## Start GDS

From repo root:

```sh
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
fprime-gds -n \
  --communication-selection uart \
  --uart-device /dev/cu.usbmodem115553301 \
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

## Full Payload Smoke Test

Use this when validating the current demo story, not just the command link.

### 1. Preflight

```sh
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
ps -axo pid,command | rg 'fprime-gds|fprime-server|payload_receiver|teensy-monitor|Arduino IDE' || true
lsof /dev/cu.usbmodem115553301 /dev/cu.usbmodem115553303 /dev/cu.usbmodem115553305 /dev/cu.usbmodem115502201 2>/dev/null || true
ssh artemis-pi 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
```

Expected:

- no stale local GDS, receiver, monitor, or Arduino IDE process owns the ports
- Pi service is `active`
- one Pi deployment process is running with `-d /dev/serial0`

### 2. Start payload receiver

Start this before requesting the downlink:

```sh
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
mkdir -p /tmp/neutron_hil/progress_smoke
python -u tools/payload_receiver.py \
  --port /dev/cu.usbmodem115553305 \
  --baud 115200 \
  --output /tmp/neutron_hil/progress_smoke/payload.bin \
  --timeout 180
```

Expected receiver output includes:

```text
header: product=<id> transfer=<id> bytes=<n> packets=<n> crc=0x....
retry: ...
progress: N/N
complete: product=<id> transfer=<id> bytes=<n> packets=<n> crc=0x.... output=<path>
```

### 3. Send the demo command story

In another terminal:

```sh
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json

fprime-cli command-send ArtemisRpiTeensyDeployment.sohManager.EMIT_SOH_SNAPSHOT --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.missionManager.ENTER_BASE_MODE --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.scienceManager.CONFIGURE_CAPTURE_DURATION --arguments 6 --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.missionManager.SCHEDULE_COLLECTION --arguments 4 --dictionary "$DICT"
sleep 12
fprime-cli command-send ArtemisRpiTeensyDeployment.storageService.REPORT_LATEST_DATASET --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.commsManager.REQUEST_SCIENCE_DOWNLINK --dictionary "$DICT"
```

Use fully-qualified command names with this dictionary.

### 4. Verify file and viewer

```sh
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
printf 'local  '; shasum -a 256 /tmp/neutron_hil/progress_smoke/payload.bin
printf 'remote '; ssh artemis-pi 'sha256sum /tmp/neutron_payload_captures/latest_payload.bin'
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --summary /tmp/neutron_hil/progress_smoke/payload.bin
```

Pass criteria:

- receiver exits `0`
- local and remote hashes match
- viewer emits a valid JSON summary
- rows are nonzero

### 5. Verify progress logs

```sh
ssh artemis-pi '
  journalctl -u artemis-fprime.service --since "5 minutes ago" --no-pager |
    grep -E "PayloadDownlinkStarted|PayloadDownlinkProgress|PayloadDownlinkComplete|DownlinkFinished"
'
```

Expected:

- `PayloadDownlinkStarted`
- progress at nominal `10` percent increments through `90`
- `PayloadDownlinkComplete`
- `DownlinkFinished`

For small products, multiple progress percentages can share the same packet
count because one packet may cover more than ten percent of the file.

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
  --fqbn teensy:avr:teensy41:usb=serial3 \
  --libraries "$PWD/../ArtemisTeensy_N2_Baremetal/firmware/libs" \
  --build-path "$PWD/build/arduino-cli-gds-teensy-debug" \
  "$PWD/firmware/gds_teensy"
arduino-cli upload -v \
  --fqbn teensy:avr:teensy41:usb=serial3 \
  -p usb:100000 \
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
  -p usb:2100000 \
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
- The demo command story reaches the Pi.
- GDS shows live telemetry/events and downlink progress.
- The payload receiver reconstructs a file from the ground Teensy payload port.
- The local reconstructed payload hash matches the Pi latest payload hash.
- The Neutron 2 payload viewer parses the reconstructed file.

Not required for this freeze:

- clean file downlink through stock F Prime `FileDownlink.SendFile`
- high-rate telemetry
- large payload downlink through stock GDS file transfer

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
