# Payload Downlink Protocol Advice

Date: 2026-04-24  
Context: Neutron 2 payload file downlink over RFM23BP using the current F Prime + Teensy bridge architecture

## BLUF

For the MVP demo, do **not** force the estimated `40,368 byte` N2 payload file through stock F Prime `FileDownlink.FileDownlink_SendFile` over the current RFM23BP transparent GDS stream.

Use F Prime for:

- commanding
- mode/state transitions
- progress telemetry
- success/failure events

Use a custom EPSCOR-style payload downlink protocol for:

- bulk payload bytes
- packet indexing
- per-packet CRC
- whole-file CRC
- missing-packet retry
- reconstruction on the ground PC

This is the lower-risk demo architecture because the EPSCOR demo already proved this radio can move image-like payload data when the transfer protocol is designed around the radio.

## Why This Question Matters

There are two different claims that sound similar but are not the same:

1. The RFM23BP can transmit a payload image/file if we packetize it carefully.
2. The RFM23BP can reliably carry stock F Prime file downlink through `fprime-gds`.

The first is plausible and has precedent in the EPSCOR demo.

The second is not yet proven and is much riskier with the current RF chain.

The core issue is that F Prime/GDS expects a byte-perfect CCSDS telemetry stream. The RFM23BP is a small packet radio. A robust file transfer over it should treat the RF link as lossy and recover at the packet/file layer.

## Payload Size Reality Check

Estimated N2 payload file size:

```text
40,368 bytes
```

EPSCOR-style packetization:

```text
RF packet max = 49 bytes
payload bytes per packet = 45 bytes
40,368 / 45 = 896.0
```

So expect about:

```text
897 data packets
```

Our current relay packetization:

```text
RF packet max = 49 bytes
relay header = 5 bytes
useful payload = 44 bytes
40,368 / 44 = 917.45
```

So expect about:

```text
918 RF packets
```

That packet count is not impossible. But it requires a transfer protocol that can survive missing packets.

## What EPSCOR Proved

The EPSCOR reference under `external/epscorc3m` did not use stock F Prime file
downlink.

It used a custom payload protocol:

- header packet with image length and packet count
- indexed data packets
- per-packet CRC16
- full-image CRC16
- ground-side missing-packet tracking
- retry request bitmap
- satellite resend of missing packets
- custom ground software to reconstruct/export/display the result

Relevant design numbers from the EPSCOR notes:

| Item | Value |
| --- | ---: |
| RF packet cap | 49 bytes |
| packet index | 2 bytes |
| packet CRC16 | 2 bytes |
| useful payload per packet | 45 bytes |
| retry bitmap size | 45 bytes, 360 packet bits |
| max retry rounds in notes | 2 |

That architecture is well matched to the RFM23BP.

## Why Stock F Prime FileDownlink Is Riskier

F Prime file downlink is clean architecturally:

```text
file exists on Pi
F Prime FileDownlink sends file packets
ComCcsds frames them into CCSDS TM frames
GDS receives and reconstructs the file
```

But over the current RF bridge, each CCSDS TM frame has to survive several RF packets.

Current RF MVP settings:

```text
CCSDS TM frame = 128 bytes
RFM useful payload = 44 bytes
128 / 44 = 3 RF packets per TM frame
```

For a 40,368-byte payload, before F Prime overhead:

```text
40,368 / 128 = about 316 TM frames
316 TM frames * 3 RF packets/frame = about 948 RF packets
```

That is the optimistic lower bound. Real F Prime file downlink adds file-packet overhead, events, idle padding, retransmission consequences, and GDS framing constraints.

The main failure mode:

- one RF segment drops
- the CCSDS TM frame is incomplete or corrupted
- GDS frame CRC fails
- file downlink continuity suffers

This is why the current debug session saw APID sequence warnings even after valid TM frames started arriving.

## Integration Options

### Option 1: Separate Payload Downlink Program

Recommended for MVP.

Flow:

```mermaid
flowchart LR
  GDS["fprime-gds"]
  FSW["F Prime on Pi"]
  SAT_T["Satellite Teensy"]
  RF["RFM23BP"]
  GND_T["Ground Teensy"]
  HELPER["Ground Python helper/viewer"]

  GDS -->|"START_PAYLOAD_DOWNLINK command"| FSW
  FSW -->|"trigger/custom UART control"| SAT_T
  SAT_T -->|"indexed payload RF packets"| RF
  RF --> GND_T
  GND_T -->|"packet stream / reconstructed chunks"| HELPER
  FSW -->|"progress telemetry"| GDS
```

F Prime owns:

- `START_PAYLOAD_CAPTURE`
- `START_PAYLOAD_DOWNLINK`
- `ABORT_PAYLOAD_DOWNLINK`
- `GET_PAYLOAD_STATUS`
- telemetry/events for progress and result

Custom helper owns:

- RF payload packet decoding
- missing-packet tracking
- retry requests
- file reconstruction
- CRC verification
- Neutron 2 CSV display using `ground-station/neutron2-payload-viewer/`

Pros:

- fastest to build
- matches EPSCOR proven approach
- best fit for RFM23BP packet size
- easy to debug with raw packet counters
- reliable enough for a staged demo if traffic is controlled

Cons:

- final file does not automatically appear inside the stock F Prime GDS file-downlink panel
- operator uses GDS plus a companion viewer/helper

### Option 2: Custom F Prime Component + Telemetry Chunks

Possible middle ground.

Flow:

```text
GDS command -> PayloadDownlink F Prime component
PayloadDownlink emits chunk telemetry
GDS receives telemetry
custom ground script/plugin reconstructs file
```

This avoids `FileDownlink.SendFile` but still routes payload chunks through F Prime telemetry.

Pros:

- more visible in F Prime/GDS
- progress and packet fields are dictionary-defined
- easy to command from GDS

Cons:

- telemetry channels are not ideal for 40 KB bulk data
- still stresses the same CCSDS/GDS path
- requires custom reconstruction logic anyway
- may be slower and noisier than Option 1

Use this only if the team strongly wants the bytes to be represented as F Prime telemetry.

### Option 3: Stock F Prime `FileDownlink.SendFile`

Cleanest long-term architecture, highest current demo risk.

Flow:

```text
payload file is on Pi filesystem
GDS/F Prime command calls FileDownlink.SendFile
FileDownlink emits file packets
GDS file receiver writes the file
```

Pros:

- native F Prime architecture
- final file can be handled by GDS file-downlink machinery
- less custom ground software long-term

Cons:

- current RF link still shows sequence discontinuities
- frame loss is painful for GDS file reconstruction
- 40 KB product becomes hundreds of CCSDS frames
- not matched to the RFM23BP packet-loss behavior yet

This should be a later milestone after the RF link has a stronger reliable transport layer.

## Recommended MVP Architecture

Use Option 1.

### Command/Control Plane

F Prime/GDS should issue commands:

| Command | Purpose |
| --- | --- |
| `START_PAYLOAD_CAPTURE` | tell payload/RPi to acquire data |
| `START_PAYLOAD_DOWNLINK` | begin custom RF transfer |
| `ABORT_PAYLOAD_DOWNLINK` | stop transfer |
| `GET_PAYLOAD_STATUS` | query current state |

### Status Telemetry

F Prime should publish small status telemetry:

| Field | Meaning |
| --- | --- |
| `payloadState` | idle/capturing/ready/downlinking/done/error |
| `fileSizeBytes` | expected payload size |
| `totalPackets` | expected RF data packets |
| `packetsSent` | satellite-side sent count |
| `packetsMissing` | ground-reported missing count |
| `retryRound` | current retry round |
| `crcExpected` | file CRC from satellite |
| `crcReceived` | ground computed CRC if reported back |
| `lastError` | timeout, CRC fail, retry exhausted, etc. |

Keep these small. Do not stream the whole file as telemetry.

### Bulk Data Plane

The RF payload file protocol should look like:

1. Header packet
   - magic
   - transfer ID
   - file length
   - total packets
   - file CRC
   - optional filename/product ID
2. Data packet
   - transfer ID
   - packet index
   - payload bytes
   - per-packet CRC
3. End packet
   - transfer ID
   - packet count
   - file CRC
4. Retry request
   - transfer ID
   - bitmap offset
   - bitmap of missing packet indices
5. Retry data packets
   - same packet format as normal data

This is essentially the EPSCOR approach with clearer transfer IDs and status telemetry.

## Could This Hook Directly Into The F Prime GDS GUI?

There are three levels of GDS integration:

### Minimal Integration

Use stock GDS for command/status and a separate helper for file reconstruction.

This is recommended now.

The operator experience is:

```text
1. Press/send command in GDS.
2. Watch progress telemetry in GDS.
3. Ground helper reconstructs file.
4. Helper opens/displays result.
```

### GDS Dashboard Integration

Use a custom dashboard or panel to show progress telemetry.

The file still reconstructs outside the stock file-downlink plugin, but GDS can show:

- percent complete
- missing packet count
- retry count
- CRC pass/fail

This is a good demo polish step.

### Full GDS File Plugin Integration

Possible but not recommended for MVP.

This would require writing or modifying GDS-side code so the custom payload protocol appears like a native GDS file product. That is extra ground-software work and still does not solve RF reliability by itself.

## Why Separate Program Is Not "Less Legit"

For spacecraft systems, separating control plane and payload data plane is normal.

F Prime can remain the authoritative command and telemetry system while a mission-specific payload receiver handles bulk data products.

The clean division is:

```text
F Prime: "what should happen and what is the state?"
Payload protocol: "move these 40,368 bytes reliably over a lossy small-packet radio"
```

That is more honest than pretending a low-rate packet radio is a transparent serial cable.

## Recommended Demo Story

1. GDS shows spacecraft in base/ready mode.
2. Operator sends `START_PAYLOAD_CAPTURE`.
3. GDS telemetry shows `capturing`.
4. Payload file becomes ready with size `40,368`.
5. Operator sends `START_PAYLOAD_DOWNLINK`.
6. Ground helper shows packet progress:
   - `897 / 897 packets`
   - retry round
   - missing count
   - CRC status
7. GDS telemetry mirrors high-level status:
   - `downlinking`
   - `retrying`
   - `complete`
   - `crc_pass`
8. Helper displays the reconstructed payload file/image.

This gives judges the full mission story without betting everything on stock GDS file downlink over RFM23BP.

## Recommendation Summary

| Goal | Recommended Path |
| --- | --- |
| MVP command + status | F Prime/GDS |
| MVP 40 KB payload product | custom EPSCOR-style downlink + helper |
| GDS visible progress | small F Prime telemetry |
| Stock GDS file panel | later, after stronger RF transport |
| Long-term real file downlink | better radio or robust link layer |

Bottom line:

```text
Use F Prime to command the downlink and report progress.
Use a custom payload protocol to move the file.
Use a helper/viewer to reconstruct and display the file.
```
