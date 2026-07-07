# Neutron 2 Local Emulation Runbook

BLUF: use this to manually demonstrate the Neutron 2 demo story on one laptop,
without hardware-in-the-loop.

If the local demo fails, use `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md` to
identify whether the issue is build, GDS/dictionary, mission flow, payload
capture, storage, downlink, or viewer state.

This runbook shows:

- F Prime app running locally
- `fprime-gds` command/event/telemetry view
- channelized Pi UART mux behavior at the host-side boundary
- Base Mode and live SOH telemetry
- operator-scheduled payload collection
- simulated neutron payload CSV generation
- science downlink progress events
- payload viewer opening the latest CSV automatically
- payload viewer refocus/open after verified science downlink completion

## What This Does And Does Not Prove

Validated:

- command uplink through local emulation
- event and telemetry downlink into `fprime-gds`
- Base Mode command path
- SOH snapshot command path
- scheduled collection command path
- simulated Neutron 2 payload capture path
- storage/downlink handoff events
- channel 1 payload downlink manager completion events
- ground-side CSV review in the payload viewer

Not validated:

- physical RPi UART behavior
- Teensy bridge firmware behavior
- RF link behavior
- real payload board behavior
- physical channel 1 payload reconstruction over the RFM23BP path
- real PDU hardware response behavior over satellite Teensy `Serial1`

## Preflight

For the standard one-command local validation, run:

```bash
cd ~/Developer/fprime-artemis-cubesat
./tools/validate_local.sh
```

That script checks generated transport headers, local Python tests, the F Prime
native unified-topology build, component unit tests, and the automated demo
sequence. Use the manual steps below when you need to inspect or operate the
demo interactively.

### macOS

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

### Windows Laptop (WSL2)

```bash
cd ~/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

Expected result:

- build completes without errors
- dictionary exists under `build-artifacts/.../ArtemisRpiTeensyDeployment/dict/`

Topology note:

- `Top/topology.fpp` is the single topology for laptop rehearsal and HIL.
- `./tools/run_neutron2_local_demo.sh` builds that unified topology automatically.
- `./tools/validate_local.sh` is the standard no-HIL regression command before
  handing local changes to mission ops or another student.
- rehearse locally and demo on hardware with the same connection graph.

## Start The Manual Demo

Terminal 1: start the local F Prime app, PTY emulator, and GDS.

macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
export NEUTRON_PAYLOAD_SIM_ROOT="$PWD/../external/payload-neutron-simulation"
./tools/run_local_emulation.sh --gui-port 5050
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
export NEUTRON_PAYLOAD_SIM_ROOT="$PWD/../external/payload-neutron-simulation"
./tools/run_local_emulation.sh --gui-port 5050
```

`run_local_emulation.sh` defaults to `--link-mode channelized`, which unwraps
channel 0 for `fprime-gds`, observes channel 1 payload traffic locally, and keeps
channel 2 satellite-local RPC off the ground stream.

Terminal 2: start the Neutron 2 payload viewer.

macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --capture-dir /tmp/neutron_payload_captures \
  --port 8062
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --capture-dir /tmp/neutron_payload_captures \
  --port 8062
```

Open:

- GDS: `http://127.0.0.1:5050`
- payload viewer: `http://127.0.0.1:8062`

## GDS Setup

In `fprime-gds`, keep these pages ready:

- Commanding
- Events
- Channels or Charts

Useful channels to watch:

- `ArtemisRpiTeensyDeployment.missionManager.CurrentMode`
- `ArtemisRpiTeensyDeployment.missionManager.ModeHeartbeat`
- `ArtemisRpiTeensyDeployment.sohManager.OverallHealth`
- `ArtemisRpiTeensyDeployment.scienceManager.PendingDelaySeconds`
- `ArtemisRpiTeensyDeployment.scienceManager.CollectionCount`
- `ArtemisRpiTeensyDeployment.payloadAdapterNeutronSim.LastRowsCaptured`
- `ArtemisRpiTeensyDeployment.payloadAdapterNeutronSim.LastTotalCounts`
- `ArtemisRpiTeensyDeployment.storageService.StoredProducts`
- `ArtemisRpiTeensyDeployment.commsManager.PendingScienceBytes`

If charts are blank, check that the chart is not paused.

## Manual Demo Script

Use these commands in the GDS Commanding page.

### 1. Base Mode

Command:

```text
ArtemisRpiTeensyDeployment.missionManager.ENTER_BASE_MODE
```

Expected event:

```text
MissionManager.ModeChanged mode=0
```

Expected channel:

```text
missionManager.CurrentMode = 0
```

Lead-facing line:

```text
The spacecraft is in Base Mode and ready for the mock contact window.
```

### 2. SOH Snapshot

Command:

```text
ArtemisRpiTeensyDeployment.sohManager.EMIT_SOH_SNAPSHOT
```

Expected event:

```text
SoHManager.Snapshot
```

Expected behavior:

- SOH channels are visible in GDS
- `ModeHeartbeat` continues moving

Lead-facing line:

```text
We are downlinking health and state-of-health data during the pass.
```

### 3. Configure Payload Capture Duration

Command:

```text
ArtemisRpiTeensyDeployment.scienceManager.CONFIGURE_CAPTURE_DURATION
```

Argument:

```text
durationSeconds = 10
```

Expected event:

```text
ScienceManager.CaptureDurationConfigured durationSeconds=10
```

Lead-facing line:

```text
For the compressed demo, the payload capture will collect ten seconds of simulated neutron data.
```

### 4. Schedule Collection

Command:

```text
ArtemisRpiTeensyDeployment.missionManager.SCHEDULE_COLLECTION
```

Argument:

```text
delaySeconds = 10
```

Expected immediate events:

```text
MissionManager.CollectionScheduled delaySeconds=10
ScienceManager.CollectionTriggered delaySeconds=10
```

Expected channels:

```text
missionManager.CurrentMode = 1
scienceManager.PendingDelaySeconds counts down toward 0
```

Lead-facing line:

```text
The operator schedules science collection after a short delay, standing in for the planned collection timing during contact.
```

### 5. Confirm Collection Completed

Wait about 10 to 15 seconds.

Expected events:

```text
PayloadService.PayloadScienceCaptureRequested
PayloadService.PayloadCollectionForwarded
PayloadAdapter_NeutronSim.CaptureComplete
PayloadService.PayloadStatusUpdated
ScienceManager.ScienceProductReady
StorageService.ScienceStored
```

Expected channels:

```text
scienceManager.PendingDelaySeconds = 0
scienceManager.CollectionCount increments
payloadAdapterNeutronSim.LastRowsCaptured = 10
payloadAdapterNeutronSim.LastProductBytes is nonzero
storageService.StoredProducts increments
commsManager.PendingScienceBytes is nonzero
```

Lead-facing line:

```text
The payload adapter generated a neutron-count science product and handed it into storage.
```

### 6. Report The Latest Dataset

Command:

```text
ArtemisRpiTeensyDeployment.storageService.REPORT_LATEST_DATASET
```

Expected event:

```text
StorageService.LatestDataset
```

Optional command:

```text
ArtemisRpiTeensyDeployment.storageService.REPORT_STORAGE_HISTORY
```

Expected event:

```text
StorageService.StorageHistoryEntry
```

Lead-facing line:

```text
Storage has a science product staged for review and downlink handoff.
```

### 7. Request Science Downlink

Command:

```text
ArtemisRpiTeensyDeployment.commsManager.REQUEST_SCIENCE_DOWNLINK
```

Expected events, in order:

```text
CommsManager.DownlinkRequested
StorageService.DownlinkPrepared
PayloadDownlinkManager.PayloadDownlinkStarted
PayloadDownlinkManager.PayloadDownlinkComplete
CommsManager.DownlinkFinished
```

Lead-facing line:

```text
This shows the current F Prime downlink handoff and completion-driven channel 1 payload transfer path. HIL will validate the same payload path over the Teensy/RFM23BP hardware link.
```

The automated `run_neutron2_local_demo.sh` check waits for these completion
events and verifies that F Prime published the generated capture as
`/tmp/neutron_payload_captures/latest_payload.bin`.

### 8. Review Payload CSV

Switch to the payload viewer:

```text
http://127.0.0.1:8062
```

Expected behavior:

- newest CSV is selected automatically
- metrics update
- chart shows counts over `t_s`
- red bands mark SAA rows
- bottom notes explain fields and cleanup

The files are written under:

```text
/tmp/neutron_payload_captures
```

Lead-facing line:

```text
The ground-side viewer opens the latest downlinked or captured payload product and shows the neutron-count data for review.
```

## One-Command Rehearsal

Use this before the lead demo to verify the whole laptop path quickly. The
script always launches both GDS and the payload viewer; after verified payload
downlink completion it opens/refocuses the viewer for visual inspection.

macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_neutron2_local_demo.sh --delay 3 --capture-seconds 3 --exit-after-sequence
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_neutron2_local_demo.sh --delay 3 --capture-seconds 3 --exit-after-sequence
```

Use this if you want the script to run the sequence and then leave GDS/viewer up:

```bash
./tools/run_neutron2_local_demo.sh
```

## Cleanup

Stop local emulation and viewer with `Ctrl-C` in their terminals.

Payload CSVs are run artifacts. Do not rely on OS temp cleanup. Clean old demo
files when needed:

```bash
rm /tmp/neutron_payload_captures/neutron_capture_*.csv
```

From GDS, simulator cleanup can also be requested with:

```text
ArtemisRpiTeensyDeployment.storageService.REMOVE_OLD_DATASETS
```

Argument:

```text
confirm = 1
```

## Fast Troubleshooting

If GDS opens but telemetry/events are empty:

- confirm GDS is using `space-packet-space-data-link`
- restart with `./tools/run_local_emulation.sh --gui-port 5050`
- kill stale old GDS/emulation processes if a port is stuck

If the payload viewer does not update:

- confirm `/tmp/neutron_payload_captures` has a new `neutron_capture_*.csv`
- refresh the viewer
- restart the viewer process

If `PayloadAdapter_NeutronSim.CaptureFailed` appears:

- confirm `NEUTRON_PAYLOAD_SIM_ROOT` points to:
  `external/payload-neutron-simulation`

If port `5050` is busy:

macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_local_emulation.sh --gui-port 5060
cd ~/Developer/fprime-artemis-cubesat
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --port 8062
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_local_emulation.sh --gui-port 5060
cd ~/fprime-artemis-cubesat
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --port 8062
```

Then open `http://127.0.0.1:5060`.
