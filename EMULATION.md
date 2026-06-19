# Local Closed-Loop Emulation (MacBook)

This is the fastest local end-user test loop for this repo:

- run flight app + channelized local link emulator + `fprime-gds` on one laptop
- watch live-changing telemetry
- send one simple ping command and get a pong event/command response
- run the Neutron 2 simulated payload path and review the captured CSV in the local payload viewer

All transport stays local over pseudo-terminals (`pty`). The emulator defaults
to the current Pi UART channel mux contract:

- channel 0: GDS/CCSDS bytes
- channel 1: payload/science bytes observed by the emulator, not forwarded to GDS
- channel 2: satellite-local RPC observed by the emulator, not forwarded to GDS

This deployment uses `ComCcsds`, so the working GDS framing is:

- `space-packet-space-data-link`

## What this validates

- F' app <-> channelized local bridge <-> `fprime-gds` data path
- Live telemetry downlink
- Command uplink and command response/event path
- The Pi-side UART wrapper boundary used by HIL

## What this does not validate

- physical radios/UART electrical behavior
- real RF conditions
- real hardware timing and power sequencing
- PDU response behavior over the real satellite Teensy UART

## Prerequisites

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

## Topology Profile Toggle

The deployment has two build-time topology profiles:

- `hil` is the default merge-safe profile.
- `local-demo` enables the laptop demo rate-group path for `ScienceManager`,
  `SoHManager`, `PayloadService`, and `StorageService`, while keeping noisy
  subsystem polling off.

Switching profiles requires regenerate + rebuild.

Default/HIL build:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
fprime-util generate -f -DNEUTRON2_TOPOLOGY_PROFILE=hil
fprime-util build
```

Local-demo build:

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
fprime-util generate -f -DNEUTRON2_TOPOLOGY_PROFILE=local-demo
fprime-util build
```

The Neutron 2 demo launcher does this local-demo generate/build automatically
unless `--skip-build` is supplied.

## One-command launch

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_local_emulation.sh
```

Then open:

- `http://127.0.0.1:5050`

Stop all processes with `Ctrl-C` in the launcher terminal.

## One-command Neutron 2 MVP Demo

For a lead-facing manual walkthrough, use:

- `docs/NEUTRON2_LOCAL_EMULATION_RUNBOOK.md`

Use this for the laptop-only demo story before HIL testing. It starts local
emulation, starts the Neutron 2 payload viewer, sends the demo command sequence,
verifies that a new simulated payload CSV was generated and parsed, then
opens/refocuses the payload viewer after payload downlink completion.

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_neutron2_local_demo.sh
```

Open:

- GDS: `http://127.0.0.1:5050`
- Neutron 2 payload viewer: `http://127.0.0.1:8062`

The automated sequence is:

1. `MissionManager.ENTER_BASE_MODE`
2. `SoHManager.EMIT_SOH_SNAPSHOT`
3. `ScienceManager.CONFIGURE_CAPTURE_DURATION(<capture seconds>)`
4. `MissionManager.SCHEDULE_COLLECTION(<delay seconds>)`
5. `StorageService.REPORT_LATEST_DATASET`
6. `StorageService.REPORT_STORAGE_HISTORY`
7. `CommsManager.REQUEST_SCIENCE_DOWNLINK`

Default timing is a 10-second schedule delay and a 10-second simulated capture.
For a quick non-interactive check:

```bash
./tools/run_neutron2_local_demo.sh --delay 3 --capture-seconds 3 --exit-after-sequence
```

The pass condition is intentionally laptop-local:

- GDS accepts the command sequence.
- `PayloadAdapter_NeutronSim` writes a new CSV under `/tmp/neutron_payload_captures`.
- F Prime publishes that CSV as `/tmp/neutron_payload_captures/latest_payload.bin`.
- `PayloadDownlinkManager.PayloadDownlinkComplete` and `CommsManager.DownlinkFinished`
  appear in the run log.
- `ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --summary` parses that CSV.
- The payload viewer is opened/refocused for visual inspection after the verified
  downlink.

In this branch, GDS is the command/event/telemetry surface and the Neutron 2
viewer is the science-data review surface. `REQUEST_SCIENCE_DOWNLINK` starts the
channel 1 payload downlink manager and produces completion events locally. HIL
still needs to validate the physical RFM23BP channel 1 receive path with
`tools/payload_receiver.py`.

## End-user manual test (minimal)

1. Start emulation with `./tools/run_local_emulation.sh`.
2. Open GDS web UI at `http://127.0.0.1:5050`.
3. In telemetry, watch these channels:
   - `MissionManager.ModeHeartbeat`
   - `MissionManager.PingCount`
   - `TeensyTransportService.LinkHeartbeat`
   - `GpsAdapter_Artemis.FixState`
   - `CommsAdapter_TeensyRfm23.LinkState`
   - `CommsAdapter_TeensyRfm23.RfRxPackets`
4. Confirm `ModeHeartbeat` and `LinkHeartbeat` increment continuously.
5. Send command `MissionManager.PING` with a token (example `42`).
6. Confirm:
   - command response is `OK`
   - event `MissionManager.Pong` appears with your token and an incrementing count
   - telemetry `MissionManager.PingCount` increments

If all three checks pass, the local command/telemetry/event loop is working for basic manual demo validation.

## Adapter Telemetry Expectations (GPS + RFM23)

When the adapter model path is active, expect:

- `ArtemisRpiTeensyDeployment.gpsAdapterArtemis.FixState` to move through:
  - acquiring (`1`) early in runtime
  - mostly `3` (3D fix) with occasional `2` (2D) and rare `0` (dropout)
- `ArtemisRpiTeensyDeployment.commsAdapterTeensyRfm23.LinkState` to move through:
  - acquiring (`1`) at startup
  - mostly `2` (locked) with occasional `3` (degraded) and rare `0` (down)
- `ArtemisRpiTeensyDeployment.commsAdapterTeensyRfm23.RfRxPackets` to monotonically increase.
- `ArtemisRpiTeensyDeployment.teensyTransportService.DownlinkFrames` to follow that receive-packet counter.

## Demo Walkthrough: `CollectionScheduled` (10s)

Use this when you want to demonstrate the timed collection story in GDS.

1. Start emulation and open `http://127.0.0.1:5050`.
2. In **Commanding**, send `SCHEDULE_COLLECTION` on `missionManager` (or `ArtemisRpiTeensyDeployment.missionManager`) with argument `10`.
3. Confirm in **Command History**:
   - command response is `OK`
4. Confirm immediate updates in **Events**:
   - `ArtemisRpiTeensyDeployment.missionManager.ModeChanged` (`mode=1`)
   - `ArtemisRpiTeensyDeployment.missionManager.CollectionScheduled` (`delay=10`)
   - `ArtemisRpiTeensyDeployment.scienceManager.CollectionTriggered` (`delay=10`)
5. Confirm immediate updates in **Channels/Charts**:
   - `ArtemisRpiTeensyDeployment.missionManager.CurrentMode` becomes `1`
   - `ArtemisRpiTeensyDeployment.missionManager.LastScheduledDelaySeconds` becomes `10`
   - `ArtemisRpiTeensyDeployment.scienceManager.PendingDelaySeconds` starts at `10`
6. Wait about 10 seconds and confirm collection activity in **Events**:
   - `ArtemisRpiTeensyDeployment.payloadService.PayloadCollectionForwarded`
   - `ArtemisRpiTeensyDeployment.payloadService.PayloadStatusUpdated`
   - `ArtemisRpiTeensyDeployment.scienceManager.ScienceProductReady`
   - `ArtemisRpiTeensyDeployment.storageService.ScienceStored`
7. Confirm post-collection state in **Channels/Charts**:
   - `ArtemisRpiTeensyDeployment.scienceManager.PendingDelaySeconds` reaches `0`
   - `ArtemisRpiTeensyDeployment.scienceManager.CollectionCount` increments
   - `ArtemisRpiTeensyDeployment.storageService.StoredProducts` increments
   - `ArtemisRpiTeensyDeployment.commsManager.PendingScienceBytes` becomes nonzero

Optional follow-on command:

- Send `REQUEST_SCIENCE_DOWNLINK` on `commsManager`
- Expect `ArtemisRpiTeensyDeployment.commsManager.DownlinkRequested`,
  `ArtemisRpiTeensyDeployment.storageService.DownlinkPrepared`,
  `ArtemisRpiTeensyDeployment.payloadDownlinkManager.PayloadDownlinkStarted`,
  `ArtemisRpiTeensyDeployment.payloadDownlinkManager.PayloadDownlinkComplete`, and
  `ArtemisRpiTeensyDeployment.commsManager.DownlinkFinished`
- `DownlinkFinished` is emitted after the payload downlink manager completes, not immediately on request.
- This is a real channel 1 payload transfer path, but it is not a stock GDS `#Downlink` file transfer.
- In local laptop emulation, review the generated science CSV in the Neutron 2 payload viewer at `http://127.0.0.1:8062`. In HIL, run `tools/payload_receiver.py` on the ground channel 1 serial endpoint and review the reconstructed file.

If chart lines do not move, verify the chart is not paused (toggle play/pause in the chart widget).

## Next Step: HIL Payload Downlink

Current `REQUEST_SCIENCE_DOWNLINK` starts the file-backed channel 1 payload
transfer. For HIL, validate the same path through the satellite Teensy,
RFM23BP pair, ground Teensy, and payload receiver tool.

Done criteria:

1. Triggering science downlink causes an actual payload transfer.
2. GDS shows command/event/telemetry progress without corrupting the CCSDS stream.
3. Reconstructed CSV exists on the laptop and matches expected test content.
4. The Neutron 2 payload viewer opens/refocuses on the reconstructed CSV.

## Useful options

Use a different GDS port:

```bash
./tools/run_local_emulation.sh --gui-port 5060
```

Do not auto-launch app (manual app launch):

```bash
./tools/run_local_emulation.sh --no-app
```

Do not auto-launch GDS (manual GDS launch):

```bash
./tools/run_local_emulation.sh --no-gds
```

Adjust uplink burst flush timeout (ms):

```bash
./tools/run_local_emulation.sh --uplink-flush-ms 12
```

Use legacy wrapper/segmentation emulation (optional):

```bash
./tools/run_local_emulation.sh --link-mode legacy-wrapper
```

## Manual launch mode

When using `--no-app` and/or `--no-gds`, the emulator prints:

- `app UART device: /dev/ttys...`
- `gds UART device: /dev/ttys...`

Run app manually:

```bash
./build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment -d <app_uart_device>
```

Run GDS manually:

```bash
fprime-gds -n \
  --dictionary build-artifacts/Darwin/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json \
  --communication-selection uart \
  --uart-device <gds_uart_device> \
  --uart-baud 115200 \
  --uart-skip-port-check \
  --framing-selection space-packet-space-data-link \
  --gui-port 5050
```

## Emulation files

- `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/tools/run_local_emulation.sh`
- `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/tools/local_emulation_loop.py`
- `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/tools/run_neutron2_local_demo.sh`
- `/Users/sozodennis/Developer/fprime-artemis-cubesat/ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py`
