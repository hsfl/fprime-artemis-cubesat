---
name: fprime-local-emulation
description: "Use when running, validating, or debugging the Neutron 2 demo story on a single laptop WITHOUT hardware: local F Prime emulation, the local-demo topology profile, run_local_emulation.sh, run_neutron2_local_demo.sh, validate_local.sh regression checks, simulated neutron payload capture, and the payload viewer. Use for no-HIL rehearsal, student laptop demos, and pre-handoff regression before HIL testing."
---

# F Prime Local Emulation (No-HIL Demo)

## Operating Style

This is the no-hardware path. It proves the F Prime command/telemetry/science
pipeline on one laptop; it does NOT prove RPi UART, Teensy firmware, RF link, or
real payload behavior. Say plainly which side of that line a result falls on.

Source of truth: `docs/NEUTRON2_LOCAL_EMULATION_RUNBOOK.md`. Debug failures
with `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`.

## Platforms

- **macOS** (primary): runs natively; typical repo root `~/Developer/fprime-artemis-cubesat`.
- **Windows**: run everything inside **WSL2** (typical repo root
  `~/fprime-artemis-cubesat`); the browser for GDS (`:5050`) and the viewer
  (`:8062`) can be native Windows. Setup: `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md`.
- **Linux**: runs natively.
- No serial hardware is involved, so no `sudo` is needed on any platform — a
  local-emulation command demanding `sudo` is a sign something else is wrong
  (e.g. `/tmp` capture dir owned by root from an earlier sudo run; fix
  ownership instead).

## One-Command Paths (prefer these)

Standard laptop regression before handing changes to mission ops or another
student:

```bash
cd <repo-root>
./tools/validate_local.sh          # headers + Python tests + local-demo build + component UTs + automated demo
./tools/validate_local.sh --skip-demo   # faster; skips the automated demo sequence
```

Automated demo rehearsal (builds local-demo, launches GDS + viewer, runs the
full sequence, verifies the science CSV):

```bash
cd ArtemisRpiTeensy_N2
./tools/run_neutron2_local_demo.sh --delay 3 --capture-seconds 3 --exit-after-sequence
./tools/run_neutron2_local_demo.sh     # same, but leaves GDS/viewer running
```

## Topology Profiles — Critical

- `hil` is the default, merge-safe profile.
- `local-demo` enables the laptop rate-group path; required for local emulation.
- Manual build: `fprime-util generate -f -DNEUTRON2_TOPOLOGY_PROFILE=local-demo && fprime-util build`
- **After local demo work, rebuild with `-DNEUTRON2_TOPOLOGY_PROFILE=hil`
  before treating the binary as the HIL/default image.** Do not hand a
  local-demo binary to the HIL bench.

## Manual Interactive Demo

Terminal 1 — app + PTY emulator + GDS (from `ArtemisRpiTeensy_N2`):

```bash
. fprime-venv/bin/activate
export NEUTRON_PAYLOAD_SIM_ROOT="$PWD/../external/payload-neutron-simulation"
./tools/run_local_emulation.sh --gui-port 5050
```

Default `--link-mode channelized`: channel 0 unwrapped for GDS, channel 1
payload observed locally, channel 2 satellite-local RPC kept off the ground
stream.

Terminal 2 — payload viewer (from repo root):

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --capture-dir /tmp/neutron_payload_captures --port 8062
```

Open GDS at `http://127.0.0.1:5050`, viewer at `http://127.0.0.1:8062`.

## Demo Story Command Sequence (GDS Commanding page)

1. `missionManager.ENTER_BASE_MODE` → event `ModeChanged mode=0`, channel `CurrentMode = 0`
2. `sohManager.EMIT_SOH_SNAPSHOT` → event `SoHManager.Snapshot`; `ModeHeartbeat` keeps moving
3. `scienceManager.CONFIGURE_CAPTURE_DURATION` (durationSeconds=10) → `CaptureDurationConfigured`
4. `missionManager.SCHEDULE_COLLECTION` (delaySeconds=10) → `CollectionScheduled`, `CollectionTriggered`; `PendingDelaySeconds` counts down
5. Wait 10–15 s → events `PayloadScienceCaptureRequested` … `CaptureComplete` … `ScienceProductReady`, `ScienceStored`; channels `CollectionCount` increments, `LastRowsCaptured`/`LastProductBytes` nonzero, `PendingScienceBytes` nonzero
6. `storageService.REPORT_LATEST_DATASET` → `LatestDataset`
7. `commsManager.REQUEST_SCIENCE_DOWNLINK` → in order: `DownlinkRequested`, `DownlinkPrepared`, `PayloadDownlinkStarted`, `PayloadDownlinkComplete`, `DownlinkFinished`
8. Viewer at `:8062` auto-selects the newest CSV; chart shows counts over `t_s`, red bands mark SAA rows

Full command names are prefixed `ArtemisRpiTeensyDeployment.`. Useful watch
channels are listed in `docs/NEUTRON2_LOCAL_EMULATION_RUNBOOK.md` (GDS Setup).

## Pass Criteria

Do not call a local run good on "commands were sent". Require:

```text
GDS shows live events and telemetry
full downlink event chain including PayloadDownlinkComplete and DownlinkFinished
new neutron_capture_*.csv exists under /tmp/neutron_payload_captures
viewer parses it and renders the counts chart
```

## Fast Troubleshooting

| Symptom | Check |
|---------|-------|
| GDS opens, no events/telemetry | framing must be `space-packet-space-data-link`; kill stale GDS/emulation processes; restart `run_local_emulation.sh` |
| `PayloadAdapter_NeutronSim.CaptureFailed` | `NEUTRON_PAYLOAD_SIM_ROOT` must point at `external/payload-neutron-simulation` |
| Viewer not updating | confirm a new CSV exists in `/tmp/neutron_payload_captures`, then refresh/restart the viewer |
| Port 5050 busy | rerun with `--gui-port 5060` and open that port instead |
| Build fails after fpp edits | `fprime-util generate -f -DNEUTRON2_TOPOLOGY_PROFILE=local-demo` (stale cache) |

## Cleanup

`Ctrl-C` both terminals. Payload CSVs are run artifacts — clean with
`rm /tmp/neutron_payload_captures/neutron_capture_*.csv` or the GDS command
`storageService.REMOVE_OLD_DATASETS` (confirm=1).
