# Local Closed-Loop Emulation (MacBook)

This is the fastest local end-user test loop for this repo:

- run flight app + local link emulator + `fprime-gds` on one laptop
- watch live-changing telemetry
- send one simple ping command and get a pong event/command response

All transport stays local over pseudo-terminals (`pty`).

This deployment uses `ComCcsds`, so the working GDS framing is:

- `space-packet-space-data-link`

## What this validates

- F' app <-> local byte bridge <-> `fprime-gds` data path
- Live telemetry downlink
- Command uplink and command response/event path

## What this does not validate

- physical radios/UART electrical behavior
- real RF conditions
- real hardware timing and power sequencing

## Prerequisites

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

## One-command launch

```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_local_emulation.sh
```

Then open:

- `http://127.0.0.1:5050`

Stop all processes with `Ctrl-C` in the launcher terminal.

## End-user manual test (minimal)

1. Start emulation with `./tools/run_local_emulation.sh`.
2. Open GDS web UI at `http://127.0.0.1:5050`.
3. In telemetry, watch these channels:
   - `MissionManager.ModeHeartbeat`
   - `MissionManager.PingCount`
   - `TeensyTransportService.LinkHeartbeat`
4. Confirm `ModeHeartbeat` and `LinkHeartbeat` increment continuously.
5. Send command `MissionManager.PING` with a token (example `42`).
6. Confirm:
   - command response is `OK`
   - event `MissionManager.Pong` appears with your token and an incrementing count
   - telemetry `MissionManager.PingCount` increments

If all three checks pass, the local command/telemetry/event loop is working for basic manual demo validation.

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
- Expect `ArtemisRpiTeensyDeployment.commsManager.DownlinkRequested` and `ArtemisRpiTeensyDeployment.storageService.DownlinkPrepared`
- Current behavior is handshake-only (events/channels), not a real file transfer in GDS `#Downlink`.

If chart lines do not move, verify the chart is not paused (toggle play/pause in the chart widget).

## Next Step: Real File Downlink Path

Current `REQUEST_SCIENCE_DOWNLINK` validates command/event flow only. To finish end-user downlink UX in `http://127.0.0.1:5050/#Downlink`, implement and wire a real `Svc::FileDownlink` transfer path.

Done criteria:

1. Triggering science downlink causes an actual file transfer session.
2. GDS `#Downlink` shows active/progress/completed file entries.
3. Downloaded file exists on the laptop and matches expected test content.

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
