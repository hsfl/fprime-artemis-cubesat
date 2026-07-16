# Software Debugging Troubleshooting Guide

BLUF: debug one layer at a time. Prove the command path, then the mission
component path, then the transport path, then the viewer/file path. Do not
reflash or refactor until the failing layer is identified.

Use this guide when the Neutron 2 flow does not behave as expected. It is the
software triage map for:

- `docs/MISSION_OPS_QUICK_RUN.md`
- `docs/NEUTRON2_LOCAL_EMULATION_RUNBOOK.md`
- `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`

## First Split

Run the local gate first when hardware is not available:

```bash
cd ~/Developer/fprime-artemis-cubesat
./tools/validate_local.sh
```

If this fails, fix software before blaming the bench.

If this passes but HIL fails, the likely issue is one of:

- selected serial port
- stale dictionary or a binary built before the latest topology change
- Pi deployment/service state
- satellite Teensy firmware state
- RF link or debug counters
- payload receiver/viewer file path
- EPS/PDU channel 2 path

## Layer Map

| Layer | Owns | First proof | Look here |
|---|---|---|---|
| repo/build | generated headers, F Prime build, unit tests | `./tools/validate_local.sh` passes | `tools/validate_local.sh`, `config/transport_constants.json` |
| GDS/session | dictionary, command send, events/channels UI | `missionApp.PING` returns `Pong` | `ArtemisRpiTeensy_N2/tools/run_gds_uart.sh`, GDS Events tab |
| F Prime mission | mode and scheduled collection | `ModeChanged`, `CollectionScheduled` | `MissionApp`, `ScienceApp` |
| payload manager | capture request and driver handoff | `PayloadScienceCaptureRequested`, `CaptureComplete` | `PayloadManager`, `PayloadDriver_NeutronSim` |
| storage | latest science product | `ScienceStored`, `LatestDataset` | `StorageManager` |
| comms/downlink | science downlink request and completion | `DownlinkRequested`, `PayloadDownlinkComplete` | `CommsApp`, `PayloadDownlinkApp` |
| UART mux | Pi to satellite Teensy virtual channels | `FramesTx`/`FramesRx` move, `FrameDrops` stays low | `UartChannelMux`, generated `LinkCfg.hpp` |
| RF bridge | channel 0/1 RF movement | Teensy `#LINK_STATUS` counters move | `relay_uart_rf.*`, RF debug serial |
| EPS/PDU | satellite-local channel 2 RPC | `PduRequestQueued`, then handled or timed out | `EpsManager`, `EpsDriver_Artemis`, `pdu_proxy.cpp` |
| payload receiver/viewer | channel 1 reconstruction and display | receiver prints `complete:`, viewer summary parses | `tools/payload_receiver.py`, `ground-station/neutron2-payload-viewer/` |

## Golden Event Ladder

For the MVP demo story, expect this order in GDS Events:

```text
ModeChanged
Snapshot
CaptureDurationConfigured
CollectionScheduled
CollectionTriggered
PayloadScienceCaptureRequested
CaptureComplete
PayloadStatusUpdated
ScienceProductReady
ScienceStored
LatestDataset
DownlinkPrepared
DownlinkRequested
PayloadDownlinkStarted
PayloadDownlinkComplete
DownlinkFinished
```

If the ladder stops, debug the component after the last event that appeared.

## Common Symptoms

### Build Fails After UART Or RF Edits

Check generated constants first:

```bash
python3 tools/generate_transport_constants.py --check
python3 tools/check_transport_constants.py
```

If either fails, edit `config/transport_constants.json`, regenerate, then build:

```bash
python3 tools/generate_transport_constants.py
./tools/validate_local.sh --skip-demo
```

Do not hand-edit these generated headers:

- `ArtemisRpiTeensy_N2/Components/LinkCfg/LinkCfg.hpp`
- `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp`
- `GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp`

### GDS Opens But Commands Do Not Work

First prove the smallest command path:

```bash
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
DICT="$(find build-artifacts -name '*TopologyDictionary.json' | sort | tail -n 1)"

fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.PING \
  --arguments 4245 \
  --dictionary "$DICT" \
  --log-level-gds ERROR
```

Expected GDS event:

```text
Pong
```

If GDS accepts the command but no `Pong` appears:

- confirm the dictionary matches the running build
- confirm the right serial port is used
- confirm stale `fprime-gds` processes are not holding the port
- in HIL, check Pi service logs before reflashing Teensy

If the GDS page still loads but ground debug `uart_rx` and `rf_tx_pkt` stay
flat after a command, the web UI is alive but the command/UART backend is not.
This can happen when a local-emulation GDS reused the global
`/tmp/fprime-server-in` and `-out` IPC endpoints while the hardware GDS was
already running. `Cmd-Shift-R` refreshes only the browser. Restart the complete
hardware GDS process tree and retry one PING; do not reflash either Teensy for
this symptom.

Pi service check:

```bash
ssh artemis-pi 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment || true'
ssh artemis-pi 'journalctl -u artemis-fprime.service --since "2 minutes ago" --no-pager | tail -100'
```

### Radio Is OFF Or Keeps Recovering

Use GDS `commsApp.REQUEST_LINK_STATUS` or `commsApp.PING_LINK_RSSI` before
restarting processes or reflashing firmware. Inspect the `RadioStatusUpdated`,
`RadioRecoveryScheduled`, and `RadioRecovered` events plus these fields:

- `RadioState`: `OFF` or `READY`
- `RadioFault`: `NONE`, `INIT_FAILED`, `WATCHDOG_RESET`, or `LOCAL_TX_FAULT`
- `RadioRpcResult`: `OK`, target/protocol error, timeout, or bad response
- `RadioRetrySeconds`: time until the next bounded F Prime attempt

`OFF + NONE` is a safe boot/not-yet-enabled condition. `OFF + INIT_FAILED`,
`WATCHDOG_RESET`, or `LOCAL_TX_FAULT` is a factual local failure. An RPC
`TIMEOUT` or `BAD_RESPONSE` is also local recovery evidence. F Prime retries
after `30 s`, then `120 s`, then every `900 s`; do not restart GDS/Pi or spam
enable/status commands during that planned backoff.

The relay performs one bounded low-level recovery retry before a terminal local
TX failure forces SDN shutdown. Peer silence, an absent ground station, RSSI
changes, and ordinary ACK timeouts are not proof of a local chip wedge and do
not trigger this power-cycle path. If channel 2 remains responsive, let F Prime
restore the radio. Escalate only if the retry deadline passes without a new
attempt/event, the Pi service is down, or channel 2 itself is unavailable.

### Local Demo Fails

Run the focused local command and inspect its log path:

```bash
cd ArtemisRpiTeensy_N2
./tools/run_neutron2_local_demo.sh --skip-build --exit-after-sequence
```

Look for:

- `PASS: local Neutron 2 MVP demo sequence produced and parsed a science CSV`
- a new capture under `/tmp/neutron_payload_captures`
- a run log under `ArtemisRpiTeensy_N2/tools/logs/neutron2_local_demo_*`

If the capture exists but the viewer fails, skip F Prime and check the file:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --summary /tmp/neutron_payload_captures/latest_payload.bin
```

### Collection Command Works But No Science Product Appears

Find the last event in the ladder:

```bash
rg -n "CollectionScheduled|CollectionTriggered|PayloadScienceCaptureRequested|CaptureComplete|CaptureFailed|ScienceProductReady|ScienceStored|LatestDataset" \
  ArtemisRpiTeensy_N2/logs -g 'event.log'
```

Interpretation:

- stops at `CollectionScheduled`: check `MissionApp` to `ScienceApp`
- stops at `CollectionTriggered`: check `ScienceApp` to `PayloadManager`
- stops at `PayloadScienceCaptureRequested`: check `PayloadDriver_NeutronSim`
- shows `CaptureFailed`: inspect the simulator script and capture directory
- shows `ScienceProductReady` but not `ScienceStored`: check `StorageManager`

For local simulated payload files:

```bash
ls -lh /tmp/neutron_payload_captures
ls -l /tmp/neutron_payload_captures/latest_payload.bin 2>/dev/null || true
python3 -c 'from pathlib import Path; print(Path("/tmp/neutron_payload_captures/latest_payload.bin").resolve())'
head /tmp/neutron_payload_captures/latest_payload.bin
```

Expected first line for neutron CSV payloads:

```text
t_s,counts,flag
```

### Downlink Starts But Payload Receiver Is Incomplete

Payload downlink is channel 1. Start the receiver before requesting downlink:

```bash
python -u ArtemisRpiTeensy_N2/tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output "$RUN_DIR/payload.bin" \
  --timeout 180
```

Receiver output meaning:

- `progress:` means packets are arriving
- `retry:` means the receiver requested missing packets
- `complete:` is pass
- `incomplete:` means channel 1 transport did not finish
- `crc mismatch:` means reconstructed bytes are not the advertised product

If receiver is incomplete:

- rerun the receiver before retrying downlink
- increase timeout, for example `--timeout 240`
- check the receiver GUI plus `ProgressPercent` telemetry; use
  `GET_PAYLOAD_STATUS` for an explicit fallback, and check
  `PayloadRetryRequested` and `PayloadDownlinkFailed` in GDS Events
- check ground and satellite RF counters before reflashing

Manual status command:

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.payloadDownlinkApp.GET_PAYLOAD_STATUS \
  --dictionary "$DICT" \
  --log-level-gds ERROR
```

### Viewer Opens But Shows The Wrong Or Invalid File

The viewer does not reconstruct payloads. It only displays a file already
created by the payload receiver or local simulator.

Force an explicit file:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --file "$RUN_DIR/payload.bin" \
  --port 8062
```

Headless summary:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --summary "$RUN_DIR/payload.bin"
```

If summary fails, inspect file bytes:

```bash
ls -lh "$RUN_DIR/payload.bin"
head "$RUN_DIR/payload.bin"
```

### APID Sequence Warnings Appear

Warnings like this:

```text
Unexpected sequence count received. Packets may have been dropped.
```

mean the channel 0 GDS stream was lossy. They do not automatically mean the
payload failed.

Payload proof is separate:

- receiver prints `complete:`
- reconstructed file is nonzero
- viewer summary parses
- local hash matches the Pi latest payload when HIL has a Pi source file

### EPS/PDU Commands Fail Or Timeout

EPS/PDU MVP behavior intentionally crosses the `EpsManager` and
`EpsDriver_Artemis` boundary while the new PDU is being tested through F Prime.

Debug in this order:

1. `EpsManager` command accepted or rejected
2. `EpsDriver_Artemis` queued request
3. channel 2 local RPC frame sent by `UartChannelMux`
4. satellite Teensy local router received the channel 2 payload
5. `pdu_proxy.cpp` wrote a PDU v2 frame to the PDU UART
6. PDU response returned before timeout

Useful GDS events/channels:

- `EpsCommandRejected`
- `EpsStatusUpdated`
- `PduRequestQueued`
- `PduRequestHandled`
- `PduRequestFailed`
- `PduRequestTimedOut`
- `EpsManager.DriverLinkState`
- `EpsDriver_Artemis.TransportFailureCount`
- `EpsDriver_Artemis.PendingRequestTicks`

If channel 2 fails, treat it as satellite-local EPS/PDU driver work first. It
is not forwarded over RF to the ground Teensy.

## Teensy RF Debug Counters

Use the debug serial ports to request link status:

```text
#LINK_STATUS
```

Important counters:

- `uart_rx`, `uart_tx`: USB/UART bytes moved
- `rf_rx_pkt`, `rf_tx_pkt`: RF packets moved
- `rf_rx_msg`, `rf_tx_msg`: complete RF messages moved
- `crc_drops`, `framing_drops`: bad local frames
- `rf_reasm_timeouts`, `rf_reasm_drops`: incomplete RF messages
- `rf_tx_drops`, `rf_retries`, `rf_ack_timeouts`: RF send or ACK trouble
- `rf_tx_terminal_failures`: bounded local TX recovery was exhausted and the satellite radio was forced safely `OFF`
- `state`, `fault`, `init_attempts`: local `OFF`/`READY` truth, last factual fault, and SDN/POR enable attempts
- `up_q_drops`, `down_q_drops`: firmware queue pressure

Common interpretations:

| Pattern | Likely layer |
|---|---|
| `uart_rx` does not move on ground debug | laptop/GDS port selection |
| ground `rf_tx_pkt` moves, satellite `rf_rx_pkt` does not | RF path, power, frequency, antenna, or board state |
| satellite `rf_rx_pkt` moves, Pi sees no command | satellite Teensy to Pi UART or channel frame |
| Pi events show downlink but ground `rf_rx_pkt` does not move | satellite-to-ground RF downlink |
| payload receiver has no `progress:` but GDS events show downlink | channel 1 receiver port or ground triple-serial mapping |
| `framing_drops` or `crc_drops` rises quickly | generated constants, wrong firmware, or bad byte stream |

## Stop Rules

Stop and gather evidence before:

- repeated Teensy uploads that require manual PROGRAM presses
- reflashing when counters do not point to stale firmware
- changing generated transport headers directly
- claiming HIL/demo readiness from local-only validation
- treating GDS `recv.bin` as payload proof

`ArtemisRpiTeensy_N2/logs/.../recv.bin` is channel 0 GDS traffic. The science
payload proof is the output file from `tools/payload_receiver.py` or the local
simulator capture.

If the Pi reports `PayloadDownlinkComplete` but `payload_receiver.py` reports
`received=0 total=0 missing=0`, the payload was not proven. Check channel-1
Teensy counters:

- satellite `payload_uart_rx` and `payload_rf_tx_msg`
- ground `payload_rf_rx_msg` and `payload_uart_tx`

All four must advance during a real channel-1 downlink.
