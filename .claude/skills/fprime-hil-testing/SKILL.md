---
name: fprime-hil-testing
description: "Use when guiding live or rehearsal hardware-in-the-loop testing for the Artemis/Neutron 2 F Prime repo: staged ground Teensy and satellite Teensy bring-up, USB serial enumeration, Raspberry Pi SSH/service checks, fprime-gds launch, payload receiver/viewer setup, RF MVP smoke tests, demo-story validation, and HIL handoff/debugging on macOS or Windows WSL2."
---

# F Prime HIL Testing

## Operating Style

Keep it KISS: one physical step, one confirmation, then move on. Do not assume
old ports, old IPs, old SSH aliases, or old running processes. The goal is to
walk the operator through live HIL safely:

1. Plug in the ground/GDS Teensy.
2. Confirm triple serial.
3. Plug in the satellite Teensy.
4. Confirm the satellite serial port.
5. Confirm Raspberry Pi SSH and `artemis-fprime.service`.
6. Start GDS and payload receiver.
7. Run the smallest useful smoke test, then the full demo story only if asked.

If hardware is absent, do not fail the task. Say what could be checked locally
and what remains blocked by hardware.

## First Read

From the repo root, read these before acting:

```bash
git status --short --branch
git submodule status --recursive
sed -n '1,220p' AGENTS.md
sed -n '1,220p' README.md
sed -n '1,260p' docs/SYSTEM_ARCHITECTURE.md
sed -n '400,490p' docs/agents_notes.md
sed -n '1,140p' docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md
```

Use `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md` as the current HIL proof source. Older
`HIL_TEST_HANDOFF_*` files may be stale or branch-specific.

## Platform Detection

Do not assume the operator OS. Detect it first.

macOS:

```bash
uname -s
# Darwin
```

Windows team laptops should use WSL2 for F Prime, serial, SSH, and repo scripts:

```bash
uname -s
# Linux
grep -qi microsoft /proc/version && echo WSL2
```

Native Windows browser use is fine for:

```text
http://127.0.0.1:<gds-port>
http://127.0.0.1:8062
```

Do not present native PowerShell/CMD as the F Prime HIL path unless the repo
later documents it.

## Serial Commands By Platform

macOS:

```bash
ls -l /dev/cu.usbmodem* /dev/tty.usbmodem* 2>/dev/null || true
python3 -m serial.tools.list_ports -v
```

Windows WSL2:

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true
python3 -m serial.tools.list_ports -v
```

If system Python lacks `pyserial`, use the repo venv Python after activating
`ArtemisRpiTeensy_N2/fprime-venv`.

If WSL2 does not show the Teensy devices, attach them from Windows with
`usbipd-win` from an **administrator PowerShell** window (`usbipd bind --busid
<BUSID>` once, then `usbipd attach --wsl --busid <BUSID>` each session), then
re-run the WSL2 checks. Full procedure: `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md`.

If a serial command fails with `Permission denied` on WSL2 or Linux, the
durable fix is `sudo usermod -aG dialout "$USER"` plus a WSL restart
(or re-login). A one-off `sudo` on the specific serial command is acceptable
in a demo emergency, but keep the repo venv and the same port; never `sudo`
builds or `fprime-util`.

macOS note: `timeout` used below needs coreutils (`brew install coreutils`,
command `gtimeout`); otherwise run `cat <port>` and Ctrl-C after a few seconds.

## Teensy Upload Targeting: Mandatory

Never upload to a Teensy by `/dev/cu.usbmodem*`, `/dev/tty.usbmodem*`,
`/dev/ttyACM*`, or `/dev/ttyUSB*` when more than one Teensy is connected.
Arduino/Teensy CLI can report success while using auto-search against the wrong
physical board. This can leave the ground board enumerating as triple serial but
running the wrong or stale behavior, which looks like a dead RF/GDS link.

Before any Teensy upload, list physical Teensy upload IDs:

```bash
arduino-cli board list
```

Current HIL bench mapping:

```text
ground Teensy upload ID    = usb:100000
satellite Teensy upload ID = usb:2100000
```

Use those `usb:*` IDs for uploads:

```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000

cd ../ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:2100000
```

If an upload wrapper refuses a serial-device upload while multiple Teensys are
connected, that refusal is correct. Do not bypass it by retrying with another
`/dev/*` serial path. Use `arduino-cli board list`, identify the `usb:*` target,
and upload by that physical ID.

After flashing, prove behavior, not just enumeration:

```bash
python3 -m serial.tools.list_ports -v
timeout 5 cat <ground-debug-port>
timeout 5 cat <sat-debug-port>
```

Expected post-flash proof:

```text
ground debug prints [GDS_Teensy] counters
ground data port shows nonzero raw bytes when GDS is not attached and RF downlink is active
satellite debug prints [ArtemisTeensy] counters
```

Do not treat triple-serial enumeration alone as proof that the correct ground
firmware is running.

## Stage 1: Ground Teensy

Tell the user to plug in only the ground/GDS Teensy, then enumerate serial
devices.

Expected triple-serial shape:

```text
first ground port  = GDS channel 0 data
second ground port = ground debug counters
third ground port  = payload channel 1 receiver
```

Common macOS shape:

```text
/dev/cu.usbmodem...01 = GDS channel 0 data
/dev/cu.usbmodem...03 = ground debug counters
/dev/cu.usbmodem...05 = payload channel 1 receiver
```

Common WSL2 shape:

```text
/dev/ttyACM0 = GDS channel 0 data
/dev/ttyACM1 = ground debug counters
/dev/ttyACM2 = payload channel 1 receiver
```

If only one serial port appears, check that the ground firmware build path is
configured for triple serial:

```bash
rg -n "usb=serial3|SerialUSB1|SerialUSB2" GDS_Teensy
```

Only reflash when the user asks for it or evidence says the firmware is wrong:

```bash
cd GDS_Teensy
arduino-cli board list
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000
```

After flashing, re-enumerate serial ports and sample the debug stream. Do not
continue until the ground port roles are clear and the ground debug stream is
printing `[GDS_Teensy] counters`.

## Stage 2: Satellite Teensy

Tell the user to plug in the satellite Teensy. Re-enumerate serial devices and
identify the newly added single serial port that is not part of the ground
triple-serial group.

Build and upload only to the satellite board:

```bash
cd ArtemisTeensy_N2_Baremetal
arduino-cli board list
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:2100000
```

Stop before repeated upload retries if Teensy needs a physical PROGRAM press.
Ask the user to press PROGRAM, then retry once.

Expected result:

```text
ground data/debug/payload ports still present
satellite debug serial present as one separate port
```

## Stage 3: Raspberry Pi SSH And Service

Check SSH aliases, then probe with timeouts:

```bash
ssh -G n2pi | rg '^(host|hostname|user|identityfile|port) '
ssh -G artemis-pi | rg '^(host|hostname|user|identityfile|port) '
ping -c 3 <candidate-ip>
ssh -o BatchMode=yes -o ConnectTimeout=5 artemis-pi 'hostname; hostname -I; whoami; uname -m'
```

Do not silently switch aliases. If `n2pi` is stale but `artemis-pi` reaches the
confirmed Pi, say that plainly and use the confirmed alias only after the user
confirms or the task context clearly permits it.

Check the runtime service:

```bash
ssh -o BatchMode=yes -o ConnectTimeout=5 artemis-pi \
  'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment || true'
```

Expected runtime shape:

```text
/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0
```

Use bounded journal reads:

```bash
ssh artemis-pi 'journalctl -u artemis-fprime.service --since "2 minutes ago" --no-pager | tail -80'
```

## Stage 4: Clean Local Process State

Before opening GDS or the payload receiver, check for existing owners:

```bash
pgrep -af 'fprime-gds|fprime-cli|payload_receiver|neutron2_payload_viewer|ArtemisRpiTeensyDeployment' || true
lsof <ground-data-port> <ground-debug-port> <ground-payload-port> <sat-debug-port> 2>/dev/null || true
```

If `fprime-gds` is already running on the ground data port, preserve it. Do not
start a second serial reader on the same port.

## Stage 5: Start GDS

From `ArtemisRpiTeensy_N2`:

```bash
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json

fprime-gds -n \
  --communication-selection uart \
  --uart-device "$GDS_DATA_PORT" \
  --uart-baud 115200 \
  --framing-selection space-packet-space-data-link \
  --dictionary "$DICT" \
  --gui-port "${GDS_GUI_PORT:-5056}" \
  --log-to-stdout \
  --log-level-gds INFO
```

Use the selected GUI port in the browser:

```text
http://127.0.0.1:<gds-port>
```

If the user asks to leave GDS running, use a durable Terminal, `tmux`, or
`screen`. Do not rely on a background process that will die when the tool shell
exits.

## Stage 6: Command Smoke

Send the smallest command-path smoke first:

```bash
. fprime-venv/bin/activate
fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.PING \
  --arguments 4245 \
  --dictionary "$DICT" \
  --log-level-gds ERROR
```

Check the Pi journal for command receipt:

```bash
ssh artemis-pi 'journalctl -u artemis-fprime.service --since "45 seconds ago" --no-pager | egrep "MissionApp|PING|pong|Op[Cc]ode|completed|ERROR|WARNING" | tail -100'
```

If command bytes reach ground debug counters but the Pi never sees the command,
read the ground and satellite debug streams before reflashing. The useful RF
triage pattern is:

```text
uart_rx rises
rf_tx_pkt stays 0 or satellite rf_rx_pkt stays 0
rf_tx_drops / rf_retries / rf_ack_timeouts rise
```

That points at ground-to-satellite RF uplink/ACK behavior, not the payload
receiver.

If GDS shows no live decoded bytes after flashing the ground Teensy, check the
ground ports before blaming RF:

```bash
lsof "$GDS_DATA_PORT" "$GDS_DEBUG_PORT" "$GDS_PAYLOAD_PORT" 2>/dev/null || true
timeout 5 cat "$GDS_DEBUG_PORT"
timeout 5 cat "$GDS_DATA_PORT"
```

If the ground triple serial ports enumerate but all ground streams are silent
while the satellite debug stream shows increasing `rf_tx_pkt`, suspect a wrong
or stale ground upload first. Reflash the ground board by physical upload ID:

```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000
```

Known false-positive failure mode:

```text
ground enumerates as Triple Serial
GDS HTTP opens
event.log/channel.log/recv.bin remain empty
satellite rf_tx_pkt, rf_retries, rf_ack_timeouts keep increasing
ground debug/data streams are silent
```

This is not a GDS green-dot problem. It means the ground bridge is not producing
data for GDS, commonly because the wrong board was targeted by an ambiguous
serial-device upload.

## Stage 7: Payload Receiver And Viewer

The viewer only displays files. It does not reconstruct downlinks. Start
`payload_receiver.py` before requesting science downlink.

From the repo root:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
RUN_DIR=/tmp/neutron_hil/$(date +%Y%m%d_%H%M%S)
mkdir -p "$RUN_DIR"

python -u ArtemisRpiTeensy_N2/tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output "$RUN_DIR/payload.bin" \
  --timeout 180
```

Viewer summary check:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --summary "$RUN_DIR/payload.bin"
```

Viewer UI:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --capture-dir "$RUN_DIR" \
  --port 8062
```

Do not use `curl -I` as the viewer health check; the viewer may not support
`HEAD`. Use `GET` or open the browser.

## Stage 8: Full Demo Story

Only run this after the command smoke is sane or when the user explicitly wants
the full demo.

```bash
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json

fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.ENTER_BASE_MODE --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.sohApp.EMIT_SOH_SNAPSHOT --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.scienceApp.CONFIGURE_CAPTURE_DURATION --arguments 30 --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.SCHEDULE_COLLECTION --arguments 4 --dictionary "$DICT"
sleep 40
fprime-cli command-send ArtemisRpiTeensyDeployment.storageManager.REPORT_LATEST_DATASET --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.commsApp.REQUEST_SCIENCE_DOWNLINK --dictionary "$DICT"
fprime-cli command-send ArtemisRpiTeensyDeployment.payloadDownlinkApp.GET_PAYLOAD_STATUS --dictionary "$DICT"
```

`GET_PAYLOAD_STATUS` is useful because it can force the latest
`PayloadDownlinkProgress` into the GDS Events tab after a lossy downlink.

## Pass Criteria

Do not call a HIL run good just because a command was sent. A useful pass needs
the relevant proof bundle:

```text
GDS receives live F Prime events/telemetry
payload receiver prints complete
local payload file exists and is nonzero
viewer summary parses the payload
local hash matches Pi /tmp/neutron_payload_captures/latest_payload.bin when that source exists
Pi journal shows PayloadDownlinkProgress and DownlinkFinished
```

Hash check:

```bash
LOCAL="$RUN_DIR/payload.bin"
printf 'local  '; shasum -a 256 "$LOCAL" 2>/dev/null || sha256sum "$LOCAL"
printf 'remote '; ssh artemis-pi 'sha256sum /tmp/neutron_payload_captures/latest_payload.bin'
ssh artemis-pi 'readlink -f /tmp/neutron_payload_captures/latest_payload.bin; wc -c /tmp/neutron_payload_captures/latest_payload.bin'
```

GDS log check:

```bash
rg -n "PayloadDownlinkProgress|PayloadDownlinkComplete|DownlinkFinished|LatestDataset" \
  ArtemisRpiTeensy_N2/logs -g 'event.log' -g 'channel.log'
```

Pi journal check:

```bash
ssh artemis-pi 'journalctl -u artemis-fprime.service --since "5 minutes ago" --no-pager | egrep "PayloadDownlinkProgress|PayloadDownlinkComplete|DownlinkFinished|LatestDataset|UnexpectedSequenceCount" | tail -120'
```

APID sequence warnings mean the RF/GDS stream is lossy; they do not by
themselves mean the link is dead.

## Common Stop Rules

Stop and ask before:

- repeated Teensy upload attempts that need manual PROGRAM
- reflashing when evidence does not point to stale or wrong firmware
- uploading by serial device path while multiple Teensys are connected
- killing an existing GDS process the user asked to keep open
- switching from `n2pi` to `artemis-pi` when the user named a specific target
- claiming merge/demo readiness from a single narrow smoke test

Never treat these as payload proof:

```text
triple serial exists
ground triple serial exists but debug/data streams are silent
fprime-cli returned success
GDS recv.bin exists
viewer opened with no capture file
```

`ArtemisRpiTeensy_N2/logs/.../recv.bin` is channel 0 GDS traffic, not the
reconstructed science payload.
