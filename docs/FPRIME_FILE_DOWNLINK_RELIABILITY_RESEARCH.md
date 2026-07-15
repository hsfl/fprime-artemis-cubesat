# F Prime File Downlink Reliability Research

Date: 2026-07-01  
Status: research handoff  
Audience: Neutron 2 / Artemis F Prime SWEs evaluating stock F Prime file downlink versus the custom channel-1 payload downlink

## BLUF

Do not switch the RF MVP science product to stock F Prime `Svc.FileDownlink`
over the current RFM23BP link unless we also build a reliability overlay.

Stock F Prime file downlink gives structured file packets, CCSDS framing,
sequence/offset metadata, and buffer-return pacing. It does not provide an
end-to-end downlink retry loop where the GDS asks flight software to resend
missing file chunks. The stock GDS downlink receiver logs missing offsets and
unexpected sequence IDs, but it does not request repair and currently does not
enforce the file END hash before marking a downlink finished.

The current custom channel-1 payload path is still the right MVP science-data
transport for the small RFM23BP link because it has indexed packets, per-packet
CRC, retry bitmaps, and final file CRC verification.

If the team wants to keep more of the workflow "inside F Prime," the best next
experiment is not a wholesale replacement of the custom path. It is a
`Reliable FileDownlink` ground-side helper or GDS plugin that uses existing
F Prime hooks:

- `Svc.FileDownlink.SendFile` for first pass
- `Svc.FileDownlink.SendPartial` for range repair
- `Svc.FileManager.CalculateCrc` for flight-side source CRC comparison
- a patched/plugin GDS downlinker that tracks holes and verifies final CRC

That path may be useful upstream to F Prime/GDS because it turns existing
metadata into a generic lossy-link repair loop.

## Verified Source Context

This research was verified against the repo-local F Prime submodule, not generic
pretraining:

```text
ArtemisRpiTeensy_N2/lib/fprime = a750219414881d3461054e73c25a0868a5e648f9
version label = v3.1.1-1117-ga75021941
```

Primary files checked:

- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.fpp`
- `ArtemisRpiTeensy_N2/lib/fprime/Svc/Subtopologies/FileHandling/FileHandling.fpp`
- `ArtemisRpiTeensy_N2/lib/fprime/Svc/Subtopologies/FileHandling/docs/sdd.md`
- `ArtemisRpiTeensy_N2/lib/fprime/Svc/Subtopologies/ComCcsds/ComCcsds.fpp`
- `ArtemisRpiTeensy_N2/lib/fprime/Svc/Subtopologies/ComFprime/ComFprime.fpp`
- `ArtemisRpiTeensy_N2/lib/fprime/Svc/FileDownlink/FileDownlink.fpp`
- `ArtemisRpiTeensy_N2/lib/fprime/Svc/FileDownlink/FileDownlink.cpp`
- `ArtemisRpiTeensy_N2/lib/fprime/Fw/FilePacket/docs/sdd.md`
- `ArtemisRpiTeensy_N2/fprime-venv/lib/python3.10/site-packages/fprime_gds/common/files/downlinker.py`
- `ArtemisRpiTeensy_N2/lib/fprime/docs/reference/gds-plugins/*.md`
- `ArtemisRpiTeensy_N2/lib/fprime/docs/user-manual/gds/*.md`
- `ArtemisRpiTeensy_N2/Components/PayloadDownlinkApp/PayloadDownlinkApp.fpp`
- `ArtemisRpiTeensy_N2/Components/PayloadDownlinkApp/PayloadDownlinkApp.cpp`
- `ArtemisRpiTeensy_N2/tools/payload_receiver.py`
- `docs/SYSTEM_ARCHITECTURE.md`
- `external/epscorc3m/SatellitePayload/satellite_teensy/satellite_teensy.ino`
- `external/epscorc3m/dev-teensyGroundStation/ground_station_teensy/ground_station_teensy.ino`

## Correcting the "Zero-Copy Pipeline" Claim

Claim evaluated:

```text
Svc::BufferManager <-> Svc::FileDownlink -> Svc::FprimeRouter -> Svc::FprimeFramer
```

That claim is false for this deployment and partially misleading for stock
F Prime.

### What our deployment actually uses

Our active topology imports `ComCcsds.Subtopology`, not `ComFprime.Subtopology`:

```fpp
import CdhCore.Subtopology
import ComCcsds.Subtopology
import DataProducts.Subtopology
import FileHandling.Subtopology
```

For file downlink in this repo, the path is:

```text
FileHandling.fileDownlink.bufferSendOut
-> ComCcsds.comQueue.bufferQueueIn[ComCcsds.Ports_ComBufferQueue.FILE]
-> ComCcsds.spacePacketFramer
-> ComCcsds.aggregator
-> ComCcsds.framer          # Svc.Ccsds.TmFramer, not Svc.FprimeFramer
-> ComCcsds.comStub
-> uartChannelMux.ccsdsSendIn
-> UART channel 0
-> Teensy/RFM23BP bridge
-> fprime-gds
```

For file uplink, `ComCcsds.fprimeRouter` is on the receive side:

```text
GDS command/file bytes
-> UART channel 0
-> ComCcsds.comStub
-> ComCcsds.frameAccumulator
-> ComCcsds.tcDeframer
-> ComCcsds.spacePacketDeframer
-> ComCcsds.fprimeRouter
-> FileHandling.fileUplink.bufferSendIn
```

So `Svc.FprimeRouter` matters, but it is an uplink/router component. It is not
the downlink path after `FileDownlink`.

### What `ComFprime` would use

The alternative lightweight F Prime protocol subtopology has:

```text
ComFprime.comQueue
-> ComFprime.framer          # Svc.FprimeFramer
-> ComStub or custom Com driver
```

But that is not the active stack in this deployment.

### Why "strict zero-copy" is wrong

The actual pattern is buffer ownership and backpressure, not strict zero-copy.

Evidence:

- `Svc.FileDownlink` wraps its own internal memory store into `Fw::Buffer` in
  `getBuffer()`. It is not directly allocating each file packet from
  `Svc.BufferManager`.
- `Svc.FprimeFramer` documentation says it allocates a new output buffer and
  serializes the framed message into it.
- `Svc.FprimeRouter` explicitly copies file uplink buffers into newly allocated
  buffers before passing them to `FileUplink`.
- `Svc.ComQueue` stores queued `Fw::ComBuffer` / `Fw::Buffer` entries and uses
  status passback to control flow.

The important F Prime property is deterministic ownership return and flow
control. It is not "same bytes, same buffer, no copies" across the full pipe.

## What Stock File Handling Provides

The `FileHandling` subtopology packages:

- `Svc.FileUplink`
- `Svc.FileDownlink`
- `Svc.FileManager`
- `Svc.PrmDb`

The subtopology does not provide a complete comm stack by itself. The docs say
the file-packet ports must be wired to a communication/framing stack such as
`ComCcsds`, `ComFprime`, `FramingFprime`, or `FramingCcsds`.

`Fw.FilePacket` provides:

- packet type: `START`, `DATA`, `END`, `CANCEL`
- sequence index, starting at zero for each file
- `START`: file size, source path, destination path
- `DATA`: absolute byte offset, byte length, file data
- `END`: 32-bit CFDP-style hash/checksum

This is useful metadata for a repair protocol, but stock F Prime does not close
the repair loop by itself.

## What Stock FileDownlink Does Not Provide

`Svc.FileDownlink` sends:

```text
START -> DATA(offset 0) -> DATA(offset N) -> ... -> END
```

It waits for local buffer return before sending the next file packet. That local
buffer return is a downstream ownership/backpressure mechanism, not a ground
acknowledgement that the file packet arrived at GDS.

No stock `FileDownlink` port accepts:

- "missing offset X"
- "resend packet index Y"
- "ground verified hash"
- "ground ACK/NACK"

`FileDownlink.SendPartial` exists and is important:

```fpp
SendPartial(sourceFileName, destFilename, startOffset, length)
```

It can downlink a byte range from a source file. Since F Prime file DATA packets
carry absolute byte offsets and GDS writes chunks at their offsets, this can
be used as a manual or automated range-repair primitive.

But stock GDS does not automatically drive `SendPartial`.

## What Stock GDS File Downlink Does

The GDS `FileDownlinker`:

- receives START/DATA/END/CANCEL
- opens the destination file
- writes incoming DATA at the supplied offset
- warns when sequence ID is unexpected
- warns when an offset gap is observed
- increments its expected sequence
- marks the transfer finished on END

Observed limitation in the checked-out GDS code:

- On DATA, missing offsets produce log warnings, not retry commands.
- On END, the code comments that the `hashValue` attribute is not relevant
  right now.
- A transfer can be marked finished even if earlier DATA gaps occurred, as long
  as an END packet later arrives.

This is acceptable on a clean byte stream. It is not robust enough for our
current lossy, tiny-packet RF link without an added repair layer.

## Why the RFM23BP Link Makes Stock FileDownlink Risky

Current RF MVP constraints from the architecture and link constants:

```text
RFM23BP packet max:              49 bytes
Relay RF segment useful bytes:   44 bytes
Channel-1 payload data bytes:    35 bytes
CCSDS TM frame size:            128 bytes
FW_COM_BUFFER_MAX_SIZE:          96 bytes
```

That means one 128-byte CCSDS TM frame takes about three RF packets. If one RF
segment is not recovered before reassembly, the whole CCSDS frame fails CRC and
is dropped by the GDS side. Stock `FileDownlink` will not know that happened and
will continue sending later file DATA packets after local buffer return.

This failure mode is why APID sequence warnings and file holes are expected
under sustained downlink load on the current link.

Commands, command responses, events, and tiny SOH telemetry still belong on
CCSDS/channel 0. The risk is sustained byte-perfect bulk file transfer.

## Why the Current Channel-1 Path Exists

The current custom sidecar path is:

```text
PayloadDownlinkApp
-> uartChannelMux channel 1
-> satellite Teensy/RFM23BP
-> ground Teensy SerialUSB2
-> tools/payload_receiver.py
-> ground-station/neutron2-payload-viewer
```

It intentionally keeps science bytes outside the stock GDS CCSDS stream while
still using F Prime for command, state, progress telemetry, and events.

Current channel-1 packet design includes:

- fixed packet magic: `N2`
- packet types: header, data, end, retry-request
- transfer ID
- product ID
- total byte count
- total packet count
- per-data-packet CRC16
- whole-file CRC16
- retry bitmap from ground receiver to flight software
- selective resend of missing packet indexes

`PayloadDownlinkApp` exposes telemetry/events for operator visibility:

- `PayloadState`
- `TransferId`
- `ProductId`
- `TotalBytes`
- `TotalPackets`
- `PacketsSent`
- `ProgressPercent`
- `RetryRound`
- `PacketsMissing`
- `PayloadDownlinkProgress` (explicit `GET_PAYLOAD_STATUS` query only)
- `PayloadRetryRequested`
- `PayloadDownlinkComplete`

`tools/payload_receiver.py` is the actual ground-side reconstruction proof. GDS
logs such as `recv.bin` are UART artifacts, not proof of reconstructed payload
file success.

## EPSCOR/C3M Reference Pattern

The external EPSCOR/C3M reference does not use stock F Prime file downlink for
payload image transfer. It uses a packet/file-layer reliability protocol around
the radio:

- header packet with image length and total packets
- indexed data packets
- per-packet CRC16
- end packet with full image CRC16
- ground-side `packetReceived[]` tracking
- retry-request bitmap
- satellite `sendSpecificPacket(packetIndex)` from cached image bytes
- retry completion END packet

This is the same family of design as our channel-1 payload path and is better
matched to the RFM23BP packet budget than stock CCSDS file downlink.

## GDS Extension Surface From Repo-Local Docs

The repo-local GDS plugin and user-manual docs sharpen which "GDS plugin" ideas
are realistic.

### Data Handler plugin

`docs/reference/gds-plugins/data-handler.md` says data-handler plugins register
custom consumers for decoded F Prime data and run in a `CustomDataHandler`
process, separate from the core GDS runtime.

Important detail: descriptor strings include:

```text
FW_PACKET_TELEM
FW_PACKET_LOG
FW_PACKET_FILE
FW_PACKET_PACKETIZED_TLM
```

Implication for file downlink repair:

- A data-handler plugin can subscribe to `FW_PACKET_FILE`.
- It is the right hook to observe stock FileDownlink START/DATA/END packets.
- It can track sequence/offset gaps without blocking the main communication
  thread.
- It can publish derived ground-processed channels through
  `self.publisher.publish_channel(...)`, useful for dashboard-visible repair
  state such as holes, retry rounds, and verified/incomplete status.

Limitation:

- The data-handler interface is primarily a decoded-data consumer. By itself,
  it is not clearly the command-sending control loop.

### GDS App plugin

`docs/reference/gds-plugins/gds-app.md` says `GdsApp` plugins run separate
processes and `GdsStandardApp` wires up the standard GDS pipeline. The example
uses `pipeline.send_command(...)`.

Implication:

- A `GdsStandardApp` is the best documented plugin shape for a repair controller
  that needs to both observe GDS data and issue `SendPartial` or
  `CalculateCrc` commands.
- It isolates repair logic from the main GDS communication process.
- It can be a project-local app first, then potentially an upstreamable plugin.

Likely architecture:

```text
DataHandler(FW_PACKET_FILE) or StandardPipeline consumer
-> missing-range tracker
-> GdsStandardApp repair controller
-> pipeline.send_command("FileHandling.fileDownlink.SendPartial", ...)
-> pipeline.send_command("FileHandling.fileManager.CalculateCrc", ...)
-> publish repair/verified status as ground-processed channels
```

### Communication plugin

`docs/reference/gds-plugins/communications.md` says communication plugins swap
the raw byte I/O mechanism and run in the GDS communications process. Only one
communication plugin is selected.

Implication:

- This is the right hook only if we replace the UART/IP/radio driver itself.
- It is not the first place to implement file repair.
- Blocking or slow I/O here delays the rest of GDS, so repair logic does not
  belong here.

### Framing plugin

`docs/reference/gds-plugins/framing.md` says framing plugins assemble/extract
F Prime packets from protocol-specific frames, run inside the GDS communication
thread, and are selection plugins.

Implication:

- This is useful for custom CCSDS/F Prime framing or a future radio-specific
  framing stack.
- It is too invasive for a FileDownlink repair overlay.
- Poor implementation can delay all communications.

### GDS Function plugin

`docs/reference/gds-plugins/gds-function.md` says function plugins run once at
startup in the main GDS process.

Implication:

- Useful for startup wiring or launching helper processes.
- Not appropriate for a long-running repair loop by itself.

### Dashboards

`docs/user-manual/gds/gds-custom-dashboards.md` and
`gds-dashboard-reference.md` say dashboards support:

- `command-input`
- `command-history`
- `event-list`
- `channel-table`
- `logging`

The dashboard reference explicitly says file handling components are not
currently supported in dashboards and are better suited for a full view.

Implication:

- A dashboard can show our existing `PayloadDownlinkApp` telemetry/events.
- A dashboard can show repair-helper status if it is published as channels or
  events.
- A dashboard cannot, by itself, become a custom File Downlink/repair tab.

### CLI and Integration Test API

`docs/user-manual/gds/gds-cli.md` says `fprime-cli` supports:

- `channels`
- `events`
- `command-send`

It can send `SendFile`, `SendPartial`, and `CalculateCrc` as normal commands,
and it can filter events/channels. The developer guide lists CLI file
uplink/downlink tools as future work, so do not assume a ready-made file-repair
CLI exists in this F Prime version.

`docs/user-manual/gds/gds-test-api-guide.md` documents command send and
await/assert patterns for telemetry and events. This is a good fit for
regression testing a repair helper once the logic exists.

Implication:

- First prototype can be a standalone Python helper around `fprime-cli
  command-send` plus parsing GDS file logs, but this is crude.
- Better prototype can use the GDS Integration Test API or `GdsStandardApp`
  pipeline directly to send commands and observe results.
- HIL regression should assert command responses, `FileDownlink` telemetry,
  repair-helper derived channels/events, and final CRC match.

## Integration Options

### Option 1: Keep Custom Channel-1 Payload Downlink

Recommended for the current RF MVP.

Use F Prime for:

- commands
- mission state
- capture/store orchestration
- payload downlink progress telemetry
- success/failure events

Use channel 1 for:

- bulk science bytes
- packet indexing
- retry requests
- file reconstruction
- final CRC verification

Pros:

- already implemented
- designed around 44-byte RF useful payload
- aligns with the EPSCOR/C3M proven pattern
- has real packet-level retry and final-file verification
- does not corrupt or overload the GDS CCSDS stream

Cons:

- final file does not appear in the stock GDS File Downlink tab
- requires a companion receiver/viewer process
- looks less "pure F Prime" to reviewers unless the architecture is explained

### Option 2: Mostly-Stock F Prime Reliable FileDownlink Overlay

Best medium-term experiment if the team wants the GDS File Downlink workflow.

Flow:

```text
1. Command FileDownlink.SendFile(source, dest)
2. GDS/helper receives stock file packets
3. Helper tracks missing byte ranges from sequence/offset gaps
4. Helper commands FileDownlink.SendPartial(source, dest, offset, length)
5. Helper repeats until no holes remain
6. Helper commands FileManager.CalculateCrc(source)
7. Helper computes ground CRC and compares against flight/source CRC
8. Only then mark transfer verified
```

This could be implemented as:

- a standalone Python helper that talks to `fprime-gds`/`fprime-cli`
- a GDS `DataHandlerPlugin` subscribing to `FW_PACKET_FILE` for hole tracking
- a `GdsStandardApp` repair controller that sends `SendPartial` and
  `CalculateCrc`
- a local patch to `fprime_gds.common.files.downlinker`
- a custom GDS full view or app; the XML dashboard alone is not enough
- an upstream F Prime GDS feature proposal

Pros:

- reuses stock `Svc.FileDownlink`
- reuses stock `SendPartial`
- reuses stock `Svc.FileManager.CalculateCrc`
- makes the existing GDS File Downlink tab more useful on lossy links
- potential upstream contribution

Cons:

- still rides the fragile CCSDS/RFM23BP channel 0 path
- repair commands consume the same link budget
- needs careful state tracking when START/END packets are also dropped
- requires GDS/plugin customization anyway
- cannot be implemented as dashboard XML alone
- likely slower than channel 1 for larger files

Minimum acceptance criteria:

- Downlinker must track holes explicitly, not just log warnings.
- END must not mark verified success while holes exist.
- Final success must require ground CRC match against flight/source CRC.
- Retry must be bounded by max rounds, max bytes, and operator abort.
- The helper must surface incomplete/failed status clearly in GDS/events.

### Option 3: Custom F Prime ReliableFileDownlink Component

Build a project component that is F Prime-native but not stock `FileDownlink`.

Shape:

```text
ReliableFileDownlink
  commands:
    START_RELIABLE_DOWNLINK(source, product_id)
    REQUEST_MISSING(transfer_id, start_index, bitmap)
    ABORT
    STATUS
  events/telemetry:
    started/progress/retry/verified/failed
  data path:
    either channel 1 sidecar or custom ComCcsds APID
```

This is close to what `PayloadDownlinkApp` already is. The novel part would
be making the component generic over files and documenting it as a reusable
F Prime pattern rather than Neutron-specific payload plumbing.

Pros:

- clean F Prime component boundary
- preserves our working ARQ design
- can remain radio-agnostic
- easier to test than modifying stock `FileDownlink`

Cons:

- not stock GDS File Downlink
- still needs custom ground receiver/plugin
- if routed through CCSDS, it inherits the channel-0 loss/overhead problem

### Option 4: Custom Framing/Protocol Stack

F Prime docs explicitly support custom framing by replacing the standard
`ComCcsds`/`ComFprime` pieces with custom framer/deframer components plus GDS
integration.

This is the heavy option.

Pros:

- could make the whole comm stack match the small radio better
- may eliminate some CCSDS overhead
- could support link-layer repair below F Prime packets

Cons:

- high integration risk
- GDS plugin required
- easy to break command/event/telemetry stability
- too much scope for the current MVP

Use only after the current demo path is frozen and a better radio path is known.

## Novel Work Worth Considering

### 1. GDS "Repairing FileDownlink" Plugin

Most promising F Prime-facing contribution.

Core idea:

- keep stock `Svc.FileDownlink`
- observe file packet sequence/offset gaps via `FW_PACKET_FILE`
- maintain missing-range set
- issue `SendPartial` automatically
- verify file with CRC before marking success
- expose transfer status in the GDS UI

Why this is novel enough to matter:

- F Prime already has the metadata and `SendPartial` primitive.
- The missing piece is the ground-side repair loop.
- This would benefit any mission using F Prime GDS over a lossy or intermittent
  link, not just Neutron 2.
- The plugin should start as a `DataHandlerPlugin` plus `GdsStandardApp`, not a
  communication/framing plugin.

### 2. Patch GDS Downlinker Hash Handling

Near-term improvement:

- stop treating END as successful if sequence/offset gaps were observed
- use the END hash/CRC field instead of ignoring it
- mark incomplete transfers as incomplete, not finished

This is smaller than full repair but makes stock behavior safer.

### 3. Genericize `PayloadDownlinkApp`

Current channel-1 path is payload-oriented, but the protocol is really a
generic reliable blob downlink:

```text
source path -> fixed-size indexed chunks -> retry bitmap -> verified blob
```

Rename/refactor target after MVP:

- `ReliableBlobDownlink`
- generic source descriptor
- optional file source
- optional payload-specific viewer outside the core protocol

This keeps the working MVP path and makes it reusable for future student teams.

### 4. F Prime-Friendly Ground Bridge

Instead of forcing all science bytes into the stock GDS File Downlink tab, build
a small GDS plugin that reads the channel-1 receiver's status/output and renders
it inside GDS:

- progress
- retry rounds
- missing packet count
- final CRC
- output file path
- viewer launch link or parsed preview

This gives operators a single UI without forcing bulk data through channel 0.

## Recommendation

For the current Neutron 2 RF MVP:

1. Keep channel 0 for commands, events, telemetry, and tiny status.
2. Keep channel 1 for science payload bytes.
3. Treat `payload_receiver.py` completion plus CRC/hash match as the file proof.
4. Do not claim success from GDS events alone.
5. Document the path as "F Prime-managed, custom reliable bulk-data sidecar."

For post-MVP / possible contribution work:

1. Prototype a standalone `file_downlink_repair.py` helper using stock
   `SendFile`, `SendPartial`, and `CalculateCrc`.
2. Test it on a local loopback or intentionally lossy UART/GDS setup before RF.
3. If it works, turn it into a GDS plugin or upstream patch.
4. Keep the custom channel-1 path as the flight-demo fallback until the repair
   helper survives HIL with real RFM23BP loss.

## Questions for the Team

- Is the goal to make judges see "file downlink" inside stock GDS, or to make
  the science file arrive reliably?
- What file sizes are actually expected for the demo and for the real payload?
- Are we willing to patch/extend GDS, or do we need to stay stock for judging?
- Should `PayloadDownlinkApp` be renamed/genericized after MVP so the
  architecture reads as a reusable reliable blob service?
- Is there appetite to upstream a GDS FileDownlink repair plugin once the demo
  pressure is gone?

## Practical Decision

The clean architectural line is:

```text
F Prime owns mission intent and operator-visible state.
The radio-aware reliability layer owns bulk science bytes.
```

Trying to make stock `Svc.FileDownlink` reliable over this radio is possible,
but only after customization on the ground side. Once we customize anyway, the
question becomes which customization has lower risk:

- for MVP: current channel-1 ARQ path
- for future F Prime polish: GDS/FileDownlink repair helper
