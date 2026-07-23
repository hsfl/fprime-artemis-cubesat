# HackRF RF22 Ground Station

BLUF: this is the primary student-facing C3M ground adapter on macOS. One
supervisor starts the HackRF bridge, `fprime-gds`, and the payload receiver
with one checked-in RF configuration. Students do not select gains, TX modes,
profiles, or packet routes.

```text
433 MHz RF22/GFSK
  -> A5 / channel 0 -> /tmp/c3m-sdr/gds-port
                       -> fprime-gds commands, events, telemetry
  -> A6 / channel 1 -> /tmp/c3m-sdr/payload-port
                       -> C3M payload receiver
```

The spacecraft remains Pi -> satellite Teensy -> RFM23BP. The HackRF replaces
only the ground Teensy/RFM23BP adapter. Satellite-local UART channel 2 never
crosses RF. Switching to the fallback ground node does not change F Prime, the
satellite firmware, the dictionary, or either RF packet format.

## Fixed Student Baseline

| Item | Fixed value |
|---|---|
| RF profile/header | `epscorc3m`; downlink `A1 A2 C3 01`, uplink `A2 A1 C3 01` |
| Physical path | HackRF One -> 9–10 inch vertical monopole, no attenuator |
| Frequency | `433 MHz` |
| Channel-0 TX | ACK mode, TX gain `16`, `100 ms` zero-IQ settle lead |
| RX | LNA `8`, VGA `8` |
| RF amplifier | off |
| Antenna-port bias | off |
| Gain control | fixed; no AGC or automatic TX power |

These values are constants shared by the launcher and strict proof collector.
The student launcher does not expose RF tuning flags. Channel 0 always uses
bounded ACK/retry. Channel 1 remains ACK-free at RF level because its payload
protocol provides CRC, missing-packet detection, and selective repair.

The normal launch is:

```bash
cd ~/Developer/fprime-artemis-cubesat/ground-station/hackrf-rf22
.venv/bin/python run_hackrf_ground_station.py \
  --enable-tx \
  --tx-safety-confirmed
```

`--tx-safety-confirmed` means the operator has verified the exact qualified
antenna/no-attenuator path and tested bench geometry before allowing RF
transmit. Without both TX flags, the launcher stays receive-only and forces TX
gain to zero.

Wait for:

```text
GROUND_STATION_READY run=/tmp/c3m-sdr/runs/<timestamp>
GDS_URL http://127.0.0.1:5057
PAYLOAD_URL http://127.0.0.1:8064
```

Stop everything with one `Ctrl-C`. The supervisor stops the payload receiver,
GDS, and bridge in that order and records the final manifest state.

## One-Time macOS Setup

```bash
brew install hackrf
cd ~/Developer/fprime-artemis-cubesat/ground-station/hackrf-rf22
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
hackrf_info
```

The launcher also uses the existing F Prime virtual environment and matching
deployment dictionary under `ArtemisRpiTeensy_N2/build-artifacts`.

## What Is Deliberately Not Supported

- no AGC, adaptive gain, automatic TX power, or distance-based profile;
- no student-selectable TX/RX gains, RF path, network, or channel-0 TX mode;
- no channel-0 blind-repeat/degraded fallback;
- no direct radiation from `rf22_tx.py`—it is an offline waveform builder;
- no attempt to emulate a third/debug serial port—structured bridge metrics
  provide link health instead.

The lower-level bridge retains bounded engineering controls for a future
lead-supervised requalification. They are not the student/demo entry point and
cannot make degraded channel-0 proof pass.

## Qualification Status

As of 2026-07-22 HST, the fixed path is functionally demo-qualified at the
tested lab separation and geometry. Three consecutive fresh scheduled
collection/downlink runs passed after Base Mode and live SOH were established:

| Product / transfer | Time | Repair rounds | Controlled command | Pi/ground SHA-256 |
|---|---:|---:|---|---|
| 7 / 8 | `81.448 s` | 4 | one mid-transfer `PING(49104)` | `055d76674407b4a0080339c76483b534a36e5a1ef700dfce3ca4de83c35af012` |
| 8 / 9 | `64.892 s` | 0 | none | `7db825b0b5b2ff2dc56e7e092d691d6796999add7d7dd87a9df09d8ee761a13e` |
| 9 / 10 | `64.912 s` | 0 | none | `4fa042fab6383c5cfde5687d3b457336ae8251c0400f02166f4416b178dd219e` |

Every run received `1100/1100` packets, passed final CRC, matched the fresh Pi
source hash, decoded `160x120` / `19200` pixels, and recorded zero HackRF RX
drops and TX failures. The controlled half-duplex command caused seven payload
packets to need selective repair; delivery still completed byte-identically.
A final fresh `PING(49105)` returned its exact ACK on retry 2 and executed once.

This is C3M/macOS functional qualification, not a general RF certification. It
does not prove antenna impedance or absolute HackRF input power, and it does
not qualify the Neutron-2 `D2` profile or Windows.

## Safety and Change Control

- Attach the monopole before enabling TX.
- Keep the tested physical separation and geometry.
- Never cable another transmitter directly to the HackRF without a verified
  attenuation/link budget.
- Software cannot protect the HackRF analog input from excessive incident RF.
- Antenna length alone does not prove a 50-ohm match.
- The prior JFW attenuator/dipole and TX-gain-47 setup is historical evidence,
  not an alternate student configuration.
- Any antenna, attenuator, cable, distance, host, USB path, gain, or geometry
  change ends this qualification. Stop; a lead requalifies it.

If the fixed setup does not work during a student/demo session, do not sweep
gains or switch modes. Stop the HackRF supervisor and use the documented
GDS Teensy/RFM23BP fallback.

## RF Isolation

The decoder accepts a downlink only when all of these match:

- RadioHead header `TO=A1 FROM=A2 ID=C3 FLAGS=01`;
- RF22 CRC-16/IBM;
- `A5` channel-0 or `A6` channel-1 segment framing;
- bounded segment metadata and complete ordered reassembly.

This rejects nodes using another configured network/address. A second node
using the exact same C3M header is indistinguishable at this layer; turn it off
or assign it a different profile before the demo.

## Artifacts and Tests

Each run records:

```text
/tmp/c3m-sdr/runs/<timestamp>/
  run-manifest.json
  bridge-status.json
  bridge.log
  gds-console.log
  payload-ui.log
  gds/
  uplink/
```

`/tmp/c3m-sdr/latest` points to the newest run. Strict proof requires the exact
fixed baseline, both data channels, ACK-backed command uplink, clean bounded
queues, zero IQ rail clipping, ordered demo events, payload CRC/decode, and an
optional exact Pi hash.

Hardware-free regression:

```bash
cd ~/Developer/fprime-artemis-cubesat
ground-station/hackrf-rf22/.venv/bin/python -m unittest discover \
  -s ground-station/hackrf-rf22 \
  -p 'test_*.py' \
  -v
```

See [`docs/HACKRF_GROUND_STATION_RUNBOOK.md`](../../docs/HACKRF_GROUND_STATION_RUNBOOK.md)
for the operator flow and
[`docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`](../../docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md)
for mission commands plus the cold fallback.
