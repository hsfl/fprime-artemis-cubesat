# HackRF RF22 Ground Station

> **Project decision:** This implementation is preserved as a research,
> teaching, capture, and receive-diagnostic asset. It is not the primary C3M
> ground radio and is not scheduled for more bidirectional mission development.
> Use the ground Teensy/RFM23BP through `./tools/c3m` for normal operations.
> Rationale and evidence:
> [`docs/archive/C3M_HACKRF_GROUND_STATION_DECISION_2026-08-06.md`](../../docs/archive/C3M_HACKRF_GROUND_STATION_DECISION_2026-08-06.md).

BLUF: for deliberate reproduction, `./tools/c3m-sdr` is the HackRF C3M entry point. It
identifies the known CRC-valid C3M RF22 downlink, automatically selects RX and
TX gains, then continuously maintains them while F Prime GDS and the
payload/livestream UI remain running. Operators do not tune RF parameters.

```text
433 MHz C3M RF22/GFSK
  -> A5 / channel 0 -> /tmp/c3m-sdr/gds-port -> F Prime GDS
  -> A6 / channel 1 -> /tmp/c3m-sdr/payload-port -> payload/livestream UI
```

The satellite remains Pi -> satellite Teensy -> RFM23BP. HackRF replaces only
the ground Teensy/RFM23BP adapter; the dictionary and wire protocols stay the
same.

## Operator launch

From the repository root:

```bash
./tools/c3m-sdr
```

Use `./tools/c3m-sdr --rx-only` only for receive-only diagnosis. Normal launch
includes RX and TX, matching `./tools/c3m` behavior.

Wait for:

```text
GROUND_STATION_READY run=/tmp/c3m-sdr/runs/<timestamp>
RF_AUTO_SELECTED rx_lna=<dB> rx_vga=<dB> rx_amp=<off|on> tx=<dB> tx_amp=<off|on>
GDS_URL http://127.0.0.1:5057
PAYLOAD_URL http://127.0.0.1:8064
SDR STATUS | link tracking | RX LNA <dB>, VGA <dB>, amp <off|on> | TX gain <dB>, amp <off|on> | frames <count>, last frame <age> | reason <reason>
```

The compact SDR status repeats every 10 seconds and whenever the adaptive RF
state changes. Detailed machine-readable state remains in `bridge-status.json`.

Stop the complete stack with one `Ctrl-C`.

## Known C3M profile

| Item | Contract |
|---|---|
| Network/header | `epscorc3m`; downlink `A1 A2 C3 01`; uplink `A2 A1 C3 01` |
| Antenna | POBADY 433 MHz magnetic base, SMA male, 3 m RG174 |
| Frequency/PHY | 433 MHz RF22-compatible 125 kbps GFSK |
| RX selection | first preset with a CRC-valid C3M frame, zero IQ clipping, and zero dropped USB blocks |
| RX range | HackRF-supported LNA `0..40` and VGA `0..62` presets |
| TX selection | first coarse TX VGA setting returning the exact token from a generated `missionApp.PING` |
| TX range | complete HackRF TX VGA range `0..47` |
| Session behavior | startup proof, then hysteretic RX/TX adaptation and automatic reacquisition without restarting GDS |
| RF amplifier | off-first independently for RX/TX; only after a complete normal-gain failure, enable that direction and restart from its lowest gain |
| Antenna bias | always off; the POBADY is passive |
| Channel 0 | RF ACK/retry for ordinary commands |
| Channel 1 | RF ACK-free; application CRC and selective repair |

The PING TC builder is regression-locked against a byte capture emitted by the
current F Prime topology dictionary. A dictionary or command-layout change
must update that vector before calibration may transmit.

Immediate RF22 ACK reception is not used as the TX gain oracle. HackRF is
half-duplex and its USB-controlled TX-to-RX turnaround can miss an ACK even
when the satellite received the command. The delayed F Prime Pong proves the
whole uplink, satellite FSW execution, and downlink path instead.

## Antenna notes

[POBADY listing](https://www.amazon.com/dp/B094MW1YMV). Treat the advertised
`3 dBi` as seller metadata, not measured installed gain. Cable loss, magnetic
ground plane, orientation, nearby metal, multipath, and interference all
affect the installed link.

Attach the antenna before launch. Never connect another transmitter directly
to the HackRF without a verified attenuation/link budget. Software cannot
protect the HackRF analog input from excessive incident RF.

Moving outside does not require operator gain flags or a restart at each new
distance: start `c3m-sdr`, then leave it running while the spacecraft moves.
RX reacts after 1.5 seconds without valid frames, TX advances after each missed
command ACK, and one command receives enough retries to search all remaining
stronger TX states. The RF amplifier is not enabled just because geometry
changed: normal gains must fail first. Maximum assisted gain is held rather
than wrapped to minimum. Outdoor mission readiness still requires end-to-end
smoke/livestream/science proof; automatic gain selection is not itself outdoor
qualification or regulatory authorization.

Starting `c3m-sdr` also reaps a prior repo-owned supervisor and its
bridge/GDS/payload children, including stale runtime PTY and listening-port
holders. An unrelated process on a selected port is preserved and startup
fails with the occupied port instead. Released `TIME_WAIT` connections do not
block an immediate restart.

GDS and the payload viewer cold-start concurrently as soon as the bridge PTYs
exist. Their browser pages may open while RF calibration is still running;
wait for `GROUND_STATION_READY` before treating telemetry or commands as live.

## Qualification status

On 2026-08-06 HST, the path was proven indoors only. An earlier manual run
worked at RX `0/8`, TX `16`. The first automatic run selected RX `0/0` from a
clean CRC-valid window and TX `32` as the first coarse setting returning the
exact PING token. A repeat after adding amp fallback selected the same gains
with RX amp off, TX amp off, antenna bias off, and returned post-calibration
PING token `8061308` with zero TX failures. The amp-on fallback is covered by
hardware-free regression but was not physically invoked indoors because the
normal path worked. The difference from the earlier manual gain tuple is
evidence for measuring each session instead of freezing one indoor observation.

The automatic session passed:

- ordinary post-calibration PING/Pong;
- consecutive complete `80x60`, `4800/4800`, CRC-valid livestream frames;
- fresh product/transfer 5 in `11.9 s`, `1100/1100`, zero repair, matching CRC;
- `160x120`, `19200`-pixel decode and a successful mid-transfer PING;
- zero HackRF RX drops and zero IQ clipping.

This is not outdoor qualification, absolute RF power measurement, antenna
impedance proof, Neutron-2 `D2` qualification, or Windows qualification.

## One-time macOS setup

```bash
brew install hackrf
cd ground-station/hackrf-rf22
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
hackrf_info
```

The launcher also needs the repository F Prime venv and matching deployment
dictionary under `ArtemisRpiTeensy_N2/build-artifacts`.

## Evidence and tests

Each run records startup calibration, current gains, adaptation history, and
reacquisition evidence in:

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

`/tmp/c3m-sdr/latest` points to the newest run. The proof collector requires a
CRC/Pong-proven startup selection. If runtime settings changed, the current RX
state must have reacquired a valid frame and the current TX state must have
received an ACK; amplifier use must retain evidence that normal gain was
exhausted first.

```bash
ground-station/hackrf-rf22/.venv/bin/python -m unittest discover \
  -s ground-station/hackrf-rf22 -p 'test_*.py' -v
```

See [`docs/archive/HACKRF_GROUND_STATION_RUNBOOK.md`](../../docs/archive/HACKRF_GROUND_STATION_RUNBOOK.md)
for the operating flow and
[`docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`](../../docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md)
for mission commands and fallback.
