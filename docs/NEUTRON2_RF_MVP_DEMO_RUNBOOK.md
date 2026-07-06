# RF MVP Demo Runbook

BLUF: use this to manually demonstrate the Neutron 2 RF demo story with the
real Raspberry Pi, satellite Teensy, ground Teensy, and RFM23BP link.

This runbook shows:

- `fprime-gds` commanding over the ground Teensy channel 0 data port
- RF uplink from laptop to Raspberry Pi flight software
- live F Prime event and telemetry downlink over RF
- optional RFM23BP RSSI status through F Prime
- operator-scheduled 30 second payload collection
- channel 1 payload reconstruction on the ground laptop
- payload viewer review of the reconstructed `.bin` file
- local payload hash matching the Pi latest payload file

## What This Does And Does Not Prove

Validated:

- laptop GDS to ground Teensy channel 0 path
- RFM23BP command uplink and telemetry/event downlink
- Raspberry Pi runtime on `/dev/serial0`
- Base Mode command path
- SOH snapshot command path
- 30 second scheduled collection path
- simulated Neutron 2 payload capture on the Pi
- storage/downlink handoff events
- channel 1 payload transfer through the RF bridge
- ground-side payload reconstruction with CRC
- payload viewer parsing CSV bytes from a `.bin` product

Not validated:

- stock F Prime file downlink
- high-rate telemetry
- real Neutron payload board data source
- production SatNOGS radio behavior
- full mission timing

## Output Files

Keep these paths straight during the demo:

- Pi latest payload:
  `/tmp/neutron_payload_captures/latest_payload.bin`
- Pi latest payload target:
  `ssh artemis-pi 'readlink -f /tmp/neutron_payload_captures/latest_payload.bin'`
- ground reconstructed payload:
  `/tmp/neutron_hil/<run-name>/payload_30s.bin`
- payload receiver proof:
  the receiver prints `complete: ... output=<path>`
- viewer JSON summary:
  `python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --summary <payload.bin>`

The downlinked file can be named `.bin`; the bytes inside are Neutron 2 CSV
content. The viewer sniffs the CSV fields inside `.bin` files.

Viewer file-selection note:

- the browser UI has a dropdown for files in the watched capture directory
- the browser UI does not have an arbitrary file picker
- to open one exact file, start the viewer with `--file <path>`
- to watch a run folder, start the viewer with `--capture-dir <folder>`

## Hardware Map

Recheck ports every session. Do not assume another laptop's USB names.

macOS examples:

```bash
ls -l /dev/cu.usbmodem* /dev/tty.usbmodem* 2>/dev/null
python3 -m serial.tools.list_ports -v
```

Current common triple-serial mapping:

| Port | Purpose |
| --- | --- |
| first ground Teensy triple-serial port | GDS channel 0 data |
| second ground Teensy triple-serial port | ground Teensy debug counters |
| third ground Teensy triple-serial port | payload channel 1 receiver |
| satellite Teensy single serial port | satellite debug counters |

Example values from one HIL bench run:

```bash
GDS_DATA_PORT=/dev/cu.usbmodem115551201
GDS_DEBUG_PORT=/dev/cu.usbmodem115551203
GDS_PAYLOAD_PORT=/dev/cu.usbmodem115551205
SAT_DEBUG_PORT=/dev/cu.usbmodem115502201
```

Windows laptop:

- use WSL2 for GDS, payload receiver, and USB serial workflows
- use native Windows browser to open `http://127.0.0.1:5050` and
  `http://127.0.0.1:8062`
- attach the ground Teensy USB device to WSL with `usbipd-win`
- inside WSL, use `/dev/ttyACM*` or `/dev/ttyUSB*`, not `COM3`
- WSL2 users should run commands that open USB serial devices with `sudo`, such
  as `run_gds_uart.sh` and `tools/payload_receiver.py`, unless the WSL user is
  already configured for serial-device access
- for the full `usbipd-win` bind/attach/detach procedure, see
  `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md`

```bash
lsusb
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
dmesg | grep tty
```

## Preflight

### macOS

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate

GDS_DATA_PORT=/dev/cu.usbmodem115551201
GDS_DEBUG_PORT=/dev/cu.usbmodem115551203
GDS_PAYLOAD_PORT=/dev/cu.usbmodem115551205
SAT_DEBUG_PORT=/dev/cu.usbmodem115502201

lsof "$GDS_DATA_PORT" "$GDS_DEBUG_PORT" "$GDS_PAYLOAD_PORT" "$SAT_DEBUG_PORT" 2>/dev/null || true
ssh artemis-pi 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
```

### Windows Laptop (WSL2)

```bash
cd ~/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate

ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
GDS_DATA_PORT=/dev/ttyACM0
GDS_PAYLOAD_PORT=/dev/ttyACM2

lsof "$GDS_DATA_PORT" "$GDS_PAYLOAD_PORT" 2>/dev/null || true
ssh artemis-pi 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
```

Expected result:

- Pi service is `active`
- one Pi deployment process is running with `-d /dev/serial0`
- no stale local process owns the GDS data port or payload port
- dictionary exists at:
  `ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json`

## Start GDS

Terminal 1: start `fprime-gds`.

macOS:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
GDS_DATA_PORT=/dev/cu.usbmodem115551201

./tools/run_gds_uart.sh \
  --port "$GDS_DATA_PORT" \
  --baud 115200 \
  --gui-port 5050 \
  --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Windows WSL2:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
GDS_DATA_PORT=/dev/ttyACM0

./tools/run_gds_uart.sh \
  --port "$GDS_DATA_PORT" \
  --baud 115200 \
  --gui-port 5050 \
  --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Open:

```text
http://127.0.0.1:5050
```

GDS pages to keep ready:

- Commanding
- Events
- Channels or Charts

Useful channels:

- `missionManager.CurrentMode`
- `scienceManager.PendingDelaySeconds`
- `scienceManager.CollectionCount`
- `storageService.StoredProducts`
- `commsManager.PendingScienceBytes`
- `commsManager.LinkState`
- `commsManager.RssiDbm`
- `payloadDownlinkManager.ProgressPercent`

## Start Payload Receiver

Terminal 2: start this before requesting science downlink.

macOS:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
GDS_PAYLOAD_PORT=/dev/cu.usbmodem115551205
RUN_DIR=/tmp/neutron_hil/rf_demo_$(date +%Y%m%d_%H%M%S)
mkdir -p "$RUN_DIR"
echo "$RUN_DIR" | tee /tmp/neutron_hil/latest_rf_demo_dir

python -u tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output "$RUN_DIR/payload_30s.bin" \
  --timeout 240
```

Windows WSL2:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
GDS_PAYLOAD_PORT=/dev/ttyACM2
RUN_DIR=/tmp/neutron_hil/rf_demo_$(date +%Y%m%d_%H%M%S)
mkdir -p "$RUN_DIR"
echo "$RUN_DIR" | tee /tmp/neutron_hil/latest_rf_demo_dir

python -u tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output "$RUN_DIR/payload_30s.bin" \
  --timeout 240
```

Expected receiver output:

```text
header: product=<id> transfer=<id> bytes=<n> packets=<n> crc=0x....
retry: start=<n> count=<n> bitmap_bytes=<n>
progress: N/N
complete: product=<id> transfer=<id> bytes=<n> packets=<n> crc=0x.... output=/tmp/neutron_hil/.../payload_30s.bin
```

`retry:` lines are normal on the RF link. The receiver only passes when it prints
`complete:` and exits `0`.

Channel 1 now uses the same RF per-segment ACK/retry path as channel 0. If the
Pi logs `PayloadDownlinkComplete` but the receiver prints
`incomplete: received=0 total=0 missing=0`, treat it as a Teensy relay/channel-1
fault, not as successful payload proof. Check the debug counters for
`payload_uart_rx`, `payload_rf_tx_msg`, `payload_rf_rx_msg`, and
`payload_uart_tx`.

## Start Payload Viewer

Terminal 3: start the viewer. The viewer can be started before or after the
payload file exists.

Important: the viewer serves one folder for as long as that process is running.
If an old viewer is already active on port `8062`, it can keep showing an older
run directory even after a new payload receiver completes. Restart only the
viewer and point it at the run directory written by the payload receiver:

macOS:

```bash
cd ~/Developer/fprime-artemis-cubesat
RUN_DIR="$(cat /tmp/neutron_hil/latest_rf_demo_dir)"
echo "viewer run dir: $RUN_DIR"
ls -l "$RUN_DIR"

pkill -f 'ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py' 2>/dev/null || true
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --capture-dir "$RUN_DIR" \
  --port 8062
```

Windows WSL2:

```bash
cd ~/fprime-artemis-cubesat
RUN_DIR="$(cat /tmp/neutron_hil/latest_rf_demo_dir)"
echo "viewer run dir: $RUN_DIR"
ls -l "$RUN_DIR"

pkill -f 'ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py' 2>/dev/null || true
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --capture-dir "$RUN_DIR" \
  --port 8062
```

To open one exact file after the receiver completes:

```bash
RUN_DIR="$(cat /tmp/neutron_hil/latest_rf_demo_dir)"
ls -l "$RUN_DIR/payload_30s.bin"
pkill -f 'ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py' 2>/dev/null || true
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --capture-dir "$RUN_DIR" \
  --file "$RUN_DIR/payload_30s.bin" \
  --port 8062
```

Open:

```text
http://127.0.0.1:8062
```

Expected viewer behavior:

- newest supported `.csv` or `.bin` file is selected automatically
- dropdown lists files from `--capture-dir` plus the optional `--file`
- status text shows the selected source path
- metrics show nonzero rows
- chart shows counts over `t_s`

## Manual Demo Script

Terminal 4: send commands through the already-running GDS session.

macOS:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Windows WSL2:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

### 1. Base Mode

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.missionManager.ENTER_BASE_MODE \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected event:

```text
MissionManager.ModeChanged mode=BASE
```

### 2. SOH Snapshot

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.sohManager.EMIT_SOH_SNAPSHOT \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected behavior:

- SOH events/channels appear in GDS
- telemetry continues moving

### 3. Optional RSSI Check

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.commsManager.PING_LINK_RSSI \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected Pi journal event:

```text
CommsManager.LinkRssiPing state=<state> rssi=<value>dBm
```

Example from the bench:

```text
Comms link RSSI ping state=2 rssi=-5dBm
```

### 4. Configure 30 Second Capture

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.scienceManager.CONFIGURE_CAPTURE_DURATION \
  --arguments 30 \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected event:

```text
ScienceManager.CaptureDurationConfigured durationSeconds=30
```

### 5. Schedule Collection

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.missionManager.SCHEDULE_COLLECTION \
  --arguments 4 \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected events:

```text
MissionManager.CollectionScheduled delaySeconds=4
ScienceManager.CollectionTriggered delaySeconds=4
```

Wait about 40 seconds for the 4 second delay and 30 second capture window.

Expected events:

```text
ScienceManager.ScienceProductReady productSize=<n>
StorageService.ScienceStored productCount=<n> size=<n>
```

For a 30 second simulated capture, expect about 30 CSV rows and a few hundred
bytes.

### 6. Report Latest Dataset

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.storageService.REPORT_LATEST_DATASET \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected event:

```text
StorageService.LatestDataset productCount=<n> size=<n>
```

### 7. Request Science Downlink

Make sure the payload receiver is still running, then send:

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.commsManager.REQUEST_SCIENCE_DOWNLINK \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected events:

```text
CommsManager.DownlinkRequested bytes=<n>
StorageService.DownlinkPrepared downlinkBytes=<n>
PayloadDownlinkManager.PayloadDownlinkStarted product=<n> bytes=<n> packets=<n>
PayloadDownlinkManager.PayloadDownlinkComplete transfer=<n> packetsSent=<n>
CommsManager.DownlinkFinished bytes=<n>
```

Expected receiver completion:

```text
complete: product=<id> transfer=<id> bytes=<n> packets=<n> crc=0x.... output=$RUN_DIR/payload_30s.bin
```

## Verify Payload File

Run this from the repo root after receiver completion.

macOS:

```bash
cd ~/Developer/fprime-artemis-cubesat
RUN_DIR="$(cat /tmp/neutron_hil/latest_rf_demo_dir)"
LOCAL="$RUN_DIR/payload_30s.bin"

printf 'local  '; shasum -a 256 "$LOCAL"
printf 'remote '; ssh artemis-pi 'sha256sum /tmp/neutron_payload_captures/latest_payload.bin'
ssh artemis-pi 'readlink -f /tmp/neutron_payload_captures/latest_payload.bin; wc -c /tmp/neutron_payload_captures/latest_payload.bin'
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --summary "$LOCAL"
```

Windows WSL2:

```bash
cd ~/fprime-artemis-cubesat
RUN_DIR="$(cat /tmp/neutron_hil/latest_rf_demo_dir)"
LOCAL="$RUN_DIR/payload_30s.bin"

printf 'local  '; sha256sum "$LOCAL"
printf 'remote '; ssh artemis-pi 'sha256sum /tmp/neutron_payload_captures/latest_payload.bin'
ssh artemis-pi 'readlink -f /tmp/neutron_payload_captures/latest_payload.bin; wc -c /tmp/neutron_payload_captures/latest_payload.bin'
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --summary "$LOCAL"
```

Pass criteria:

- receiver exits `0`
- local and remote hashes match
- byte count is nonzero
- viewer summary prints valid JSON
- `rows` is nonzero
- for the 30 second demo, `rows` is normally `30`

Example passing summary:

```json
{
  "rows": 30,
  "total_counts": 140,
  "start_t_s": 666,
  "end_t_s": 695
}
```

## Verify Pi Journal

```bash
ssh artemis-pi '
  journalctl -u artemis-fprime.service --since "5 minutes ago" --no-pager |
    egrep "CaptureDurationConfigured|CollectionScheduled|ScienceProductReady|ScienceStored|LatestDataset|DownlinkRequested|PayloadDownlinkStarted|PayloadDownlinkComplete|DownlinkFinished|LinkRssiPing|UnexpectedSequenceCount" |
    tail -120
'
```

Expected proof:

- `CaptureDurationConfigured ... 30s`
- `CollectionScheduled ... 4s`
- `ScienceProductReady`
- `ScienceStored`
- `LatestDataset`
- `DownlinkRequested`
- `PayloadDownlinkStarted`
- `PayloadDownlinkComplete`
- `DownlinkFinished`

Warnings like this can happen during RF testing:

```text
Unexpected sequence count received. Packets may have been dropped.
```

That means the RF/GDS stream is lossy, not necessarily dead. Confirm the command
or event reached the Pi before retrying.

## Fast Troubleshooting

For a broader layer map across GDS, F Prime services, UART mux, RF, payload
receiver, viewer, and EPS/PDU channel 2, see
`docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`.

If GDS opens but commands do not reach the Pi:

- check Pi journal for `OpCodeDispatched`
- if GDS command log shows the command but the Pi does not, the RF uplink likely dropped it
- retry with spaced attempts, not a fast spam loop
- keep watching `UnexpectedSequenceCount` warnings

If telemetry is blank:

- confirm GDS uses `space-packet-space-data-link`
- confirm the ground data port is the first triple-serial port
- stop stale `fprime-gds` processes using the same serial device

If payload receiver prints `incomplete`:

- rerun the receiver before sending downlink again
- use a longer timeout such as `--timeout 240`
- `retry:` lines are normal; failure is only when the receiver exits before `complete:`
- for a direct retry of the same Pi latest payload, use:

```bash
BYTES="$(ssh artemis-pi 'wc -c < /tmp/neutron_payload_captures/latest_payload.bin')"
fprime-cli command-send ArtemisRpiTeensyDeployment.payloadDownlinkManager.START_PAYLOAD_DOWNLINK \
  --arguments 99 "$BYTES" \
  --dictionary "$DICT" --log-level-gds ERROR
```

That uses the current byte count from:

```bash
ssh artemis-pi 'wc -c /tmp/neutron_payload_captures/latest_payload.bin'
```

If the viewer shows the wrong file:

- check the status path shown in the viewer
- stop the old viewer process on `8062`
- reload `RUN_DIR` from `/tmp/neutron_hil/latest_rf_demo_dir`
- restart with `--capture-dir "$RUN_DIR" --file "$RUN_DIR/payload_30s.bin"`

If the payload file exists but viewer says it is invalid:

- confirm the file contains CSV bytes:

```bash
head "$RUN_DIR/payload_30s.bin"
```

Expected first line:

```text
t_s,counts,flag
```

## Cleanup

Stop local tools with `Ctrl-C`.

GDS and receiver logs/files are run artifacts. Keep useful payloads, then remove
old files when done:

```bash
rm -rf /tmp/neutron_hil/rf_demo_*
```

On the Pi, old simulator captures can be removed from GDS:

```text
ArtemisRpiTeensyDeployment.storageService.REMOVE_OLD_DATASETS
confirm = 1
```
