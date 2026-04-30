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

The EPSCOR demo in `espcor_teensy_demo` did not use stock F Prime file downlink.

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
- image/file display

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

## Optimal Clean Architecture Plan

The clean architecture is a two-plane system:

```text
Control plane:
fprime-gds <-> F Prime on Pi
Purpose: commands, state, progress, final success/failure
Protocol: normal F Prime CCSDS/GDS

Payload data plane:
sidecar receiver <-> ground Teensy <-> RFM23BP <-> satellite Teensy <-> Pi payload source
Purpose: move the 40 KB payload file reliably
Protocol: custom packet protocol designed for the radio
```

Do not try to make the bulk payload file look like normal F Prime telemetry. Use F Prime to say **what** should happen. Use the custom payload protocol to move the bytes.

### Recommended Physical/Data Ports

Keep the existing byte-clean GDS port dedicated to GDS:

```text
Ground Teensy data port: /dev/cu.usbmodem115551201
Use: fprime-gds CCSDS command/status stream
```

Run the payload sidecar on a separate USB interface:

```text
Ground Teensy sidecar/debug port: /dev/cu.usbmodem115551203
Use: binary payload downlink protocol + sidecar status logs
```

Reliability rule: do not mix GDS CCSDS bytes and payload file bytes on the same USB stream during the demo. If the sidecar uses the debug port for binary payload data, debug text should either be disabled during transfer or wrapped in a clearly separate sidecar message type. A binary receiver should not have to parse human log lines interleaved with payload chunks.

### Component Responsibilities

| Part | Responsibility |
| --- | --- |
| `fprime-gds` | send `CAPTURE`, `DOWNLINK`, `ABORT`, show high-level status |
| F Prime on Pi | own mission state, validate commands, report progress telemetry/events |
| Pi payload source | provide the payload file bytes and metadata to satellite Teensy |
| Satellite Teensy | cache or stream the payload, packetize for RF, resend missing chunks |
| Ground Teensy | receive RF chunks, verify per-chunk CRC, maintain missing map, send ACK/NAK/retry requests |
| Sidecar program | control ground payload session, reconstruct file, verify whole-file CRC, display/save result |

### Preferred Demo Flow

```text
1. Operator sends F Prime CAPTURE command from GDS.
2. Pi captures or prepares payload file.
3. F Prime telemetry reports payload_ready, file_size, product_id.
4. Operator starts sidecar receiver on ground PC.
5. Operator sends F Prime DOWNLINK command from GDS.
6. Pi sends payload metadata/file stream to satellite Teensy over UART.
7. Satellite Teensy starts RF payload session.
8. Ground Teensy receives chunks and forwards verified chunks to sidecar.
9. Ground Teensy/sidecar request missing chunks until complete or retry limit.
10. Sidecar verifies whole-file CRC/hash and writes/displays product.
11. F Prime reports downlink_complete or downlink_failed.
```

For the current `40,368 byte` file, the satellite Teensy can reasonably cache the whole product in RAM for the MVP if memory is reserved and bounded. That makes retransmission simple because the satellite Teensy can resend any missing packet without asking the Pi again. For larger future payloads, switch to a streaming/random-access design where the Pi can resend requested chunks.

### Payload Protocol Packet Types

Use small binary packets over RF. Keep every packet within the RFM23BP limit.

Recommended packet types:

| Type | Direction | Purpose |
| --- | --- | --- |
| `HELLO` | satellite -> ground | announce transfer ID, protocol version |
| `META` | satellite -> ground | file size, chunk size, chunk count, product ID, whole-file CRC/hash |
| `DATA` | satellite -> ground | one indexed payload chunk |
| `ACK_RANGE` | ground -> satellite | acknowledge a contiguous range or current high-water mark |
| `NAK_BITMAP` | ground -> satellite | request retransmission of missing chunk indices |
| `RESEND_DATA` | satellite -> ground | same format as `DATA`, sent during retry phase |
| `COMPLETE` | ground -> satellite | sidecar/ground has complete file and CRC/hash passed |
| `ABORT` | either | stop session and report reason |
| `STATUS` | either | compact counters/debug without text parsing |

Recommended `DATA` packet shape:

```text
magic          U8   fixed protocol marker
version        U8
type           U8   DATA
transfer_id    U16
chunk_index    U16 or U32
payload_len    U8
payload        up to remaining RF bytes
chunk_crc16    U16
```

If the RF max is `49` bytes, do not spend too much header. A practical MVP can use:

```text
magic       1 byte
type        1 byte
transfer_id 2 bytes
chunk_index 2 bytes
payload_len 1 byte
crc16       2 bytes
```

That is `9` bytes of header/trailer, leaving about `40` payload bytes per RF packet. For `40,368` bytes:

```text
40,368 / 40 = about 1,010 DATA packets
```

That is still reasonable if retry is selective and not whole-file restart.

### Reliability Strategy

Use selective repeat by transfer phase:

1. `META` phase
   - Satellite sends metadata repeatedly until ground ACKs the transfer ID.
   - Ground rejects stale or wrong transfer IDs.

2. First-pass data phase
   - Satellite sends all `DATA` chunks in order.
   - Ground verifies each packet CRC before marking the chunk received.
   - Ground stores chunks by `chunk_index`, not by arrival order.

3. Missing-map phase
   - Ground computes missing chunks.
   - Ground sends `NAK_BITMAP` messages listing missing chunks.
   - Satellite resends only missing chunks.

4. Retry rounds
   - Repeat missing-map/resend until all chunks are received or max retry rounds/time expires.
   - Recommended MVP max: `3-5` retry rounds, configurable.

5. Final verification
   - Sidecar reconstructs file.
   - Sidecar computes whole-file CRC32 or SHA-256.
   - Sidecar sends `COMPLETE` only if final check passes.
   - F Prime reports final success/failure as telemetry/event.

Avoid stop-and-wait per chunk unless the link is extremely unreliable. It is simple but slow. Avoid "send everything once and hope" unless the demo is purely illustrative. The best MVP balance is:

```text
burst all chunks -> request missing bitmap -> resend missing -> repeat -> final CRC
```

### Retry Bitmap Design

Use bitmap retry requests like EPSCOR.

For roughly `1,010` chunks, a full missing bitmap is:

```text
1,010 bits / 8 = about 127 bytes
```

That does not fit in one RF packet, so split it by window:

```text
NAK_BITMAP {
  transfer_id
  base_chunk_index
  bitmap_byte_count
  bitmap bytes
}
```

Example:

```text
base_chunk_index = 360
bitmap = 45 bytes = 360 chunks of coverage
```

A bit value of `1` means "please resend this chunk." This matches the EPSCOR-style approach and keeps retry requests compact.

### State Machines

Satellite Teensy transfer states:

```text
IDLE
WAIT_FILE_FROM_PI
SEND_META
SEND_DATA_BURST
WAIT_RETRY_REQUEST
RESEND_MISSING
WAIT_COMPLETE
DONE
ERROR
```

Ground Teensy/sidecar transfer states:

```text
IDLE
WAIT_META
RECEIVE_DATA
SEND_MISSING_MAP
RECEIVE_RETRY_DATA
VERIFY_FILE
COMPLETE
ERROR
```

F Prime mission state should stay high-level:

```text
IDLE
CAPTURING
PAYLOAD_READY
DOWNLINKING
DOWNLINK_COMPLETE
DOWNLINK_FAILED
```

Do not make F Prime track every RF chunk for the MVP. That would turn a reliable payload protocol problem into a noisy telemetry problem.

### Sidecar Program Behavior

The sidecar should be the source of truth for ground reconstruction.

Recommended sidecar features:

- open the ground Teensy sidecar/debug port
- parse only binary payload protocol messages
- maintain a chunk receipt table
- write chunks into a preallocated file buffer or sparse file
- display progress:
  - bytes received
  - chunks received
  - missing chunks
  - retry round
  - packet CRC failures
  - whole-file CRC/hash status
- save raw transfer logs for post-demo debugging
- export final payload file/image

For reliability, the sidecar should be able to restart a transfer cleanly:

- discard stale transfer IDs
- detect duplicate chunks
- tolerate chunks arriving out of order
- request missing chunks after timeouts
- fail with a clear reason if retry limit is exceeded

### Pi-To-Satellite Teensy Interface

Keep this simple for the MVP.

Recommended Pi-to-satellite Teensy messages:

```text
PAYLOAD_BEGIN {
  transfer_id
  file_size
  chunk_size
  file_crc32 or sha256
  product_id
}

PAYLOAD_BYTES {
  transfer_id
  offset
  length
  bytes
}

PAYLOAD_END {
  transfer_id
}
```

For a `40 KB` file, the satellite Teensy can cache the whole file and then control the RF session. This avoids having RF retry timing depend on Pi filesystem or UART timing.

If memory is tight later, change this to:

```text
satellite Teensy requests missing chunk from Pi -> Pi reads file chunk -> Teensy resends over RF
```

That is more scalable but more complicated.

### Failure Handling

Every failure should map to a small F Prime status/event and a sidecar log entry.

| Failure | Detection | Action |
| --- | --- | --- |
| metadata not ACKed | satellite timeout | resend META, then fail |
| chunk CRC fail | ground packet check | do not mark chunk received; request retry |
| missing chunks remain | sidecar missing map | send NAK bitmap |
| retry limit exceeded | sidecar/satellite counter | abort transfer, F Prime reports failed |
| whole-file CRC/hash fail | sidecar final verification | request full retry or fail |
| wrong transfer ID | ground or satellite parser | drop packet |
| duplicate chunk | sidecar receipt table | ignore or overwrite same bytes after CRC check |
| sidecar disconnected | ground USB write failure | abort payload data plane, keep GDS alive |

### What Not To Do

- Do not put raw payload bytes into `fprime-gds` CCSDS telemetry.
- Do not mix binary payload data and human debug text on the same sidecar stream.
- Do not restart the whole 40 KB transfer for one missing RF packet.
- Do not depend on arrival order.
- Do not treat "packet received by radio" as "file byte accepted"; verify CRC first.
- Do not make F Prime emit telemetry for every chunk unless needed for debugging.
- Do not use stock F Prime file downlink for the demo unless the RF link has already proven sustained low-loss delivery.

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
