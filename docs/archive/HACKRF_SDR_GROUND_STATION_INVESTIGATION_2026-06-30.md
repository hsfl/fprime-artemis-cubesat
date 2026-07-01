# HackRF / SDR Ground Station Investigation

Date: 2026-06-30 HST
Status: investigation note; no implementation yet
Scope: replacing or augmenting the current `GDS_Teensy` ground node with a HackRF One / SDR-based ground path

## BLUF

A HackRF One ground station is technically plausible, but it is not a drop-in
replacement for the ground Teensy. The current RFM23BP + RadioHead stack hides
the modem, packet sync, packet header, length, CRC, FIFO, and interrupt handling
inside the RF22/RFM23BP chip and the RadioHead `RH_RF22` driver. HackRF exposes
raw IQ samples. That means an SDR path must implement the missing modem and
packet layer in software before it can feed bytes to `fprime-gds` or
`tools/payload_receiver.py`.

Recommended posture:

- Keep the current RFM23BP + ground Teensy path as the demo-safe path.
- Treat HackRF/SDR as a parallel research branch.
- First prove receive-only decode of existing RFM23BP packets.
- Only after receive decode works, add transmit/uplink and pseudo-serial ports.

## Existing Docs Checked

This investigation overlaps but does not duplicate:

- `docs/archive/RF_CHAIN_ROOT_CAUSE_ANALYSIS_2026-04-24.md`
  - Explains why the RFM23BP packet budget and lossy RF hop break large
    byte-perfect CCSDS transfer frames.
- `docs/archive/rf_refactor.md`
  - Defines reliability requirements for the RFM23BP bridge and records the
    practical `RH_RF22_MAX_MESSAGE_LEN = 50` constraint.
- `docs/archive/RADIO_AGNOSTIC_COMMS_AND_PAYLOAD_DOWNLINK_PLAN.md`
  - Defines the channelized architecture and radio-swap seams.
- `README.md`
  - Already says SDR is a future direction, not a near-term default, because it
    adds SDR stack maintenance and loses RadioHead code reuse.

No HackRF-specific or SDR-packet-compatibility note was found in the existing
RF/radio archive filenames as of this note.

## Current Ground Link Layers

The current hardware stack is:

```text
fprime-gds / payload_receiver.py
-> ground Teensy USB serial ports
-> ground Teensy RadioHead/RH_RF22 driver
-> RFM23BP packet radio at 433 MHz
-> satellite RFM23BP + RadioHead/RH_RF22
-> satellite Teensy channel bridge
-> Raspberry Pi /dev/serial0
-> UartChannelMux
-> F Prime ComCcsds and PayloadDownlinkManager
```

The current over-the-air RF settings used by the repo are:

```text
frequency: 433.0 MHz
modem: RH_RF22::GFSK_Rb125Fd125
tx power profile: RH_RF22_RF23BP_TXPOW_30DBM
RadioHead max message length: 50 bytes
repo RF packet max: 49 bytes
repo RF segment header: 5 bytes
repo RF useful segment data: 44 bytes
```

## Packet Stack

There are multiple "packet" layers. Keep them separate.

### Layer 0: RF waveform

The radio waveform is 433 MHz GFSK using the RadioHead `RH_RF22` modem profile
`GFSK_Rb125Fd125`. HackRF must sample and demodulate this as IQ. Matching the
center frequency alone is not enough.

### Layer 1: RadioHead / RF22 packet

RadioHead configures the RF22/RFM23BP packet handler. The relevant lower packet
structure is:

```text
preamble
sync words: 0x2d 0xd4
RadioHead header: TO, FROM, ID, FLAGS
length
data
CRC-16 IBM over header, length, and data
```

The RFM23BP chip currently handles much of this in hardware. An SDR receiver has
to recreate it in software:

- frequency correction
- GFSK demodulation
- clock recovery
- bit slicing
- preamble detection
- sync-word detection
- header/length parsing
- CRC verification
- packet extraction

### Layer 2: Repo RF segment inside RadioHead data

The repo puts this 5-byte segment header inside the RadioHead data field:

```text
byte 0: magic
  0xA5 = channel 0 CCSDS/GDS
  0xA6 = channel 1 payload
byte 1: msg_id
byte 2: seg_idx
byte 3: seg_count
byte 4: chunk_len
byte 5..48: chunk bytes, up to 44 bytes
```

ACK packets are 5 bytes:

```text
magic | msg_id | 0xFF | acked_seg_idx | 0
```

Channel meanings:

- channel 0 carries byte-pure F Prime CCSDS/GDS traffic
- channel 1 carries custom payload/science packets
- channel 2 is satellite-local RPC and does not cross RF

### Layer 3: Payload protocol inside channel 1

Payload packets begin with `N2`:

```text
"N2" | type | transfer_id | fields...
```

Types:

```text
1 = header
2 = data
3 = end
4 = retry request
```

Data packets look like:

```text
"N2" | 2 | transfer_id | packet_index_u16 | valid_len | data[<=35] | crc16_ccitt
```

This layer is intentionally filetype-agnostic and is already handled by
`ArtemisRpiTeensy_N2/tools/payload_receiver.py`.

## Compatibility Answer

HackRF is not directly compatible with RadioHead.

Specific compatibility split:

| Compatibility type | Answer | Why |
| --- | --- | --- |
| API-compatible with `RH_RF22` | No | RadioHead drives an RF22/RFM23BP chip over SPI registers and FIFOs. HackRF exposes raw IQ samples through libhackrf/SoapySDR/GNU Radio. |
| Drop-in replacement for `GDS_Teensy` | No | The ground Teensy currently provides USB serial endpoints plus RadioHead packet handling. HackRF provides neither without extra software. |
| Over-the-air compatible with RFM23BP | Possible | SDR software can emulate the same GFSK modem and RadioHead packet format, then parse the repo segment format. |
| Near-term demo-safe | No | It adds DSP/modem software risk. The current Teensy path is already validated for the MVP smoke flow. |

The satellite RFM23BP will not accept SDR-transmitted packets unless the SDR
reproduces the RF22/RadioHead packet layer closely enough: preamble, sync,
header, length, CRC, modulation, rate, deviation, timing, and frequency
accuracy.

## Software Needed For HackRF

Minimum useful stack:

```text
libhackrf / hackrf-tools
GNU Radio or SoapySDR
GFSK demod/mod blocks
packet sync/parser blocks
Python bridge daemon
pseudo-terminal creation for fprime-gds and payload_receiver.py
offline tools such as Inspectrum or Universal Radio Hacker for capture analysis
```

Receive path target:

```text
HackRF RX IQ
-> GFSK demod
-> clock recovery / slicer
-> RadioHead packet parser
-> repo segment parser
-> channel 0 PTY for fprime-gds
-> channel 1 PTY or direct pipe for payload_receiver.py
```

Transmit path target:

```text
fprime-gds / payload_receiver.py
-> PTY bridge
-> repo segment encoder
-> RadioHead-compatible packet encoder
-> GFSK modulator
-> HackRF TX
```

## Practical Phased Plan

### Phase 0: Preserve known-good demo path

Keep `GDS_Teensy` as the required path for RF MVP demo validation. Do not gate
the demo on SDR.

### Phase 1: Capture-only

Use HackRF to capture current ground/satellite traffic at 433 MHz while the
normal Teensy-to-Teensy link runs. Confirm the SDR can see bursts at expected
times and estimate frequency offset/SNR.

Deliverable:

- saved IQ capture
- note with sample rate, center frequency, gain settings, and observed burst
  timing

### Phase 2: Offline packet decode

Decode a saved capture into RadioHead payload bytes. The first success target is
extracting repo segment headers:

```text
0xA5 msg_id seg_idx seg_count chunk_len ...
0xA6 msg_id seg_idx seg_count chunk_len ...
```

Deliverable:

- Python or GNU Radio script that prints decoded RadioHead payload bytes and
  validates CRC

### Phase 3: Live receive-only bridge

Create a live demod process that exposes channel 0 and channel 1 as local
software streams. This can be receive-only and still valuable for telemetry and
payload downlink observation.

Deliverable:

- `fprime-gds` can observe channel 0 downlink through a PTY
- payload receiver can reconstruct channel 1 downlink from SDR-decoded bytes
- no SDR transmit yet

### Phase 4: Closed-loop TX/RX

Add transmit support so the SDR can send command uplink packets that the
satellite RFM23BP accepts.

Deliverable:

- `missionManager.PING` succeeds through HackRF ground path
- ACK/retry behavior works for channel 0 and channel 1

### Phase 5: Ground Teensy optional

Only after Phase 4 should the ground Teensy become optional. The acceptance gate
is running the RF MVP smoke/demo flow with the same proof bundle currently used
for the Teensy path:

- GDS command acknowledgement
- live SOH/event telemetry
- scheduled collection
- `PayloadDownlinkProgress`
- payload receiver `complete:`
- reconstructed payload hash match
- viewer parse success

## Risks And Open Questions

- HackRF One is half-duplex, like the RFM23BP, so ACK timing and TX/RX switching
  still matter.
- HackRF output power is not a substitute for the RFM23BP high-power PA path;
  use bench-safe attenuation and avoid assuming field range.
- RadioHead RF22 packet compatibility may require exact modem parameters beyond
  the symbolic `GFSK_Rb125Fd125` name.
- SDR transmit should be tested with dummy loads/attenuators and within legal
  amateur/ISM constraints.
- SatNOGS may be a better long-term architectural target than a one-off
  RadioHead-compatible HackRF bridge. A HackRF bridge is still useful as a
  learning and diagnostic tool.

## Decision

Do not rewrite the current ground station around HackRF for the MVP. Start a
research branch only after the current RFM23BP demo path remains frozen and
repeatable.

The first serious milestone is receive-only decode of existing RadioHead/RFM23BP
packets. If that cannot be made reliable, do not attempt SDR transmit or remove
the ground Teensy.

