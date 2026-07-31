# EPSCoR C3M Lepton Local And RF Mission Operations Runbook

## START HERE: Manual HIL Operator Script

Use this section when you have the C3M satellite and ground hardware with you
and want to run the real RF demo manually from a Mac. You need:

- both nodes powered with antennas attached.
- the ground Teensy connected to the Mac by USB.
- the satellite Pi and Teensy powered and connected together.
- the Mac able to reach `artemis-pi-c3m` over SSH.

If `ssh artemis-pi-c3m` cannot reach the Pi, fix the network first. The demo
cannot be operated remotely through SSH alone: the ground RF node must be with
the operator and connected to the Mac.

### 1. Find the three ground Teensy ports

```bash
cd ~/Developer/fprime-artemis-cubesat
GROUND_PORTS=(/dev/cu.usbmodem*(N))
printf 'GDS:     %s\nDebug:   %s\nPayload: %s\n' \
  "${GROUND_PORTS[1]}" "${GROUND_PORTS[2]}" "${GROUND_PORTS[3]}"
```

Continue only when this prints three ground Teensy ports with the same number
stem:

```text
first port  = GDS
second port = debug
third port  = payload receiver
```

Do not use a satellite Teensy port if one is separately connected to the Mac.

### 2. Confirm the satellite Pi is running F Prime

```bash
ssh artemis-pi-c3m 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
```

Continue only when the service prints `active` and the deployment is running
with `-d /dev/serial0`.

### 3. Terminal 1: start GDS

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
GROUND_PORTS=(/dev/cu.usbmodem*(N))
export GDS_DATA_PORT="${GROUND_PORTS[1]}"
./tools/run_gds_uart.sh \
  --port "$GDS_DATA_PORT" \
  --baud 115200 \
  --gui-port 5050 \
  --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Leave this terminal running. Open the GDS URL printed by the launcher, normally
`http://127.0.0.1:5050`.

### 4. Terminal 2: start the payload receiver

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py
```

Leave this terminal running. Open `http://127.0.0.1:8064` and wait for green
**Ready - awaiting downlink**. If asked for a port, select the **third** ground
Teensy port from step 1.

### 5. Run one picture from the GDS Commanding page

Send these commands in order:

1. `missionApp.ENTER_BASE_MODE`
2. `sohApp.EMIT_SOH_SNAPSHOT`
3. `missionApp.SCHEDULE_COLLECTION` with `delaySeconds = 10`
4. Wait for a new `storageManager.ScienceStored` event with a nonzero size.
5. `commsApp.REQUEST_SCIENCE_DOWNLINK`
6. Wait for the payload web app to show `Complete` with a passing CRC and the
   decoded `160x120` image.

During the transfer, send only one optional channel-0 proof command:
`missionApp.PING` with token `37002`. Otherwise leave GDS alone until the image
finishes.

To take another picture, repeat steps 3-6. Do not restart GDS, the payload web
app, the Pi, or either Teensy between pictures.

### 6. Stop when finished

Press `Ctrl-C` once in Terminal 2, then once in Terminal 1. Completed payloads
remain under repo-root `data/`.

Everything below is validation, recovery, engineering fallback, and historical
evidence. You do not need it for a normal manual HIL run.

BLUF: use this for the C3M Lepton laptop proof and refined RF demo operation.
The normal operator uses `fprime-gds` for channel 0 and the C3M payload receiver
web app for channel 1; the raw receiver and decoder CLIs are engineering
fallbacks. The 2026-07-09 HIL run proved real UVC capture and a byte-identical
full-resolution RF downlink in `58.557 s`. The July 16 close-range hardening run
passed 10/10 real radio `OFF` to `READY` cycles and 3/3 new byte-identical
payload cycles in 65–67 seconds; mid-transfer PING responded immediately in 2/3
runs and the one missed response passed on the immediate idle retry. The final
July 16 acceptance also proved ground-only cancel with a saved partial, followed
by a byte-identical retry of the same retained spacecraft picture without a new
capture.

The active hardening scope and live HIL matrix are defined in
[`C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md`](C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md).
The implemented Pi-owned radio lifecycle and recovery contract is defined in
[`C3M_RFM23BP_KISS_CONTROL_PLAN_2026-07-16.md`](C3M_RFM23BP_KISS_CONTROL_PLAN_2026-07-16.md).
Do not change transport constants during a timed HIL run. The July 9 validated
baseline below used `22` payload/retry messages per tick; the current July 15
hardening artifacts deliberately use `18`. Treat the July 9 section as
historical evidence, not the active generated contract.

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
- the C3M demo sends one-time mission/SOH commands, then runs three scheduled
  capture/downlink cycles without restarting the app, GDS, or receiver.
- each cycle produces a new satellite `Dp_*.fdp` and a separate
  receiver-generated ground `.fdp`.
- each cycle includes `PayloadDownlinkComplete` and `DownlinkFinished`.
- each ground-copy viewer summary reports `width=160`, `height=120`, and
  `pixels=19200`.
- a decoded Lepton PNG is written for each cycle under the run log directory.
- the decoded local-demo CSV matches
  `ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv`, ignoring
  only capture-time metadata.
- the script prints:
  `PASS: completed 3 consecutive C3M capture/downlink/decode cycles from ground-received artifacts`.

## Manual Local Demo

Use this when debugging a failure from the umbrella gate:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
./tools/run_c3m_local_demo.sh --delay 10 --captures 3 --exit-after-sequence
```

To exercise the actual N2 repair path at home, run one focused cycle with a
single deterministic DATA loss. The emulator drops only the first copy, so the
repair retransmission can pass:

```bash
./tools/run_c3m_local_demo.sh \
  --delay 2 --captures 1 --drop-payload-data-index 100 \
  --exit-after-sequence --no-open
```

To prove receiver checkpoint/resume across a real process replacement:

```bash
./tools/run_c3m_local_demo.sh \
  --delay 2 --captures 1 --restart-receiver-cycle 1 \
  --exit-after-sequence --no-open
```

To prove that an unrecoverable transfer terminates honestly and cannot poison
the next picture, run two cycles. Cycle 1 permanently loses DATA index 100 and
saves `.fdp.partial` plus `.missing.json`; cycle 2 must complete and decode:

```bash
./tools/run_c3m_local_demo.sh \
  --delay 2 --captures 2 --abandon-first-cycle \
  --exit-after-sequence --no-open
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

## Validated Transport Freeze

Local emulation verifies that the generated transport configuration and generic
C3M dataflow remain intact:

- generated payload pacing constants.
- generated per-channel RF ACK policy constants.
- channel-1 payload transfer shape.
- Lepton `.fdp` reconstruction and decode.

Local emulation cannot prove RF throughput. The 2026-07-09 HIL values are frozen
for this mission-operations work:

Current optimized RF policy:

- Ground-to-satellite channel 0 / CCSDS commands: ACK required.
- Satellite-to-ground channel 0 telemetry: ACK not required.
- Channel 1 / payload: ACK not required in either direction.
- App-level repair remains owned by `PayloadDownlinkApp` and
  `payload_receiver.py` through packet indexes, retry requests, and final CRC.

Do not adjust pacing, packet counts, UART values, ACK policy, RF gaps, or PHY
values to compensate for a failed rehearsal. Preserve the run evidence and
inspect the directional counters first. Any future transport-value change is a
separate bench campaign that invalidates the validated Pi release and both
Teensy firmware images until the local gate, rebuilds, and HIL acceptance flow
pass again.

### Transport Source-To-Artifact Map

`config/transport_constants.json` is the only editable source for generated
transport values. `tools/generate_transport_constants.py` renders three
headers; never hand-edit those headers.

| Manifest key group | Generated consumers | Runtime effect |
| --- | --- | --- |
| `frame.magic_*`, `frame.max_payload`, `frame.uart_baud`, `frame.inter_frame_margin_us`, `frame.ccsds_extra_margin_us` | F Prime `Components/LinkCfg/LinkCfg.hpp`; satellite and ground `link_protocol.hpp` | Pi UART framing and channel-aware drain pacing; both Teensy UART parsers |
| `frame.timeout_ms` | satellite and ground `link_protocol.hpp` | Teensy partial-frame timeout |
| `channels.*` | F Prime `LinkCfg.hpp`; satellite and ground `link_protocol.hpp` as applicable | virtual-channel identity and bounds |
| `rf.packet_max_len`, `rf.segment_header_len`, `rf.inter_segment_gap_ms`, `rf.payload_inter_packet_gap_ms` | F Prime `LinkCfg.hpp`; both Teensy `link_protocol.hpp` | RF segmentation size and send pacing |
| `rf.segment_magic_*`, `rf.ack_segment_index`, `rf.reassembly_timeout_ms`, `rf.ack_retries`, `rf.ack_timeout_ms` | both Teensy `link_protocol.hpp` | RF framing, reassembly, and link-layer ACK mechanics |
| `rf.ack_directions.*` | F Prime `LinkCfg.hpp`; direction-specific TX/RX policy in both Teensy `link_protocol.hpp` files | per-direction, per-channel ACK policy |
| `payload.packet_data_bytes`, `payload.magic_*` | F Prime `LinkCfg.hpp`; Python `payload_receiver.py` is drift-checked against these values | channel-1 payload packet shape and receiver identity |
| `payload.packets_per_run`, `payload.retry_packets_per_run` | F Prime `LinkCfg.hpp`; both Teensy `link_protocol.hpp` files | 1 Hz application burst limits |
| `teensy_rpc.*` | F Prime `LinkCfg.hpp`; satellite `link_protocol.hpp` | channel-2 local RPC identifiers/status values |
| `command.*` | both Teensy `link_protocol.hpp` files | debug/control command parser strings and limits |

Generated destinations:

```text
ArtemisRpiTeensy_N2/Components/LinkCfg/LinkCfg.hpp
ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp
GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp
```

The RFM23BP PHY register profile, including the validated 125 kbps
`0x58=0xC0` setting, still lives in the two firmware driver implementations and
is not generated from this manifest. Do not relocate or retune it during the
mission-operations pass.

After any approved manifest or PHY change: regenerate, run
`./tools/validate_local.sh --demo c3m`, rebuild the ARMv6/libuvc Pi release,
rebuild and flash both Teensies, then repeat live HIL acceptance. Until all of
those pass, the changed artifact set is not the validated demo configuration.

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

Web-app-only changes do not require a Teensy rebuild or reflash. Never reflash a
validated board merely to exercise the ground web UI.

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

### Ground Teensy USB Recovery Only If Missing

Do not reflash during normal demo setup. If the ground triple-serial device is
absent, press the physical PROGRAM button once, then confirm the physical upload
ID before doing anything else:

```bash
arduino-cli board list
```

The current HIL identities are ground `usb:100000` and satellite
`usb:2100000`. Never upload through `/dev/cu.usbmodem*` while both boards are
attached; auto-search can flash the satellite and still report success.

Only when `usb:100000` is visible and recovery is actually required:

```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000
```

On macOS, retry once if the first upload only launched `teensy.app`. After
upload, re-enumerate all three ground serial ports and read the second port long
enough to see `[GDS_Teensy]` counters. Triple-serial enumeration alone is not
proof that the correct firmware is running. Stop rather than trying another
upload ID if `usb:100000` does not appear.

Pi service preflight:

```bash
ssh artemis-pi-c3m 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
ssh artemis-pi-c3m 'systemctl show artemis-fprime.service -p Environment'
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

### Radio recovery preflight

In GDS, send `commsApp.REQUEST_LINK_STATUS` or `commsApp.PING_LINK_RSSI`.
The nominal result is a `RadioStatusUpdated` event showing `READY`, `NONE`, and
`OK`, with telemetry:

- `DesiredRadioEnabled = 1`
- `RadioStatusKnown = 1`
- `RadioState = READY`
- `RadioFault = NONE`
- `RadioRpcResult = OK`
- `RadioRetrySeconds = 0`

If the radio reports `OFF`, do not spam commands, restart GDS/Pi, or immediately
reflash the Teensy. Watch for `RadioRecoveryScheduled` and `RadioRecovered`.
F Prime retries after `30 s`, then `120 s`, then at a capped `900 s` cadence.
A factual terminal local TX failure causes the Teensy to assert SDN and report
`OFF + LOCAL_TX_FAULT`; F Prime then performs the same known-good enable path.
Ordinary peer/ACK loss does not power-cycle the radio.

At the deliberately saturated 30 dBm close bench, a command response may be
missed during bulk downlink. Wait for the transfer to become idle and retry the
single command once; do not send a burst of duplicate commands.

## Start Mission Operations Tools

The normal demo operator needs two surfaces: GDS and the payload web app. Start
both before scheduling a capture.

### One-command laptop operator launcher (preferred)

With only the ground Triple-Serial Teensy connected, run this from the repo
root:

```bash
./tools/c3m
```

It identifies one complete Triple-Serial group and maps the interfaces as GDS,
debug, and payload. Then it runs the existing payload UI command followed by
the existing `run_gds_uart.sh` command. Ctrl-C stops those two processes. It is
laptop-only: it never connects to, restarts, flashes, or otherwise manages the
satellite/Pi.

### GDS On Channel 0

On macOS, the ground Teensy is the **Triple Serial** device. Its first serial
port is channel 0 for GDS; do not use the satellite Teensy's separate
single-serial port. Confirm the three ground ports, then export the first one:

```bash
ls -l /dev/cu.usbmodem* 2>/dev/null
# Example ground triple serial ports: ...301 (GDS), ...303 (debug), ...305 (payload)
export GDS_DATA_PORT=/dev/cu.usbmodem115553301
```

Replace the example value with the first port in the ground Teensy's
triple-serial group on this laptop. Keep the terminal open so `$GDS_DATA_PORT`
remains set.

Run local emulation **before** starting the hardware GDS. Local demo GDS
instances use the same global `/tmp/fprime-server-in` and
`/tmp/fprime-server-out` IPC endpoints. If local emulation is run while the HIL
GDS is already open, the old browser UI can remain HTTP 200 while UART commands
stop. A browser refresh does not rebuild that backend; stop and restart the
complete hardware GDS process tree, then confirm a PING and moving ground
`uart_rx`/`rf_tx_pkt` counters.

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
./tools/run_gds_uart.sh \
  --port "$GDS_DATA_PORT" \
  --baud 115200 \
  --gui-port 5050 \
  --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Open the GDS URL printed by the launcher.

### Payload Web App On Channel 1

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py
```

The app opens `http://127.0.0.1:8064/`. It auto-selects the ground Teensy
payload port only when the triple-serial grouping is unambiguous; otherwise,
select `GDS_PAYLOAD_PORT` in the browser. Do not request a downlink until the
app shows green `Ready — awaiting downlink`.

The app listens continuously, writes one timestamped directory per transfer
under repo-root `data/`, verifies whole-file CRC, decodes the exact current
`.fdp`, and displays its PNG. History browses earlier `data/` runs without
making them look current. See
[`C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md`](C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md).

If an incomplete product is already useful, click **Stop & save partial**.
This is ground-only: it saves the packets already received in their correct
positions, marks missing pixels white/`NaN`, and ignores the remaining packets
for that transfer while leaving the receiver ready for the next transfer ID.
The satellite continues its current transmission. Use the separate GDS abort
command only when mission operations intend to stop spacecraft RF airtime.
After GDS reports `commsApp.DownlinkFinished` (and `DownlinkActive = 0`), the
latest captured product remains available: send
`commsApp.REQUEST_SCIENCE_DOWNLINK` again to retry the same picture with a new
transfer ID. Do not retry immediately after the ground partial save because
the spacecraft is still sending transfer 1. A newer collection replaces the
retained latest-product descriptor; a Pi/F´ process restart clears it.

### Engineering CLI Fallback

Use this only when the web app is stopped. Only one process may own the
channel-1 serial port.

```bash
cd ~/Developer/fprime-artemis-cubesat
RUN_DIR="$PWD/data/c3m_cli_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RUN_DIR"
python3 -u ArtemisRpiTeensy_N2/tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output-dir "$RUN_DIR" \
  --ext .fdp \
  --idle-timeout 0 \
  --debug
```

`--output-dir` is the CLI's explicit continuous-listen mode; the unchanged CLI
default remains single-file mode. For manual decode after a completed fallback
transfer, pass the exact `.fdp` path:

```bash
python3 ground-station/lepton-dp-viewer/lepton_dp_viewer.py \
  "$RUN_DIR/Dp_<timestamp>.fdp" \
  --outdir "$RUN_DIR/decode" \
  --summary --no-show
```

## Event-Gated Demo Command Sequence

Use the **GDS Commanding** page for this entire sequence. Do not use the CLI
for normal demo operations.

1. In the payload web app, confirm green **Ready — awaiting downlink**. In
   GDS, note `storageManager.StoredProducts`.
2. Send these one-time GDS commands in order:

   | Command | Argument |
   | --- | --- |
   | `missionApp.ENTER_BASE_MODE` | none |
   | `sohApp.EMIT_SOH_SNAPSHOT` | none |

3. For each requested picture, send:

   | Command | Argument |
   | --- | --- |
   | `missionApp.SCHEDULE_COLLECTION` | `delaySeconds = 10` |

4. Wait for a new `storageManager.ScienceStored` event. Its product count must
   be greater than the value noted in step 1, and its size must be nonzero.
   Do not downlink an older product.
5. Send `commsApp.REQUEST_SCIENCE_DOWNLINK`.
6. Confirm GDS reports `PayloadDownlinkStarted` and that its product ID matches
   the payload web-app header. During the transfer, send exactly one
   `missionApp.PING` with token `37002` to prove channel 0 remains usable.
7. Otherwise keep channel 0 quiet until the payload web app reports CRC-complete
   decode. Do not use `GET_PAYLOAD_STATUS` during a normal timed run.
8. Confirm the receiver is back at **Ready**, then repeat steps 3-7 for the
   next picture. Leave all processes and both Teensys running between cycles.

The Lepton driver opens the camera when the collection arrives and takes one
frame; separate enable and capture-duration commands are not required for this
MVP. Storage automatically hands the new descriptor to the downlink path, so
storage report commands are optional diagnostics rather than demo steps.

## HIL Pass Criteria

Record these without changing pacing or ACK policy:

- payload web app showed `Ready` before the downlink request.
- GDS event `LeptonBackendSelected backend=uvc`.
- a new `ScienceStored` product count appeared after scheduling.
- `ScienceProductDescriptor` handoff has nonzero source path, byte count, and
  source CRC.
- GDS `PayloadDownlinkStarted` and the web-app header identify the same product.
- command-to-file time is recorded from downlink request to web-app completion.
- web app reaches `Complete`, final CRC succeeds, and its exact current product
  decodes as `width=160`, `height=120`, `pixels=19200`.
- the app writes `.fdp`, JSON, CSV, PNG, and `run.json` under repo-root `data/`.
- `rf_retries`, `rf_ack_timeouts`, `rf_msg_id_gaps`, and queue drops are
  recorded from both Teensy debug ports.
- exactly one mid-transfer PING proves channel 0 remains responsive; otherwise
  channel 0 remains quiet during bulk transfer.
- any retry round count is acceptable only if the final file is byte-correct and
  the demo timing remains within `120 s`.

Timing interpretation:

- `75 s` or less: nominal target.
- around `90 s`: longer than target but still acceptable while progress remains
  visible and CRC succeeds.
- `120 s`: live-demo cutoff. Preserve best-effort evidence and mark the timed
  rehearsal unsuccessful. If progress has stopped, click `Reset receiver`, wait
  for `Ready`, then make any retry an explicit new GDS action.

## Three-Run Demo Rehearsal

At the intended demo geometry, complete three consecutive fresh:

```text
capture -> fresh ScienceStored gate -> downlink -> CRC -> decode -> display
```

The first run begins from a fresh web-app and bench startup. For each run,
record the product/transfer identity, elapsed time, retry count, CRC result, and
`data/` output directory. Require:

- `3/3` newly captured products reach CRC-complete decode.
- each finishes before the `120 s` cutoff; `75 s` or less is the nominal target.
- exactly one mid-transfer PING per run and no other bulk-transfer channel-0
  traffic.
- channel 0 remains usable and the transport/parser queue-drop counters remain
  zero.
- no transport-value changes between runs.

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

### July 16 close-range recovery hardening

- Fixed antennas stayed approximately 2–5 ft apart; RF power and RadioHead
  configuration were unchanged.
- ARMv6 Pi deployment, GDS, and payload receiver stayed running across the
  recovery and repeated-transfer tests.
- Real channel-2 radio control passed 10/10 `OFF` to `READY` cycles without a
  Pi restart or systemd restart.
- Three new 38,480-byte Lepton products completed 1,100/1,100 transfer, CRC,
  160x120 decode, and exact Pi/ground SHA-256 matching in 65–67 seconds.
- Mid-transfer PING passed immediately in 2/3 runs; the missed response did not
  wedge the link and passed on the immediate idle retry.
- `CommsApp` now owns authoritative comms health. A quiet but healthy RF channel
  no longer becomes a false SOH failure through the legacy transport monitor.

See the KISS recovery plan for exact release hashes, evidence paths, and the
physical electrical/fault gates that remain pending.

### July 16 ground-cancel and same-picture retry acceptance

- Active Pi release:
  `/home/pi/artemis/releases/c3m-payload-retry-camera-20260716T224440Z-7c23d355`;
  ARMv6 binary SHA-256
  `7c23d3554e4d6abb0b5c81180190c1113c779fc1271b74ebfb7f687edf5d9e79`.
- A post-restart UVC capture produced product 1 with 19,200 valid pixels, zero
  values below 1,000 centikelvin, and a plausible 14.83–24.69 C range.
- The camera wrapper now rejects short UVC frames and frames with more than 1%
  physically impossible low pixels. This prevents a partial startup frame from
  becoming the retained science product; the existing five-second capture
  timeout remains the bounded failure path.
- Ground reception of transfer 1 was stopped at 253/1,100 packets. The receiver
  saved `data/c3m_20260716_224646_transfer_1/` as an operator-cancelled partial
  with 4,389 valid image pixels while the spacecraft continued transmitting.
- After `commsApp.DownlinkFinished`, no new collection command was sent.
  `commsApp.REQUEST_SCIENCE_DOWNLINK` resent product 1 as transfer 2.
- Transfer 2 completed 1,100/1,100 in 64.8 seconds with no repair round, matching
  CRC, and a successful mid-transfer PING. Complete evidence is
  `data/c3m_20260716_224901_transfer_2/`.
- Spacecraft source, pre-RF local copy, and complete ground file all matched
  SHA-256
  `1babc1aa35ed840d12b6353cf44cabbd1face542d69cd544384ed9a33ffb472c`.

This is the intended demo contract: capture once, make a best-effort ground
attempt, preserve any useful partial, then request the same latest picture
again after the spacecraft finishes. A newer capture replaces the retained
picture, and a Pi/F Prime process restart clears the volatile descriptor.

## Stop Rules

Stop the timed run and preserve its evidence if any of these happen:

- channel-0 commands or telemetry become unreliable.
- payload web app never reaches final CRC or reports an explicit failure.
- Teensy queue drops appear.
- RF message-id gaps climb continuously.
- Pi deployment restarts or watchdogs.
- C3M full-res HIL timing exceeds `120 s` after retries.

Do not retune transport values during this mission-operations hardening pass.
First distinguish serial ownership, stale-product sequencing, camera/backend,
decode/filesystem, and genuine RF transport failures using the saved `run.json`,
GDS events, Pi journal, and Teensy counters.

## Cleanup

Stop the web app with `Ctrl-C` in its terminal after the demo session. If the
engineering CLI fallback was used instead, stop that process before reopening
the web app.

Keep repo-root `data/` as the operator's local payload history. It is ignored by
Git; do not delete run folders until hashes, logs, and screenshots needed for
the FSR writeup and three-run rehearsal record have been saved.
