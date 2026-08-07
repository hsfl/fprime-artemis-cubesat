# C3M HackRF Ground-Station Engineering Decision

**Date:** 2026-08-06 HST
**Status:** Accepted; operational HackRF development stopped
**Decision owner:** C3M engineering lead
**Applies to:** the current C3M 433 MHz RF22-compatible ground link in this repository

## BLUF

Use the mirrored ground Teensy plus RFM23BP as the primary C3M command,
telemetry, preview, and science ground radio. Preserve the completed HackRF One
adapter as a research, teaching, capture, and receive-diagnostic asset, but do
not spend further SpaSat/C3M mission time trying to make it the normal
bidirectional ground station.

The HackRF work was technically successful: it recreated the RFM23BP/RadioHead
waveform and packet layers in software, delivered CRC-valid telemetry and
science, and sent real F Prime commands without changing the satellite wire
protocol. It did not meet the team's practical field-usability bar. The
hallway test retained a strong downlink but produced unreliable commanding and
frequent acknowledgement timeouts even after runtime calibration reached the
strongest HackRF TX state. Closing that gap would require more ground DSP/USB
latency work, an additional receiver, custom HackRF firmware, or a spacecraft
protocol accommodation. None is justified while the existing RFM23BP path is
more usable and the team has higher-priority SpaSat work.

This is a project-priority decision, not a claim that HackRF One or SDRs are
bad tools or that another mission could not use this implementation.

## Decision

1. `./tools/c3m` and the ground Teensy/RFM23BP are the primary C3M HIL and
   mission-operations path.
2. `./tools/c3m-sdr` is retained but is not the default, fallback, or
   qualification path. Use it deliberately for receive diagnosis, RF captures,
   education, or reproduction of the recorded investigation.
3. Do not change the validated satellite RFM23BP firmware, ACK timing, packet
   format, modem profile, or F Prime command path to accommodate HackRF One.
4. Do not schedule more HackRF TX gain automation, USB-turnaround tuning,
   outdoor qualification, or custom-firmware work unless a future owner opens
   a new decision with a concrete requirement that the RFM23BP path cannot
   satisfy.
5. Preserve the implementation, tests, runbooks, and evidence. They are useful
   proof that an SDR can interoperate over the air with the existing satellite
   without becoming its best operational partner.

## What We Built And Proved

The ground adapter implements the layers that the RFM23BP normally handles in
hardware:

- 8 Msps complex-I/Q acquisition and software GFSK demodulation;
- RF22 preamble/sync, RadioHead identity, length, and CRC handling;
- the repository's channel 0/channel 1 segment protocol and reassembly;
- pseudo-terminals compatible with F Prime GDS and the payload receiver;
- matching RF22/GFSK transmit waveforms;
- channel-0 ACK/retry and channel-1 application repair;
- startup RX/TX calibration, amplifier fallback, runtime adaptation, and
  evidence capture.

The controlled indoor qualification proved that this was a real interoperable
ground station rather than a spectrum-viewing prototype. It completed F Prime
PING/Pong, repeated `80x60` previews, and an exact `160x120` science product:
`1100/1100` packets, matching CRC/source hash, successful decode, and a
mid-transfer PING. Those results remain valid for the tested indoor setup; they
do not establish general field range or operational equivalence to the
RFM23BP.

## Hallway Evaluation

The decision-driving run is:

```text
/tmp/c3m-sdr/runs/20260806_164909
```

The directory is local runtime evidence rather than a committed repository
artifact. Its final `bridge-status.json` recorded:

| Observation | Result |
| --- | ---: |
| CRC-valid RF22 frames | 2,915 |
| Complete channel-0 messages | 951 |
| Reassembly drops / timeouts | 2 / 2 |
| Dropped HackRF USB RX blocks | 0 |
| Command TX attempts | 74 |
| RF ACKs received | 26 |
| RF ACK timeouts | 43 |
| Command-message TX failures | 3 |
| Runtime TX gain | 47 dB plus RF amplifier enabled |
| Holds at the maximum TX state | 34 |
| RX reacquisitions | 12 |

The important asymmetry was repeatable: the SDR continued receiving a large
amount of valid telemetry while commanding became intermittent. Startup found
a valid end-to-end PING at TX gain 32 dB with the RF amplifier off, but the
moving hallway run later exhausted the normal TX range, enabled the RF
amplifier, reached gain 47 dB, and repeatedly held that maximum state.

The run ended after the HackRF disconnected, so the final reconnect errors are
not used as evidence about RF range. The engineering decision is based on the
link behavior before that disconnect.

## Antenna Checks

The operator reported these NanoVNA readings at 434 MHz in the tested mounted
configurations:

| Antenna/configuration | SWR | Smith impedance |
| --- | ---: | ---: |
| POBADY vertical on the magnetic floor | 1.47 | approximately 50 ohms |
| POBADY on the floor with aluminum sheet | 1.502 | approximately 51 ohms |
| Artemis steel monopole with aluminum sheet | 2.0 | approximately 36 ohms |

These readings do not characterize radiation pattern, cable loss, multipath,
or absolute gain, but they rule out a gross POBADY impedance mismatch as the
main explanation. The aluminum sheet provided no meaningful match improvement,
and the steel-monopole arrangement measured worse. Changing antennas was
therefore not a sufficient answer to the command-path problem.

## Why The RFM23BP Is The Better Operational Radio

| Property | RFM23BP | HackRF One in this implementation |
| --- | --- | --- |
| Intended role | Narrowband packet transceiver | General-purpose SDR development platform |
| 433 MHz TX capability | Up to approximately +30 dBm, with about 550 mA required at maximum | Official typical maximum is broadly +5 to +15 dBm across 10-2170 MHz; exact 433 MHz output was not measured here |
| RX/TX turnaround | Datasheet specifies 200 microseconds from RX to TX and TX to RX | No deterministic system-turnaround specification |
| Packet work | Hardware preamble, sync, headers, CRC, FIFO, AFC/filtering | Host software and USB I/Q processing |
| Switching control | Local MCU/SPI plus radio state machine | Stop asynchronous RX, change controls, stream TX, flush, stop TX, retune, reset the decoder, and start a new RX USB stream |
| Current field result | Operator-reported usable around hallway corners beyond approximately 30 ft, subject to the separate TX-power fault below | Good telemetry reception but unreliable hallway commands in the decision-driving run |

The output-power comparison is not a calibrated link-budget measurement. At
the published endpoints, the nominal difference can be roughly 15-25 dB, but
actual conducted power at 433 MHz, feed loss, antennas, polarization, and the
environment would need measurement before assigning an exact margin.

The timing difference is architectural. The RFM23BP datasheet gives 200
microseconds for direct RX/TX transitions and performs the sequence on-chip.
HackRF One is officially half-duplex, and libhackrf uses separate asynchronous
USB streams for RX and TX. The current adapter must finish the transmitted
samples, wait for the TX flush callback, stop TX, restore the RX tuning/gain,
and start RX before it can capture another packet.

The satellite sends its five-byte RF ACK immediately after accepting a valid
segment. It is therefore plausible that some commands reached the satellite
while their short ACKs occurred inside the HackRF's receive blind interval.
This explains why delayed application-level PING/Pong is a better HackRF uplink
oracle. The hallway logs did not timestamp the last radiated TX sample against
the first valid RX sample, so they do **not** prove that every timeout was a
missed ACK; propagation/link margin and turnaround may both contribute.

The logged `switch_tx_s` value of approximately 104 ms is also not a measured
TX-to-RX turnaround. It contains the configured 100 ms leading zero-I/Q period
and packet airtime, while excluding parts of the surrounding RX stop/retune and
RX restart sequence. It must not be cited as a HackRF turnaround specification.

## Options Considered And Rejected

### Change satellite ACK timing or repeat ACKs

Rejected. The satellite works with the purpose-built RFM23BP ground node and
the existing packet protocol. Altering validated spacecraft behavior for one
ground SDR weakens backward compatibility and creates another HIL matrix.

### Continue optimizing host-controlled HackRF turnaround

Possible but not selected. Ground-only experiments could keep one center
frequency and digitally offset TX, remove redundant control writes, and
instrument TX-flush-to-first-RX-sample latency. These may improve the result,
but stock libhackrf still changes between separate host-controlled TX and RX
streams. The remaining benefit is uncertain and no longer mission-priority
work.

### Implement autonomous HackRF firmware switching

Technically possible in principle, but this becomes a custom firmware/USB API
project plus a new qualification burden. It is disproportionate to the need.

### Add a separate continuous receiver

A second SDR could remain in RX while HackRF transmits, eliminating the single
device's receive blind interval. It adds hardware, antenna isolation/filtering,
clock/frequency coordination, and software integration. The existing
ground-RFM23BP solution is simpler.

### Change the modem or spacecraft stack

Rejected. Over-the-air compatibility with the current satellite was a core
success of the experiment. Replacing the modem or transport would be a much
larger mission change and would discard that compatibility.

## RFM23BP Follow-Up Is A Separate Electrical Problem

Selecting RFM23BP as the primary ground radio does not erase its known
ground-side TX/control wedge. The observed failure is local to some high-power
transmissions and may involve supply impedance, the approximately 550 mA PA
transient, local decoupling, wiring/ground return, SPI integrity, RF load, or
module condition. SDN/POR recovery contains the fault but does not establish
its electrical root cause.

Future radio effort should go there: measure VCC at the module during TX with
a short probe ground, verify the power source and return path, add/validate
local bulk and ceramic decoupling, compare a known-good module, and qualify the
lowest TX power that meets the required link. Do not treat a longer software
timeout as a brownout fix.

See
[`C3M_GROUND_RFM23BP_TX_WEDGE_RCA_2026-07-16.md`](../C3M_GROUND_RFM23BP_TX_WEDGE_RCA_2026-07-16.md)
and
[`C3M_RFM23BP_KISS_CONTROL_PLAN_2026-07-16.md`](../C3M_RFM23BP_KISS_CONTROL_PLAN_2026-07-16.md).

## When The HackRF Work Is Still Useful

The preserved adapter remains valuable for:

- receive-only telemetry, spectrum, modulation, and packet investigation;
- saved-I/Q regression and RF22 decoder development;
- teaching the GFSK, packet, segmentation, and GDS bridge layers;
- independent observation while the RFM23BP ground station operates, provided
  the RF setup is safe;
- future projects whose requirements favor SDR flexibility over compact,
  deterministic packet-radio behavior.

It should not be presented as the normal C3M operator path or as outdoor
qualified.

## Reopening Gate

Reopen bidirectional HackRF work only with a named owner and a requirement the
RFM23BP path cannot meet. At minimum, a new proposal must define:

1. required command range, packet-success rate, and response latency;
2. a conducted or controlled radiated link budget;
3. direct measurement of TX-end to first recoverable RX sample;
4. a plan for immediate ACKs without changing the spacecraft protocol;
5. three consecutive full command/telemetry/preview/science HIL cycles at the
   intended geometry.

## Sources And Repository Evidence

- [HopeRF RFM23BP datasheet](https://www.hoperf.com/uploads/RFM23BPdatasheet_1695351296.pdf), including operating-mode response times and TX current.
- [Repo-local RFM23BP datasheet extraction](../rfm23bp/RFM23BP_datasheet.txt).
- [Official HackRF One documentation](https://hackrf.readthedocs.io/en/latest/hackrf_one.html), including half-duplex, USB, 8-bit, and typical TX-power specifications.
- [Official libhackrf source](https://github.com/greatscottgadgets/hackrf/blob/main/host/libhackrf/src/hackrf.c), showing separate start/stop RX and TX streams.
- [`ground-station/hackrf-rf22/README.md`](../../ground-station/hackrf-rf22/README.md), implementation and indoor qualification.
- [`HACKRF_GROUND_STATION_RUNBOOK.md`](HACKRF_GROUND_STATION_RUNBOOK.md), preserved reproduction and evidence procedure.
- [`HACKRF_SDR_GROUND_STATION_INVESTIGATION_2026-06-30.md`](HACKRF_SDR_GROUND_STATION_INVESTIGATION_2026-06-30.md), the original pre-implementation investigation.
