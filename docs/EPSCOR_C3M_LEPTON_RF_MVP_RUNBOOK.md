# EPSCoR C3M Lepton Local And RF MVP Runbook

BLUF: use this for the C3M Lepton laptop proof first, then the RF MVP bench
prep. Local emulation proves the F Prime command/event/Data Product/channel-1
flow and Lepton `.fdp` decode. HIL is still required for real Pi, Teensy UART,
RFM23BP ACK/retry timing, queue drops, and CRC repeatability.

## Scope

Validated locally:

- `PayloadDriver_Lepton` simulated full-res Lepton product generation using
  the real Lepton sample grid from `ground-station/c3m-lepton-test-data`.
- F Prime Data Product write to `DpCat/Dp_*.fdp`.
- `PayloadDownlinkApp` channel-1 packetization and retry flow.
- Ground receiver reconstruction.
- Lepton viewer decode of `160x120` thermal pixels.

Not validated locally:

- real Lepton/libuvc capture.
- Raspberry Pi runtime on `/dev/serial0`.
- Teensy serial bridge behavior.
- RFM23BP airtime, ACK timeouts, retries, or packet loss.
- HIL bench downlink timing.

## Local Preflight

```bash
cd ~/Developer/fprime-artemis-cubesat
git status --short --branch
git submodule status --recursive
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
```

Pass criteria:

- expected branch/worktree is active.
- F Prime virtual environment activates.
- submodules are present.
- `ground-station/c3m-lepton-test-data/Dp_20260707_120740.fdp` and
  `ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv` are present.

## One-Command Local Gate

Run this before HIL or after any transport/payload change:

```bash
cd ~/Developer/fprime-artemis-cubesat
./tools/validate_local.sh --demo c3m
```

This gate checks generated transport constants, Python local-emulation tests,
F Prime generate/build, unit-test generate/build/check, and the automated C3M
local demo.

Pass criteria:

- `validate_local.sh` exits `0`.
- the C3M demo sends the mission, SOH, Lepton enable, capture-duration, and
  scheduled-collection commands.
- a new `ArtemisRpiTeensy_N2/DpCat/Dp_*.fdp` is produced.
- the demo log includes `PayloadDownlinkComplete` and `DownlinkFinished`.
- the viewer summary reports `width=160`, `height=120`, and `pixels=19200`.
- a decoded Lepton PNG is written under the run log directory.
- the decoded local-demo CSV matches
  `ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv`, ignoring
  only capture-time metadata.
- the script prints:
  `PASS: local EPSCoR C3M demo produced, downlinked, and decoded a Lepton .fdp`.

## Manual Local Demo

Use this when debugging a failure from the umbrella gate:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
./tools/run_c3m_local_demo.sh --delay 10 --capture-seconds 10 --exit-after-sequence
```

The manual demo writes JSON, CSV, and PNG outputs under the run log directory
and opens the decoded Lepton PNG after the downlink and decode checks pass. Use
`--no-open` when running headless or inside automated validation.

By default, `run_c3m_local_demo.sh` exports `C3M_LEPTON_SAMPLE_CSV` to the
checked-in real Lepton sample:

```text
ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv
```

The resulting local `.fdp` must decode back to that same 120x160 grid. Override
with `--sample-csv <path>` only when intentionally testing a different Lepton
sample.

The script writes logs under:

```text
ArtemisRpiTeensy_N2/tools/logs/c3m_local_demo_<timestamp>/
```

Manual decode check:

```bash
cd ~/Developer/fprime-artemis-cubesat
python3 ground-station/lepton-dp-viewer/lepton_dp_viewer.py \
  ArtemisRpiTeensy_N2/DpCat/Dp_*.fdp \
  --summary
```

Use the newest `.fdp` file from `DpCat` if the shell expands multiple products.

## Local Optimization Gate

Local emulation can verify that these optimizations compile and preserve the
generic C3M dataflow:

- generated payload pacing constants.
- generated per-channel RF ACK policy constants.
- channel-1 payload transfer shape.
- Lepton `.fdp` reconstruction and decode.

Local emulation cannot prove RF throughput. Treat any ACK-off or pacing change
as bench-candidate work until HIL counters prove it.

Current optimized RF policy:

- Channel 0 / CCSDS: ACK required.
- Channel 1 / payload: ACK not required.
- App-level repair remains owned by `PayloadDownlinkApp` and
  `payload_receiver.py` through packet indexes, retry requests, and final CRC.

Rollback rule:

- If HIL shows payload CRC failures, queue drops, or unacceptable retry rounds,
  set payload ACK back on in `config/transport_constants.json`, regenerate
  transport constants, rebuild both Teensy sketches, and rerun this local gate.

## Firmware Build Gates

Build both bridge firmwares before HIL:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh

cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Pass criteria:

- both builds exit `0`.
- no generated build/cache files are staged.

## Pi Artifact Gate

If Docker is available locally, cross-compile the Pi deployment before HIL:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh --local-only
```

Pass criteria:

- ARMv6 artifact is produced.
- script `file` / `readelf` checks pass.
- no SSH deploy is attempted from this local-only gate.

## RF MVP Bench Prep

Recheck ports every HIL session:

```bash
ls -l /dev/cu.usbmodem* /dev/tty.usbmodem* 2>/dev/null
python3 -m serial.tools.list_ports -v
```

Common macOS mapping:

| Port | Purpose |
| --- | --- |
| first ground Teensy triple-serial port | GDS channel-0 data |
| second ground Teensy triple-serial port | ground debug counters |
| third ground Teensy triple-serial port | channel-1 payload receiver |
| satellite Teensy single serial port | satellite debug counters |

Set the ports for the session:

```bash
GDS_DATA_PORT=/dev/cu.usbmodem...
GDS_DEBUG_PORT=/dev/cu.usbmodem...
GDS_PAYLOAD_PORT=/dev/cu.usbmodem...
SAT_DEBUG_PORT=/dev/cu.usbmodem...
lsof "$GDS_DATA_PORT" "$GDS_DEBUG_PORT" "$GDS_PAYLOAD_PORT" "$SAT_DEBUG_PORT" 2>/dev/null || true
```

Pi service preflight:

```bash
ssh artemis-pi 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
```

Expected:

- service is `active`.
- deployment is running with `-d /dev/serial0`.
- no stale local process owns the GDS data or payload ports.

## Start RF MVP Tools

Terminal 1: GDS over channel 0.

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
./tools/run_gds_uart.sh \
  --port "$GDS_DATA_PORT" \
  --baud 115200 \
  --gui-port 5050 \
  --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Terminal 2: payload receiver on channel 1.

```bash
cd ~/Developer/fprime-artemis-cubesat
RUN_DIR=/tmp/neutron_hil/c3m_rf_demo_$(date +%Y%m%d_%H%M%S)
mkdir -p "$RUN_DIR"
echo "$RUN_DIR" | tee /tmp/neutron_hil/latest_c3m_rf_demo_dir
python3 -u tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output-dir "$RUN_DIR" \
  --timeout 240 \
  --continuous
```

After the receiver writes a `.fdp`, decode it:

```bash
cd ~/Developer/fprime-artemis-cubesat
RUN_DIR="$(cat /tmp/neutron_hil/latest_c3m_rf_demo_dir)"
python3 ground-station/lepton-dp-viewer/lepton_dp_viewer.py \
  "$RUN_DIR"/*.fdp \
  --summary --no-show
```

## RF MVP Command Sequence

Use GDS or `fprime-cli` with the Pi dictionary.

```bash
DICT=~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json

fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.ENTER_BASE_MODE --dictionary "$DICT" --log-level-gds ERROR
fprime-cli command-send ArtemisRpiTeensyDeployment.sohApp.EMIT_SOH_SNAPSHOT --dictionary "$DICT" --log-level-gds ERROR
fprime-cli command-send ArtemisRpiTeensyDeployment.payloadDriverLepton.ENABLE --dictionary "$DICT" --log-level-gds ERROR
fprime-cli command-send ArtemisRpiTeensyDeployment.scienceApp.CONFIGURE_CAPTURE_DURATION --arguments 10 --dictionary "$DICT" --log-level-gds ERROR
fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.SCHEDULE_COLLECTION --arguments 10 --dictionary "$DICT" --log-level-gds ERROR
fprime-cli command-send ArtemisRpiTeensyDeployment.storageManager.REPORT_LATEST_DATASET --dictionary "$DICT" --log-level-gds ERROR
fprime-cli command-send ArtemisRpiTeensyDeployment.commsApp.REQUEST_SCIENCE_DOWNLINK --dictionary "$DICT" --log-level-gds ERROR
fprime-cli command-send ArtemisRpiTeensyDeployment.payloadDownlinkApp.GET_PAYLOAD_STATUS --dictionary "$DICT" --log-level-gds ERROR
```

## HIL Pass Criteria

Record these before changing pacing or ACK policy again:

- wall-clock time from `PayloadDownlinkStarted` to `PayloadDownlinkComplete`.
- receiver reports `complete` and exits cleanly when not in continuous mode.
- Lepton viewer parses `width=160`, `height=120`, `pixels=19200`.
- final CRC succeeds.
- `rf_retries`, `rf_ack_timeouts`, `rf_msg_id_gaps`, and queue drops are
  recorded from both Teensy debug ports.
- channel-0 commands remain responsive during channel-1 payload downlink.
- any retry round count is acceptable only if the final file is byte-correct and
  the demo timing remains acceptable.

## Stop Rules

Stop and roll back or slow down the optimization if any of these happen:

- channel-0 commands or telemetry become unreliable.
- payload receiver never reaches final CRC.
- Teensy queue drops appear.
- RF message-id gaps climb continuously.
- Pi deployment restarts or watchdogs.
- C3M full-res HIL timing exceeds the live-demo window after retries.

## Cleanup

```bash
pkill -f 'tools/payload_receiver.py' 2>/dev/null || true
rm -rf /tmp/neutron_hil/c3m_rf_demo_*
```

Do not delete run folders until hashes, logs, and screenshots needed for the
FSR writeup have been saved.
