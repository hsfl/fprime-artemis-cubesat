# HackRF C3M Ground Station Runbook

> **Archived status — research/diagnostic path, not normal mission operations:** The
> accepted 2026-08-06 engineering decision makes the ground
> Teensy/RFM23BP and `./tools/c3m` the primary C3M path. Bidirectional HackRF
> development and outdoor qualification are stopped. Read
> [`C3M_HACKRF_GROUND_STATION_DECISION_2026-08-06.md`](C3M_HACKRF_GROUND_STATION_DECISION_2026-08-06.md)
> before reproducing this work. The procedure below is preserved for research,
> RX diagnosis, education, and evidence review.

BLUF: for a deliberate HackRF reproduction, attach the POBADY antenna and run
`./tools/c3m-sdr`. The launcher finds
the known C3M RF22 signal, selects working RX and TX gains, opens GDS plus the
payload/livestream UI, and keeps adapting while the geometry changes. Operators
do not enter RF settings or restart GDS to reacquire the link.

## Preserved reproduction flow

### 1. Physical preflight

Require:

- satellite RFM23BP antenna attached and satellite/Pi on;
- POBADY 433 MHz SMA-male antenna and 3 m RG174 attached to HackRF;
- magnetic base placed on a suitable metal ground plane when practical;
- no direct coax connection from another transmitter to HackRF;
- no Gqrx, `hackrf_transfer`, or second bridge owning the HackRF.

The [POBADY listing](https://www.amazon.com/dp/B094MW1YMV) advertises 3 dBi.
That is not a measured installed gain. Cable loss, ground plane, orientation,
height, nearby metal, multipath, and interference remain part of the RF path.

### 2. Start everything

```bash
cd ~/Developer/fprime-artemis-cubesat
./tools/c3m-sdr
```

Normal launch is RX/TX with no special flags, like `./tools/c3m`. Use
`./tools/c3m-sdr --rx-only` only for receive-only diagnosis.

Calibration does this before declaring ready:

1. applies progressively higher HackRF RX presets;
2. accepts only CRC-valid `epscorc3m` frames with zero clipping and USB drops;
3. sends unique, valid F Prime `missionApp.PING` commands over the supported
   HackRF TX VGA range;
4. accepts TX only when the exact generated token returns in channel 0;
5. starts continuous link maintenance after the startup selections pass.

For each direction, the complete normal-gain range is tried with HackRF's RF
amplifier off. Only if that direction cannot prove the link does calibration
enable the amplifier and restart from its lowest gain. RX and TX selections
are independent. Antenna-port bias power always stays off because the POBADY
antenna is passive.

During operation, short fades and individual missed ACKs are ignored. Sustained
CRC-valid frame silence advances RX through the normal presets with a dwell
between changes. Two clipped I/Q blocks step RX downward. Only after all normal
RX settings fail does the RX amplifier stage begin from minimum gain. Repeated
ACK timeouts advance TX; sustained ACK success cautiously backs TX down. GDS
and the payload receiver stay connected throughout.

Immediate RF22 ACK alone is not the TX oracle because HackRF's half-duplex
USB turnaround can miss that short response. The delayed Pong proves uplink,
satellite FSW execution, and downlink.

Wait for:

```text
GROUND_STATION_READY run=/tmp/c3m-sdr/runs/<timestamp>
RF_AUTO_SELECTED rx_lna=<dB> rx_vga=<dB> rx_amp=<off|on> tx=<dB> tx_amp=<off|on>
GDS_URL http://127.0.0.1:5057
PAYLOAD_URL http://127.0.0.1:8064
SDR STATUS | link tracking | RX LNA <dB>, VGA <dB>, amp <off|on> | TX gain <dB>, amp <off|on> | frames <count>, last frame <age> | reason <reason>
```

`SDR STATUS` prints every 10 seconds and immediately after an RF-state change,
so the operator can see adaptation without reading JSON or restarting GDS.

If calibration cannot prove both paths, the launcher fails closed and does not
start GDS. Fix the physical path or use the RFM23BP fallback; do not ask the
operator to guess gains.

### 3. Ready gate

```bash
jq '{
  radio_state, network, gain_control, calibration, adaptive_link,
  rx_lna_gain, rx_vga_gain, tx_gain,
  rf_amp_enabled, rx_rf_amp_enabled, tx_rf_amp_enabled,
  antenna_power_enabled,
  rx_blocks, rf22_frames, reconnects, last_error,
  rx_iq
}' /tmp/c3m-sdr/latest/bridge-status.json
```

Require:

- `radio_state = receiving`, `network = epscorc3m`;
- `gain_control = automatic`, `calibration.state = complete`;
- `adaptive_link.enabled = true`, normally `adaptive_link.state = tracking`;
- selected RX window has `passed = true`;
- selected TX probe has `pong = true`;
- RX/TX amplifier selections match the calibration proof; normally both are
  `false`, and an enabled direction must show the complete amp-off search failed;
- antenna bias is `false`;
- RX blocks/frames increase, clipping and dropped blocks remain zero;
- both web pages respond and payload says `Ready — awaiting downlink`.

### 4. Channel-0 smoke

In GDS send `missionApp.PING` with a unique token. Require the matching
`missionApp.Pong`, no final bridge TX failure, and continuing telemetry.

### 5. Livestream regression

Send `payloadStreamApp.START_STREAM`. Require consecutive preview frames with:

- `80x60`;
- `4800/4800` bytes;
- `complete = true` and `crc_ok = true`.

Then send `payloadStreamApp.STOP_STREAM` and wait for
`PreviewStreamStopped` before science downlink.

### 6. Full demo regression

Send in GDS:

```text
missionApp.ENTER_BASE_MODE
sohApp.EMIT_SOH_SNAPSHOT
missionApp.SCHEDULE_COLLECTION  delaySeconds = 10
```

Wait for a new nonzero `storageManager.ScienceStored`, then send:

```text
commsApp.REQUEST_SCIENCE_DOWNLINK
```

During the transfer, send exactly one unique `missionApp.PING`. Require:

- matching `PayloadDownlinkStarted` product and receiver transfer;
- `1100/1100`, zero missing packets after repair, final CRC match;
- automatic `160x120` / `19200`-pixel decode;
- mid-transfer Pong and `DownlinkFinished`;
- completion under the 120-second cutoff.

### 7. Collect proof

```bash
SUPERVISOR_RUN="$(readlink /tmp/c3m-sdr/latest)"
PAYLOAD_RUN="data/c3m_<timestamp>_transfer_<id>/run.json"

ground-station/hackrf-rf22/.venv/bin/python \
  ground-station/hackrf-rf22/collect_hil_proof.py \
  "$SUPERVISOR_RUN" --payload-run "$PAYLOAD_RUN"
```

Add `--pi-sha256 <hash> --pi-path <path>` when Pi source-hash evidence is
available. A CRC-complete decoded `.fdp` is science proof; preview alone is not.

### 8. Stop

Press `Ctrl-C` once in the launcher terminal. The supervisor stops payload UI,
GDS, and bridge, then writes the final manifest state. Completed data remains.

## Historical outdoor procedure

Outdoor qualification is no longer scheduled under the accepted stop-work
decision. The procedure below is retained only to make the investigation
reproducible if that decision is formally reopened.

The operator command does not change. Start `./tools/c3m-sdr` at the initial
geometry and leave it running as the satellite moves. The bridge tracks valid
frames, clipping, and ACK outcomes; it adjusts and reacquires without restarting
GDS or the payload receiver.

What does change is the evidence requirement. Automatic gain selection is not
outdoor qualification. Record location, distance, antenna height/orientation,
ground plane, cable routing, HackRF serial, selected gains, clipping/drops,
PING/Pong, preview results, and science CRC/decode. Run three consecutive fresh
science cycles at the intended outdoor geometry before calling that setup
demo-qualified.

For qualification, pause at each planned distance long enough for
`adaptive_link.state` to return to `tracking`, then collect the required proof.
The half-second adjustment dwell and multi-sample clipping hysteresis prevent
rapid RX gain thrashing during brief motion or multipath fades.

## Automatic range and limits

- RX search spans HackRF-supported LNA `0..40` and VGA `0..62` presets.
- TX search spans the complete HackRF TX VGA range `0..47`.
- RX adjustment begins after 1.5 seconds without a valid C3M frame, then changes
  at most once per half-second until a CRC-valid frame returns.
- One missed command ACK advances TX by one state. The command retry budget is
  expanded automatically so that one command can traverse every remaining
  stronger state instead of waiting for another operator command.
- Six consecutive command ACKs permit one cautious TX step down.
- The RF amplifier starts off. It is enabled for RX or TX only after that
  direction exhausts its normal gain range, then the search restarts low to
  avoid throwing maximum VGA gain and broadband amplifier gain together.
- At the strongest amplifier-assisted state, RX and TX hold maximum instead of
  wrapping to minimum and dropping a marginal link.
- Antenna-port DC bias stays off because this antenna is passive.
- Frequency, modulation, headers, packet format, and C3M identity come from the
  known RFM23BP/transport configuration and are not learned from arbitrary RF.

These are hardware/profile invariants, not the old indoor 0/8/16 observation.

On every start, the launcher terminates prior repo-owned SDR supervisors and
their bridge/GDS/payload children, including processes still holding the
runtime PTYs or selected listening ports. It never takes over an unrelated
listener: if a selected port remains occupied after cleanup, startup stops and
reports that port. Recently released TCP connections in `TIME_WAIT` do not
block a restart.

The bridge creates its two PTYs first, then GDS and the payload viewer start
concurrently while automatic RF calibration continues. Each browser surface
opens as soon as its own HTTP server is ready; telemetry and commanding become
live after calibration completes and `GROUND_STATION_READY` is printed.

## Indoor evidence, 2026-08-06 HST

The original manual run observed RX `0/8` and a working TX at `16`. That was an
indoor observation, not a universal cap. The first automatic run selected RX
`0/0` and TX `32`; lower coarse TX probes did not return their exact PING
tokens under that run's conditions. After the amplifier-fallback enhancement,
the indoor rerun again selected RX `0/0` and TX `32`, with both direction
amplifiers off and passive-antenna bias off. Post-calibration PING token
`8061308` returned the matching Pong with zero TX failures.

The selected automatic session then passed:

- ordinary post-calibration PING token `680806`;
- multiple complete `80x60`, `4800/4800`, CRC-valid preview frames;
- new product/transfer 5, `38482` bytes, `1100/1100`, zero repair;
- expected/actual CRC `32806`, `11.9 s` completion;
- decode `160x120`, `19200` pixels;
- mid-transfer PING token `37002`;
- zero HackRF RX block drops and zero IQ clipping.

This proves the implemented indoor workflow. It does not prove outdoor range,
absolute radiated power, antenna impedance, regulatory authorization, or every
future geometry. The amp-on fallback order is hardware-free regression tested,
but this indoor geometry did not require it, so an actual amplified RF path has
not yet been physically qualified.

## Recovery and fallback

1. If calibration sees no valid C3M downlink, check satellite power, network
   profile, antenna connections, polarization, and interference.
2. If RX clips even at the lowest preset, increase separation or attenuation;
   do not rely on software to protect the analog input.
3. If no PING token returns across TX VGA, verify the Pi deployment and
   dictionary/profile match before blaming RF gain.
4. If ports remain in use, return to the existing launcher terminal and stop it
   cleanly; do not kill unknown processes blindly.
5. For a demo fallback, stop `c3m-sdr`, connect the known GDS
   Teensy/RFM23BP, and run `./tools/c3m`.

Hardware-free regression:

```bash
ground-station/hackrf-rf22/.venv/bin/python -m unittest discover \
  -s ground-station/hackrf-rf22 -p 'test_*.py' -v
```
