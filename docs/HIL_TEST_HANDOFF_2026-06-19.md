# HIL Test Handoff - 2026-06-19

## 2026-06-23 Update - Three-Pass Gate Passed

Tuesday HIL repeated the full shortened demo story three times cleanly after
redeploying the Pi Zero W binary with
`PayloadDownlinkManager.downlinkRequestIn` changed to an async input port.

Deployed Pi binary:

```text
94578e82e3467143ceb569017ecf876582880046e5d3b1956fa21fb050f0f588  /home/pi/artemis/current/ArtemisRpiTeensyDeployment
```

Matching deployed/local dictionary:

```text
f34e7f9bf05e3e1a0338b7f976fad6637719392288d3905b70f7d9788a1e41e0  ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Evidence files on the laptop:

- GDS logs: `/tmp/neutron_hil/pass_async_1_gds/`
- Payload CSVs:
  - `/tmp/neutron_hil/pass_async_1_payload.csv`
  - `/tmp/neutron_hil/pass_async_2_payload.csv`
  - `/tmp/neutron_hil/pass_async_3_payload.csv`
- Payload receiver logs:
  - `/tmp/neutron_hil/pass_async_1_payload.log`
  - `/tmp/neutron_hil/pass_async_2_payload.log`
  - `/tmp/neutron_hil/pass_async_3_payload.log`

Three pass results:

| Pass | Pi capture | Bytes | SHA-256 | Viewer rows | Journal evidence |
|---|---:|---:|---|---:|---|
| 1 | `neutron_capture_20260623T212811Z_00012_00006.csv` | 83 | `eabd0e9f1ddbff5224617affffc72d187b53bffe5bf83426714f5f5359157165` | 6 | `DownlinkFinished 83 bytes` |
| 2 | `neutron_capture_20260623T213030Z_00018_00006.csv` | 83 | `b4cd40d47c8b7cef9fc407ab7013fdc8831e2d31651e322921592e7f97fe792c` | 6 | `DownlinkFinished 83 bytes` |
| 3 | `neutron_capture_20260623T213153Z_00024_00006.csv` | 83 | `00a1f94fa03521938757e4e6f85fde76b81ca82fe04099f74fa0c177ca7ccfa7` | 6 | `DownlinkFinished 83 bytes` |

Each pass used the normal demo command path:

1. `missionManager.ENTER_BASE_MODE`
2. `sohManager.EMIT_SOH_SNAPSHOT`
3. `scienceManager.CONFIGURE_CAPTURE_DURATION --arguments 6`
4. `missionManager.SCHEDULE_COLLECTION --arguments 4`
5. `storageService.REPORT_LATEST_DATASET`
6. `commsManager.REQUEST_SCIENCE_DOWNLINK`

The downlinked laptop CSV hash matched the Pi
`/tmp/neutron_payload_captures/latest_payload.bin` hash for all three passes.
The ground viewer parsed all three CSVs with `--summary`.

Remaining caveat: the RF link still shows APID sequence-count warnings and the
payload receiver needed retry rounds on passes 1 and 3. The retry path recovered
successfully, so this is now repeatable enough for the current demo story, but
the RF loss should remain visible in the handoff.

## Original 2026-06-19 BLUF

Friday HIL proved the full shortened demo story once end-to-end:

1. Ground commanded Base Mode / SOH.
2. Ground scheduled a 6 second neutron simulator collection after a 4 second delay.
3. Raspberry Pi F Prime invoked the separate Pi-hosted neutron simulator.
4. F Prime stored the latest 83 byte CSV product.
5. `commsManager.REQUEST_SCIENCE_DOWNLINK` sent that product through:
   - RPi F Prime
   - satellite Teensy
   - RFM23BP RF link
   - ground Teensy triple-serial payload port
   - laptop payload receiver
6. The downlinked CSV matched the Pi simulator output byte-for-byte and the viewer parsed it.

This is strong progress, but do not call the branch merge-ready until Monday repeats the full story several times cleanly. The RF channel still drops packets/END sometimes; the receiver has been hardened locally, but repeatability still needs proof.

## Hardware / SSH State

- Confirmed Raspberry Pi:
  - IP: `192.168.0.152`
  - working SSH profile: `artemis-pi`
  - host: `raspberrypi`
  - user: `pi`
  - arch: `armv6l`
- Stale SSH profile:
  - `n2pi` still points at `192.168.5.140` and timed out.
  - Overriding `n2pi` to `192.168.0.152` reached the Pi but the `n2pi` key was rejected.
- Working key/profile:

```text
Host artemis-pi
HostName 192.168.0.152
User pi
IdentityFile ~/.ssh/id_ed25519_artemis_pi
```

## USB Serial Ports Seen Friday

Ground Teensy triple serial:

- GDS / channel 0 data: `/dev/cu.usbmodem115551201`
- debug: `/dev/cu.usbmodem115551203`
- payload / channel 1: `/dev/cu.usbmodem115551205`

Satellite Teensy debug:

- `/dev/cu.usbmodem115502201`

Always re-check on Monday:

```bash
ls -l /dev/cu.usbmodem* /dev/tty.usbmodem*
```

## Pi Runtime State

F Prime service:

```bash
ssh artemis-pi 'systemctl status artemis-fprime.service --no-pager'
```

Service definition observed Friday:

```text
WorkingDirectory=/home/pi/artemis/current
ExecStart=/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0
```

The deployed binary and dictionary are symlinked from `/home/pi/artemis/current` to `/home/pi/artemis/cross`.

Verified deployed binary hash Friday:

```text
3f1288b855116b8ce567a88d9f9f712dc307d6711ff76a437af4b2acf639a566  /home/pi/artemis/current/ArtemisRpiTeensyDeployment
```

Verified dictionary hash Friday:

```text
1f9346795b3b5e2ff6345aa50a043d55e7fd9a318d22ecdbf0917f8b7e209eb2
```

## Pi Neutron Simulator

Simulator installed on Pi:

```text
/home/pi/artemis/external/payload-neutron-simulation/neutron_payload_sim.py
/home/pi/artemis/external/payload-neutron-simulation/neutron_data.csv
```

F Prime adapter source:

```text
ArtemisRpiTeensy_N2/Components/PayloadAdapter_NeutronSim/PayloadAdapter_NeutronSim.cpp
```

The adapter shells out to:

```bash
python3 <sim-root>/neutron_payload_sim.py capture \
  --duration-seconds <duration> \
  --dataset <sim-root>/neutron_data.csv \
  --cursor /tmp/neutron_payload_sim_cursor.json \
  --output-dir /tmp/neutron_payload_captures \
  --end-policy wrap \
  --format kv
```

Because the service runs from `/home/pi/artemis/current`, the adapter default `../external/payload-neutron-simulation` resolves to the Pi simulator copy.

Friday simulator smoke test:

```bash
ssh artemis-pi '
  cd /home/pi/artemis &&
  python3 external/payload-neutron-simulation/neutron_payload_sim.py capture \
    --duration-seconds 2 \
    --dataset external/payload-neutron-simulation/neutron_data.csv \
    --cursor /tmp/neutron_payload_sim_cursor_smoke.json \
    --output-dir /tmp/neutron_payload_captures_smoke
'
```

Result: produced a 35 byte smoke CSV in `/tmp/neutron_payload_captures_smoke`.

## Local Changes Made During HIL

### Topology

File:

```text
ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.fpp
```

Restored the scheduled science path:

```text
rateGroup1.RateGroupMemberOut[8] -> scienceManager.run
```

Without this, scheduled collections did not actually trigger capture.

### Payload Downlink Manager

Files:

```text
ArtemisRpiTeensy_N2/Components/PayloadDownlinkManager/PayloadDownlinkManager.cpp
ArtemisRpiTeensy_N2/Components/PayloadDownlinkManager/PayloadDownlinkManager.hpp
ArtemisRpiTeensy_N2/Components/PayloadDownlinkManager/test/ut/*
```

Purpose:

- paced payload downlink packets to avoid blasting channel 1
- queued retry requests instead of resending immediately inside the packet handler
- added/updated unit coverage for retry queue behavior

Friday verification:

```bash
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
build-fprime-automatic-native-ut/bin/Darwin/Components_PayloadDownlinkManager_ut_exe
```

Result: 2 tests passed.

### UART Channel Mux

File:

```text
ArtemisRpiTeensy_N2/Components/UartChannelMux/UartChannelMux.fpp
```

Changed channel send input ports to guarded ports:

```text
guarded input port payloadSendIn
guarded input port localSendIn
```

This improved channel 1 behavior under mixed F Prime / payload traffic.

### Payload Receiver

File:

```text
ArtemisRpiTeensy_N2/tools/payload_receiver.py
```

Hardening added Friday:

- repeated retry requests every 5 seconds while missing packets remain
- retry after a header followed by 8 seconds of silence, even if END was lost
- exit as soon as all declared packets are received, even if END was lost

Syntax check passed:

```bash
cd ArtemisRpiTeensy_N2
python -m py_compile tools/payload_receiver.py
```

## Friday End-to-End Evidence

Fresh scheduled capture:

```text
/tmp/neutron_payload_captures/neutron_capture_20260620T014359Z_00030_00006.csv
```

Pi events:

```text
SOH Snapshot
CaptureDurationConfigured duration=6s
CollectionScheduled delay=4s
CollectionTriggered
PayloadCollectionForwarded key=6
CaptureComplete duration=6s rows=6 bytes=83
ScienceProductReady size=83
ScienceStored count=4 size=83
LatestDataset count=4 size=83
DownlinkRequested 83 bytes
DownlinkPrepared size=83
PayloadDownlinkStarted product=5 bytes=83 packets=3
PayloadDownlinkComplete transfer=5 packetsSent=3
DownlinkFinished 83 bytes
```

Receiver output:

```text
header: product=5 transfer=5 bytes=83 packets=3 crc=0x5f79
progress: 3/3
complete: product=5 transfer=5 bytes=83 packets=3 crc=0x5f79 output=/tmp/neutron_hil/hil_payload_comms_manager_retry3.csv
```

Hash match:

```text
98a341d7ef067e166da2753c9c3eea7438f1970406dc38eb37ca429012f3511b  /tmp/neutron_hil/hil_payload_comms_manager_retry3.csv
98a341d7ef067e166da2753c9c3eea7438f1970406dc38eb37ca429012f3511b  /tmp/neutron_payload_captures/latest_payload.bin
```

Viewer summary:

```json
{
  "bg_mean": 0.0,
  "bg_rows": 0,
  "end_t_s": 35,
  "max_counts": 39,
  "mean_counts": 28.333333333333332,
  "median_counts": 27.5,
  "min_counts": 17,
  "rows": 6,
  "saa_mean": 28.333333333333332,
  "saa_rows": 6,
  "start_t_s": 30,
  "total_counts": 170
}
```

## Known Issues / Findings

- `n2pi` SSH alias is stale. Use `artemis-pi` unless config is updated.
- `CommsManager` clears `m_pendingScienceBytes` after a downlink attempt. A second `REQUEST_SCIENCE_DOWNLINK` without a fresh capture is rejected with:

```text
DownlinkFailed state=0 error=1
```

- RF/channel 1 is not perfectly reliable:
  - one earlier comms-manager run saw header plus partial data and ended incomplete
  - one run saw header and all data but lost END
  - receiver hardening now handles the complete-data-without-END case and can retry after header silence
- Friday proved one complete end-to-end downlink, but repeatability is not proven.
- `SOH` currently reports several subsystem states as `UNKNOWN`; this is acceptable for the demo only if the team is comfortable explaining the MVP scope.

## Monday Test Plan

### 1. Preflight

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
git status --short --branch
git submodule status --recursive
ls -l /dev/cu.usbmodem* /dev/tty.usbmodem*
ssh artemis-pi 'systemctl is-active artemis-fprime.service && hostname && uname -m'
```

Confirm:

- Pi is `192.168.0.152`.
- Ground Teensy triple serial is present.
- Satellite Teensy is connected.
- F Prime service is active.

### 2. Start GDS

Use the ground Teensy channel 0 data port:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
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

### 3. Run the demo command story

In another terminal:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json

fprime-cli command-send ArtemisRpiTeensyDeployment.missionManager.ENTER_BASE_MODE --dictionary "$DICT" --log-level-gds INFO
fprime-cli command-send ArtemisRpiTeensyDeployment.sohManager.EMIT_SOH_SNAPSHOT --dictionary "$DICT" --log-level-gds INFO
fprime-cli command-send ArtemisRpiTeensyDeployment.scienceManager.CONFIGURE_CAPTURE_DURATION --arguments 6 --dictionary "$DICT" --log-level-gds INFO
fprime-cli command-send ArtemisRpiTeensyDeployment.missionManager.SCHEDULE_COLLECTION --arguments 4 --dictionary "$DICT" --log-level-gds INFO
sleep 14
fprime-cli command-send ArtemisRpiTeensyDeployment.storageService.REPORT_LATEST_DATASET --dictionary "$DICT" --log-level-gds INFO
```

Confirm Pi events:

```bash
ssh artemis-pi '
  printf "latest="; readlink /tmp/neutron_payload_captures/latest_payload.bin
  journalctl -u artemis-fprime.service --since "2 minutes ago" --no-pager |
    egrep "SOH|Collection|Capture|Science|LatestDataset|Payload" || true
'
```

### 4. Receive science payload over RF

Start receiver first:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
rm -f /tmp/neutron_hil/monday_payload.csv
python -u tools/payload_receiver.py \
  --port /dev/cu.usbmodem115551205 \
  --baud 115200 \
  --output /tmp/neutron_hil/monday_payload.csv \
  --timeout 180
```

Then trigger downlink:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
fprime-cli command-send ArtemisRpiTeensyDeployment.commsManager.REQUEST_SCIENCE_DOWNLINK --dictionary "$DICT" --log-level-gds INFO
```

Expected receiver output:

```text
header: ...
progress: N/N
complete: ...
```

### 5. Verify byte match and viewer

```bash
LOCAL=/tmp/neutron_hil/monday_payload.csv
printf 'local  '; shasum -a 256 "$LOCAL"
printf 'remote '; ssh artemis-pi 'sha256sum /tmp/neutron_payload_captures/latest_payload.bin'

cd /Users/sozodennis/Developer/fprime-artemis-cubesat
python ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --summary "$LOCAL"
```

Pass criteria:

- receiver exits code 0
- local and remote hashes match
- viewer emits a valid JSON summary
- Pi journal shows `DownlinkFinished`

### 6. Repeatability Gate Before Merge

Run the complete story at least 3 times:

- schedule fresh capture
- downlink through `commsManager`
- verify hash match
- run viewer

Merge readiness recommendation:

- 3/3 passing runs: likely OK to merge after normal build/test review.
- 2/3 or worse: keep branch open and debug RF/receiver/downlink pacing.
- Any receiver incomplete case: save receiver output plus Pi journal window before retrying.

## Useful Debug Commands

Pi journal:

```bash
ssh artemis-pi 'journalctl -u artemis-fprime.service --since "5 minutes ago" --no-pager | egrep "Downlink|PayloadRetry|PayloadDownlink|Science|Capture|SOH|LatestDataset" || true'
```

Latest Pi captures:

```bash
ssh artemis-pi 'ls -lt /tmp/neutron_payload_captures | sed -n "1,12p"; readlink /tmp/neutron_payload_captures/latest_payload.bin'
```

Direct payload-manager downlink fallback:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT=build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
fprime-cli command-send ArtemisRpiTeensyDeployment.payloadDownlinkManager.START_PAYLOAD_DOWNLINK --arguments 9 83 --dictionary "$DICT" --log-level-gds INFO
```

Use this only to isolate channel 1 payload transport. The merge gate should use `commsManager.REQUEST_SCIENCE_DOWNLINK`.

## Validation Already Run Friday

```bash
cd ArtemisRpiTeensy_N2
python -m py_compile tools/payload_receiver.py

. fprime-venv/bin/activate
build-fprime-automatic-native-ut/bin/Darwin/Components_PayloadDownlinkManager_ut_exe
```

Results:

- payload receiver syntax check passed
- `PayloadDownlinkManager` unit test binary passed 2/2 tests
- one full comms-manager RF payload downlink passed with hash match and viewer summary

## Suggested Monday Cleanup Before Merge

- Decide whether to update `~/.ssh/config` so `n2pi` points at `192.168.0.152` with the working Artemis key, or stop using `n2pi`.
- Consider adding a small receiver test for:
  - missing END but all packets present
  - header followed by silence, then retry request
- Re-run native F Prime build if Monday changes C++:

```bash
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
fprime-util generate -f
fprime-util build -j8
```

- If any flight-side C++ changes are made Monday, cross-build and redeploy to the Pi before claiming HIL results.
