# HackRF C3M Ground Station Runbook

BLUF: this is the primary student/operator path for the current C3M/macOS
FlatSat. Assemble the exact qualified HackRF/monopole geometry, run one command,
wait for both web pages, and execute the normal F Prime demo. Do not tune RF
values. If this fixed path fails, stop it and use the GDS Teensy/RFM23BP node.

## Commands: Run Top to Bottom

These are all normal operator commands. Everything below this section is
reference, checks, recovery, and qualification context.

### 1. Preflight

```bash
cd ~/Developer/fprime-artemis-cubesat
hackrf_info
ssh -o BatchMode=yes -o ConnectTimeout=5 artemis-pi-c3m \
  'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
```

Require the HackRF to appear and the Pi service to print `active`.

### 2. Start the Ground Station

```bash
cd ~/Developer/fprime-artemis-cubesat/ground-station/hackrf-rf22
.venv/bin/python run_hackrf_ground_station.py \
  --enable-tx \
  --tx-safety-confirmed
```

Wait for `GROUND_STATION_READY`, then open:

```text
GDS:     http://127.0.0.1:5057
Payload: http://127.0.0.1:8064
```

### 3. Run the Demo in GDS

Send these GUI commands in order:

```text
missionApp.PING                 (one unique token)
missionApp.ENTER_BASE_MODE
sohApp.EMIT_SOH_SNAPSHOT
missionApp.SCHEDULE_COLLECTION  delaySeconds = 10
commsApp.REQUEST_SCIENCE_DOWNLINK
```

Wait for the payload page to return `Complete`, `crc_ok=true`, and a `160x120`
decode before another collection.

### 4. Collect the Proof Bundle

```bash
cd ~/Developer/fprime-artemis-cubesat
SUPERVISOR_RUN="$(readlink /tmp/c3m-sdr/latest)"
PAYLOAD_RUN="data/c3m_<timestamp>_transfer_<id>/run.json"

ground-station/hackrf-rf22/.venv/bin/python \
  ground-station/hackrf-rf22/collect_hil_proof.py \
  "$SUPERVISOR_RUN" \
  --payload-run "$PAYLOAD_RUN"
```

Add `--pi-sha256 <hash> --pi-path <path>` when Pi hash evidence is available.

### 5. Stop

Press `Ctrl-C` once in the launcher terminal, then verify:

```bash
RUN_DIR="$(readlink /tmp/c3m-sdr/latest)"
jq '.state' "$RUN_DIR/run-manifest.json"
```

## Baseline Contract

```text
satellite Pi <-> satellite Teensy <-> satellite RFM23BP
                                      )) 433 MHz C3M RF22 ((
ground laptop <-> USB <-> HackRF One <-> 9-10 inch monopole
```

| Item | Fixed value |
|---|---|
| Profile | `epscorc3m` |
| Downlink / uplink headers | `A1 A2 C3 01` / `A2 A1 C3 01` |
| RF path | 9–10 inch vertical monopole, no attenuator, tested lab geometry |
| Channel-0 uplink | ACK mode, TX gain `16`, four bounded retries |
| Receive gain | LNA `8`, VGA `8` |
| TX settle lead | `100 ms` zero-IQ |
| RF amplifier / antenna bias | off / off |
| Gain policy | fixed; no AGC, adaptive profile, or automatic TX power |

The supervisor exposes:

```text
channel 0 -> /tmp/c3m-sdr/gds-port
             -> fprime-gds commands, events, telemetry

channel 1 -> /tmp/c3m-sdr/payload-port
             -> C3M payload receiver and selective repair

link health -> /tmp/c3m-sdr/latest/bridge-status.json
```

There is no third HackRF serial port. Structured metrics replace the fallback
ground Teensy's debug port. Satellite UART channel 2 remains local to the
satellite Pi/Teensy and never crosses RF.

The current proof is C3M-specific on the tested macOS host. It does not qualify
the Neutron-2 `D2` profile or Windows.

## 1. Physical Check

Before every launch:

1. Satellite RFM23BP antenna is attached.
2. HackRF has the 9–10 inch vertical monopole attached directly, with no
   attenuator.
3. The satellite and HackRF remain at the tested lab separation and geometry.
4. No coax directly connects the satellite transmitter to the HackRF.
5. Gqrx, `hackrf_transfer`, and old HackRF bridge instances are closed.
6. Other team nodes are off or use a different network/address.

The checked-in software disables the HackRF RF amplifier and antenna-port bias.
It cannot protect the analog input from excessive incident RF. Functional
packet success and zero ADC clipping do not prove antenna impedance or an
absolute RF-input safety margin.

Any antenna, attenuator, cable, distance, host, USB path, gain, or geometry
change ends this qualification. Students do not compensate by changing gain.

## 2. Preflight

From the repository root:

```bash
cd ~/Developer/fprime-artemis-cubesat

hackrf_info
ssh -o BatchMode=yes -o ConnectTimeout=5 artemis-pi-c3m \
  'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'

test -x ground-station/hackrf-rf22/.venv/bin/python
test -x ArtemisRpiTeensy_N2/fprime-venv/bin/fprime-gds
test -f ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json

lsof -nP -iTCP:5057 -iTCP:50057 -iTCP:8064 -sTCP:LISTEN 2>/dev/null || true
pgrep -af 'run_hackrf_ground_station|live_rx_bridge|hackrf_transfer|fprime-gds|c3m_payload_receiver_ui' || true
```

Require:

- the intended HackRF is visible;
- `artemis-fprime.service` is `active` with one deployment;
- both Python environments and the dictionary exist;
- ports `5057`, `50057`, and `8064` are free;
- no other process owns the HackRF.

If the stack is already intentionally running, use that stack. Do not kill or
replace it from another terminal.

One-time host setup:

```bash
brew install hackrf
cd ~/Developer/fprime-artemis-cubesat/ground-station/hackrf-rf22
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

## 3. Start the Fixed Ground Station

This is the only normal student launch:

```bash
cd ~/Developer/fprime-artemis-cubesat/ground-station/hackrf-rf22
.venv/bin/python run_hackrf_ground_station.py \
  --enable-tx \
  --tx-safety-confirmed
```

The confirmation means the operator personally checked the exact physical path
in section 1. RF values are loaded from checked-in constants; the launcher has
no gain, RF-path, network, or alternate channel-0 mode flags.

Continue only after:

```text
GROUND_STATION_READY run=/tmp/c3m-sdr/runs/<timestamp>
TX_ENABLED safety_confirmed=true gain=16 rf_path=monopole-9to10in-no-attenuator
GDS_URL http://127.0.0.1:5057
PAYLOAD_URL http://127.0.0.1:8064
```

Open:

- GDS: <http://127.0.0.1:5057>
- payload receiver: <http://127.0.0.1:8064>

The supervisor starts the bridge first, waits for real HackRF RX samples and
both virtual serial ports, then starts GDS and the payload receiver.

## 4. Ready Gate

Before sending the demo commands:

```bash
jq '{
  radio_state,
  network,
  rf_contract,
  tx_enabled,
  tx_mode,
  tx_gain,
  tx_leading_ms,
  rx_lna_gain,
  rx_vga_gain,
  gain_control,
  rf_path_label,
  rf_amp_enabled,
  antenna_power_enabled,
  rx_blocks,
  rf22_frames,
  reconnects,
  last_error
}' /tmp/c3m-sdr/latest/bridge-status.json
```

Require:

- `radio_state = receiving`;
- `network = epscorc3m`;
- `tx_enabled = true`, `tx_mode = ack`, `tx_gain = 16`;
- `tx_leading_ms = 100.0`, RX gains `8/8`, `gain_control = fixed`;
- amplifier and antenna power are `false`;
- `rx_blocks` increases;
- `reconnects = 0` and `last_error = null`;
- payload page says **Ready — awaiting downlink**.

Incoming frames must match the C3M header, RF22 CRC, and `A5`/`A6` channel
framing. Merely seeing RF energy is not a ready gate.

## 5. Channel-0 Smoke

In GDS send:

```text
ArtemisRpiTeensyDeployment.missionApp.PING
```

Use one unique token. Require all three:

1. bridge metrics show an ACK-backed completed channel-0 message with zero
   final TX failure;
2. Pi/GDS shows one execution for that token;
3. `missionApp.Pong` or `missionApp.PingCount` proves the downlink.

An ACK retry is allowed. Do not respond to one timeout by raising gain or
spamming duplicate commands.

## 6. Demo Flow

### A. Base Mode

Send:

```text
missionApp.ENTER_BASE_MODE
```

Require:

```text
missionApp.ModeChanged mode=BASE
missionApp.CurrentMode = BASE
```

### B. Live SOH

Send:

```text
sohApp.EMIT_SOH_SNAPSHOT
```

Require the `sohApp.Snapshot` event and moving SOH values in GDS.

### C. Schedule one fresh collection

Send:

```text
missionApp.SCHEDULE_COLLECTION
delaySeconds = 10
```

Require the ordered events:

```text
missionApp.CollectionScheduled delaySeconds=10
scienceApp.CollectionTriggered delaySeconds=10
scienceApp.ScienceProductReady productSize=<nonzero>
storageManager.ScienceStored product=<new id> size=<nonzero>
```

Record the new product ID. Do not reuse an earlier product as fresh proof.

### D. Downlink channel 1

Confirm the payload page is ready, then send:

```text
commsApp.REQUEST_SCIENCE_DOWNLINK
```

Require:

```text
commsApp.DownlinkRequested bytes=<nonzero>
storageManager.DownlinkPrepared product=<same new id>
payloadDownlinkApp.PayloadDownlinkStarted transfer=<new id>
payloadDownlinkApp.PayloadDownlinkComplete transfer=<same id>
commsApp.DownlinkFinished
```

The payload receiver must reach:

- `result=complete`;
- `1100/1100` packets for the current C3M sample product;
- `crc_ok=true`;
- automatic `160x120` / `19200`-pixel decode;
- a final SHA-256.

Channel 1 is intentionally ACK-free at RF level. Its CRC, missing-packet list,
and bounded selective repair provide delivery reliability.

## 7. Collect Proof

After a clean run, use the exact run paths:

```bash
cd ~/Developer/fprime-artemis-cubesat
SUPERVISOR_RUN="$(readlink /tmp/c3m-sdr/latest)"
PAYLOAD_RUN="data/c3m_<timestamp>_transfer_<id>/run.json"
PI_SHA256="<optional 64-hex SHA-256 read from the Pi>"
PI_PATH="<optional Pi payload path>"

ground-station/hackrf-rf22/.venv/bin/python \
  ground-station/hackrf-rf22/collect_hil_proof.py \
  "$SUPERVISOR_RUN" \
  --payload-run "$PAYLOAD_RUN" \
  --pi-sha256 "$PI_SHA256" \
  --pi-path "$PI_PATH"
```

Omit the two Pi options if Pi-side evidence was not collected. The collector
is read-only with respect to hardware and never opens SSH. It writes
`proof-summary.json` and `proof-summary.md` inside the supervisor run.

A strict pass requires:

- the exact fixed RF baseline and C3M identity;
- ACK-backed channel-0 command completion;
- traffic on channels 0 and 1;
- zero host RX drops, queue rejection, and IQ rail clipping;
- ordered Base -> SOH -> Schedule(10) -> Trigger -> Stored -> Downlink events;
- complete CRC/decode and exact selected product identity;
- Pi/ground hash equality when Pi evidence is supplied.

## 8. Recovery

Use this bounded decision tree:

1. If another process owns the HackRF or a port, return to its terminal. Do not
   kill it blindly.
2. If `radio_state` is `reconnecting`, wait for it to return to `receiving`.
3. If one ACK attempt times out, let the checked-in bounded retries finish.
4. If the bridge exits, queues reject data, clipping appears, ACK mode cannot
   complete, or the physical setup changed, stop this rehearsal.
5. Do not enable AGC, sweep gains, use blind repeats, or create another profile.
6. Switch to the GDS Teensy/RFM23BP fallback.

For an incomplete payload, preserve the partial artifacts, wait for
`commsApp.DownlinkFinished`, return the receiver to Ready, and request the same
retained product again. Do not schedule a new collection first.

If the satellite reports `LOCAL_TX_FAULT`, wait for Pi-owned recovery to return
the radio to `READY/NONE`. Repeated faults fail the run; do not spam commands,
restart GDS, or reflash the Teensy during the demo.

## 9. Cold Fallback: GDS Teensy/RFM23BP

No architecture refactor is required:

1. Press `Ctrl-C` once in the HackRF supervisor terminal.
2. Confirm the manifest state is `"stopped"` and no process owns the HackRF.
3. Connect the known GDS Teensy/RFM23BP node.
4. Follow the **Backup Ground Node** section in
   [`EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`](EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md).

The same Pi binary, F Prime dictionary, mission commands, RF headers, channel
mapping, payload receiver, CRC/decode, and proof gates remain in use. Do not
run both adapters against the same GDS/TTS ports.

## 10. Shutdown

Press `Ctrl-C` once in the supervisor terminal. It stops:

1. payload receiver;
2. GDS;
3. HackRF bridge.

Then verify:

```bash
RUN_DIR="$(readlink /tmp/c3m-sdr/latest)"
jq '.state' "$RUN_DIR/run-manifest.json"
pgrep -af 'run_hackrf_ground_station|live_rx_bridge|fprime-gds|c3m_payload_receiver_ui' || true
```

Expected manifest state is `"stopped"`. Completed payloads and proof artifacts
remain on disk.

## Qualification Boundary

The current geometry passed three consecutive fresh collection/downlink runs:

| Product / transfer | Time | Repair rounds | Command condition | SHA-256 |
|---|---:|---:|---|---|
| 7 / 8 | `81.448 s` | 4 | one mid-transfer `PING(49104)` | `055d76674407b4a0080339c76483b534a36e5a1ef700dfce3ca4de83c35af012` |
| 8 / 9 | `64.892 s` | 0 | quiet | `7db825b0b5b2ff2dc56e7e092d691d6796999add7d7dd87a9df09d8ee761a13e` |
| 9 / 10 | `64.912 s` | 0 | quiet | `4fa042fab6383c5cfde5687d3b457336ae8251c0400f02166f4416b178dd219e` |

All three were byte-identical to their fresh Pi products with zero HackRF RX
drops and final TX failures. The controlled half-duplex command caused seven
payload packets to require selective repair; the final product still completed
correctly.

The qualification does not move when the equipment moves. A changed distance
outside this lab is a new RF path and requires lead-supervised measurement and
the same three-run matrix. Until then, use the fixed lab geometry or the
fallback node.

Hardware-free regression:

```bash
cd ~/Developer/fprime-artemis-cubesat
ground-station/hackrf-rf22/.venv/bin/python -m unittest discover \
  -s ground-station/hackrf-rf22 \
  -p 'test_*.py' \
  -v
```

Software tests verify framing, safety policy, fixed configuration, ACK
accounting, bounded queues, proof rejection, and process cleanup. They do not
replace live RF qualification.
