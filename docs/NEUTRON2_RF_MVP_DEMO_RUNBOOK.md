# RF MVP Demo Runbook

BLUF: use this to manually demonstrate the Neutron 2 RF demo story with the
real Raspberry Pi, satellite Teensy, ground Teensy, and RFM23BP link.

This runbook shows:

- `fprime-gds` commanding over the ground Teensy channel 0 data port
- RF uplink from laptop to Raspberry Pi flight software
- live F Prime event and telemetry downlink over RF
- optional RFM23BP RSSI status through F Prime
- operator-scheduled 30 second payload collection
- cancel or base-mode recovery for pending collections
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
- pending-collection cancel path
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

## RF Budget Note

The HIL demo and laptop rehearsal use the same topology. The scheduled science
path, payload downlink app, transport manager, command/telemetry framework,
EPS driver ticks, and the slow `commsApp.run` radio-recovery policy are active.
The higher-volume periodic `sohApp.run`, `payloadManager.run`, and
`storageManager.run` loops remain disabled; use command-triggered SOH/storage
checks for demo visibility.

## RF Reliability Wiring Gate

The hardened firmware uses RFM23BP `SDN` as a functional hardware reset. Verify
this wiring before the demo:

| Teensy pin | Signal | Required behavior |
| --- | --- | --- |
| 37 | RFM23BP `SDN` | HIGH shuts the radio down; LOW permits initialization |
| 36 | Raspberry Pi enable | remains HIGH while the radio is reset/recovered |
| 38 / 40 | RFM23BP CS / IRQ | unchanged |
| 30 / 31 | RF front-end RX / TX control | unchanged |

On every satellite Teensy boot, including a watchdog reboot, pin 37 is asserted
HIGH before the Pi enable and UART initialization path. F Prime then queries
channel 2 and enables the radio. A repeated 500 ms TX-completion failure also
asserts SDN; F Prime retries after 30 seconds, 120 seconds, and then every 15
minutes. The ground Teensy independently performs SDN recovery with bounded
1-second, 5-second, and 30-second initialization backoff, without requiring a
GDS or USB restart.

SDN does not remove the radio board's power rail. If the failure persists, test
28 dBm versus 30 dBm, scope RFM23BP VCC during TX, inspect SPI/nIRQ/SDN with a
logic analyzer, verify common ground and backfeed paths, and swap the module or
cable before attributing the remaining fault to software.

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

- `missionApp.CurrentMode`
- `scienceApp.PendingDelaySeconds`
- `scienceApp.CollectionCount`
- `storageManager.StoredProducts`
- `commsApp.PendingScienceBytes`
- `commsApp.LinkState`
- `commsApp.RadioStatusKnown`
- `commsApp.RadioState`
- `commsApp.RadioFault`
- `commsApp.RadioRpcResult`
- `commsApp.RadioRecoveryFailures`
- `commsApp.RadioRetrySeconds`
- `commsApp.RadioInitAttempts`
- `commsApp.RssiValid`
- `commsApp.RssiDbm`
- `payloadDownlinkApp.ProgressPercent`

Expected startup sequence:

1. `RadioStatusKnown=0`, `RadioState=OFF`, compatibility `LinkState=0`.
2. The Pi receives factual OFF status and issues `SET_ENABLED(1)`.
3. `RadioStatusKnown=1`, `RadioState=READY`, `RadioFault=NONE`, and
   compatibility `LinkState=2`.

If `RadioState=OFF` with `RadioFault=INIT_FAILED` or `LOCAL_TX_FAULT`, leave the
Pi and GDS running and watch `RadioRetrySeconds`; autonomous recovery is now the
expected behavior. `RssiValid=0` means no addressed Neutron 2 RF packet has yet
been accepted, so `RssiDbm` must not be treated as a live link measurement.

Command bounds:

- `missionApp.SCHEDULE_COLLECTION delaySeconds` accepts `1..300`.
- capture duration accepts `1..120`; default is `30`.
- invalid values return `VALIDATION_ERROR` and emit a rejection warning event.
- operator command-rejection events are always visible; storm-capable
  link/downlink warnings remain throttled for RF event budget.
- `scienceApp.SCIENCE_CAPTURE(durationSeconds)` is one-shot; it does not
  change the default capture duration for later scheduled collections.

Persistent capture duration:

- Set `scienceApp.CAPTURE_DURATION_SECONDS` with `PRM_SET`, then persist it
  with `PRM_SAVE`.
- The parameter file is `PrmDb.dat` in the deployment runtime working
  directory.
- `scienceApp.CONFIGURE_CAPTURE_DURATION` is a volatile runtime override;
  use `PRM_SET` + `PRM_SAVE` when the default should survive restart.

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
fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.ENTER_BASE_MODE \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected event:

```text
MissionApp.ModeChanged mode=BASE
```

`ENTER_BASE_MODE` also cancels any pending scheduled collection.

### 2. SOH Snapshot

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.sohApp.EMIT_SOH_SNAPSHOT \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected behavior:

- SOH events/channels appear in GDS
- telemetry continues moving

### 3. Optional RSSI Check

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.commsApp.PING_LINK_RSSI \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected Pi journal event:

```text
CommsApp.LinkRssiPing state=<state> rssi=<value>dBm
```

Example from the bench:

```text
Comms link RSSI ping state=2 rssi=-5dBm
```

### 4. Configure 30 Second Capture

The default capture duration is already `30` seconds. This command is useful
when you want to make the current runtime setting explicit before a demo.

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.scienceApp.CONFIGURE_CAPTURE_DURATION \
  --arguments 30 \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected event:

```text
ScienceApp.CaptureDurationConfigured durationSeconds=30
```

### 5. Schedule Collection

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.SCHEDULE_COLLECTION \
  --arguments 4 \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected events:

```text
MissionApp.CollectionScheduled delaySeconds=4
ScienceApp.CollectionTriggered delaySeconds=4
```

Wait about 40 seconds for the 4 second delay and 30 second capture window.

Expected events:

```text
ScienceApp.ScienceProductReady productSize=<n>
StorageManager.ScienceStored productCount=<n> size=<n>
```

For a 30 second simulated capture, expect about 30 CSV rows and a few hundred
bytes.

Optional cancel before the delay expires:

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.CANCEL_COLLECTION \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected behavior:

- pending collection is cleared
- mode returns to `BASE`
- no science capture starts from the canceled schedule

### 6. Report Latest Dataset

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.storageManager.REPORT_LATEST_DATASET \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected event:

```text
StorageManager.LatestDataset productCount=<n> size=<n>
```

### 7. Request Science Downlink

Make sure the payload receiver is still running, then send:

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.commsApp.REQUEST_SCIENCE_DOWNLINK \
  --dictionary "$DICT" --log-level-gds ERROR
```

Expected events:

```text
CommsApp.DownlinkRequested bytes=<n>
StorageManager.DownlinkPrepared downlinkBytes=<n>
PayloadDownlinkApp.PayloadDownlinkStarted product=<n> bytes=<n> packets=<n>
PayloadDownlinkApp.PayloadDownlinkComplete transfer=<n> packetsSent=<n>
CommsApp.DownlinkFinished bytes=<n>
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

Teensy watchdog boot lines are visible on bench serial logs:

- `[ArtemisTeensy] hardware watchdog armed (12s)` or
  `[GDS_Teensy] hardware watchdog armed (12s)` is normal boot arming.
- `[ArtemisTeensy] watchdog reset detected` or
  `[GDS_Teensy] watchdog reset detected` means the previous reset was caused by
  the hardware watchdog.

## Fast Troubleshooting

For a broader layer map across GDS, F Prime applications/managers, UART mux, RF, payload
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
fprime-cli command-send ArtemisRpiTeensyDeployment.payloadDownlinkApp.START_PAYLOAD_DOWNLINK \
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
ArtemisRpiTeensyDeployment.storageManager.REMOVE_OLD_DATASETS
confirm = 1
```
