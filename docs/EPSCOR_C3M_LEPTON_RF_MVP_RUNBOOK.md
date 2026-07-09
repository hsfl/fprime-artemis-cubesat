# EPSCoR C3M Lepton Local And RF MVP Runbook

BLUF: use this for the C3M Lepton laptop proof and the validated RF MVP bench
flow. Local emulation proves the F Prime command/event/Data Product/channel-1
path; the 2026-07-09 HIL run additionally proved real UVC capture and a
byte-identical full-resolution RF downlink in `58.557 s`.

## Scope

Validated locally:

- `PayloadDriver_Lepton` full-res Lepton product generation using the explicit
  `LEPTON_CAMERA_BACKEND=sample` path and the real Lepton sample grid from
  `ground-station/c3m-lepton-test-data`.
- F Prime Data Product write to `DpCat/Dp_*.fdp`.
- Nonzero source CRC emitted in the science product descriptor before channel-1
  downlink.
- `PayloadDownlinkApp` channel-1 packetization and retry flow.
- Ground receiver reconstruction.
- Lepton viewer decode of `160x120` thermal pixels.

Requires HIL rather than local emulation:

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

It also exports:

```text
LEPTON_CAMERA_BACKEND=sample
```

The resulting local `.fdp` must decode back to that same 120x160 grid. Override
with `--sample-csv <path>` only when intentionally testing a different Lepton
sample. Do not use `sample`, `synthetic`, or `auto` as HIL camera proof.

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

- Ground-to-satellite channel 0 / CCSDS commands: ACK required.
- Satellite-to-ground channel 0 telemetry: ACK not required.
- Channel 1 / payload: ACK not required in either direction.
- App-level repair remains owned by `PayloadDownlinkApp` and
  `payload_receiver.py` through packet indexes, retry requests, and final CRC.

Rollback rule:

- If HIL shows payload CRC failures, queue drops, or unacceptable retry rounds,
  stop optimizing, inspect the directional counters, and increase the relevant
  manifest-driven UART/RF pacing before considering per-packet payload ACK.
  Regenerate transport constants, rebuild both Teensy sketches, and rerun both
  the local gate and HIL acceptance flow after any change.

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
- CMake reports `PayloadDriver_Lepton: libuvc enabled` for real-camera HIL.
- no SSH deploy is attempted from this local-only gate.

If the build reports `PayloadDriver_Lepton: libuvc disabled`, the binary is
still useful for ARM/runtime smoke, but it is not real-camera HIL-ready. Install
or sync `libuvc`, `libuvc/libuvc.h`, and `libusb-1.0` into the Pi build
environment, then rebuild until the `libuvc enabled` line appears.

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
ssh artemis-pi 'systemctl show artemis-fprime.service -p Environment'
```

Expected:

- service is `active`.
- deployment is running with `-d /dev/serial0`.
- service environment includes `LEPTON_CAMERA_BACKEND=uvc`.
- no stale local process owns the GDS data or payload ports.

Before involving F Prime, prove the Lepton camera backend directly on the Pi:

```bash
cd /home/pi/artemis/current
LEPTON_CAMERA_BACKEND=uvc ./testLeptonCamera
```

Minimum pass:

- `open OK, streaming`
- `frame OK` with non-identical min/max values
- exit code `0`

If this fails, stop and fix camera/libuvc access before running the RF demo.

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
python3 -u ArtemisRpiTeensy_N2/tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output-dir "$RUN_DIR" \
  --ext .fdp \
  --idle-timeout 240 \
  --debug
```

`--output-dir` is the receiver's continuous-listen mode; there is no separate
`--continuous` flag. `--ext .fdp` is required so the Lepton viewer command
below finds the reconstructed products. `--idle-timeout 240` exits only after
four minutes with no serial activity; set it to `0` for an operator-stopped
session.

Future HIL quality-of-life plan: wrap this receiver in a local web UI so the
operator can see ground-side packet progress, retry state, final CRC, and the
decoded Lepton PNG next to GDS. See
[`C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md`](C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md).

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

- GDS event `LeptonBackendSelected backend=uvc`.
- `ScienceProductDescriptor` handoff has nonzero source path, byte count, and
  source CRC.
- wall-clock time from `PayloadDownlinkStarted` to `PayloadDownlinkComplete`.
- receiver reports `complete` and exits cleanly when not in continuous mode.
- Lepton viewer parses `width=160`, `height=120`, `pixels=19200`.
- final CRC succeeds.
- `rf_retries`, `rf_ack_timeouts`, `rf_msg_id_gaps`, and queue drops are
  recorded from both Teensy debug ports.
- channel-0 commands remain responsive during channel-1 payload downlink.
- any retry round count is acceptable only if the final file is byte-correct and
  the demo timing remains acceptable.

## Validated Final Bench Configuration (2026-07-09)

The live C3M Lepton/RFM23BP bench passes both gates with the following generated
transport constants:

- Ground and satellite RF antennas were approximately 30 inches apart on the
  tabletop, line-of-sight.
- Pi-to-satellite UART: `115200 8N1` on `/dev/serial0`.
- payload/base Pi inter-frame margin: `37 ms`.
- additional channel-0 inter-frame margin: `40 ms`.
- payload/retry messages per 1 Hz run: `22`.
- payload RF inter-packet gap: `15 ms`.
- ground-to-satellite CCSDS commands: ACKed.
- satellite-to-ground CCSDS telemetry and channel-1 payload: unACKed.
- RFM23BP 125 kbps register `0x58=0xC0` on both radios.

Do not replace the channel-aware Pi pacing with a baud-rate change. Both the
current branch and `EPSCOR_C3M_REFACTOR` use `/dev/serial0` at 115200; the
temporary 57600 diagnostic did not remove bulk CRC errors. The failure was a
flow-control regression: Pi UART frames overlapped the satellite's RF service.
Channel-0 frames need the extra allowance because one 128-byte telemetry frame
uses three RF segments.

Final acceptance evidence:

- Pi release: `/home/pi/artemis/releases/c3m-hil-uartflow37-channel`.
- real UVC `.fdp`: `38480` bytes.
- receiver: `1100/1100`, no retry request.
- application status: `sent=1100 total=1100 error=0`.
- command-to-file time: `58.557 s`.
- source/ground SHA-256:
  `87b61b387647a4e732918b93a071fe51bf64b9b1a55ede6ff30e99289465ac26`.
- viewer: `160x120`, `19200` pixels; PNG opened successfully.
- mid-transfer ping returned in the same second.
- satellite counters: zero CRC, framing, timeout, RF TX, and queue drops.

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
