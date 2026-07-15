# C3M Downlink Protocol v2 — Design Proposal

## Status

**Design only — not implemented, approved, flashed, or HIL-qualified.** This
document is a concrete proposal for WP3–WP6 of the
[RF reliability hardening plan](C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md).
It does not mark any work package or acceptance gate complete.

Repository history contains no implemented `N1` payload wire protocol. The
current `N2` magic was the first custom channel-1 payload protocol and appears
to have been named for the Neutron-2 project. This successor design is named
**C3M Downlink Protocol v2**, abbreviated **C3M-DL v2**, with proposed two-byte
wire magic `C2` (`0x43 0x32`). It is the second protocol design in this lineage,
not a Neutron-3 mission or a third implemented generation.

## BLUF

C3M-DL v2 keeps channel 0 as the control plane and channel 1 as bulk payload,
but adds the contracts missing from N2:

- a restart-aware transfer identity
- distinct transmit and ground-verified completion
- whole-product CRC-32 confirmation plus a final ACK
- additive, idempotent selective repair
- bounded half-duplex ownership windows
- explicit expiry and honest partial outcomes

C3M-DL v2 uses distinct `C2` magic and remains within the existing 44-byte RF
segment payload. The ground receiver must decode N2 and C3M-DL v2 before flight
is allowed to transmit C3M-DL v2. N2 completion must be labeled
`LEGACY_UNVERIFIED_COMPLETE`; it must never be presented as C3M-DL v2
ground-verified completion.

## Invariants

1. Channel `0` remains bounded command, event, and telemetry traffic.
2. Channel `1` carries C3M-DL v2 payload data, status, repair, and final
   confirmation.
3. A packet or repair bit is retired only after the local copy-and-status output
   reports `LOCAL_ACCEPTED`; enqueue or attempted transmission is not local
   acceptance and local acceptance is not RF delivery.
4. `TRANSMIT_COMPLETE` means the nominal packet set was accepted by the local
   output path. It does not mean ground received or verified the product.
5. Only an exact, valid ground CRC confirmation can enter
   `GROUND_VERIFIED_COMPLETE`.
6. Product bytes and transfer metadata remain retained until verified
   completion or explicit expiry/abort.
7. A restart, stale packet, duplicate request, lost status, or overlapping
   repair request has one deterministic outcome.
8. Lease or state uncertainty fails back to `CONTROL` with channel 1 held; it
   never allows both RF peers to assume transmit ownership.

## Wire Encoding

All multibyte integers are little-endian. Packet lengths are exact and
variable; unused bytes are not transmitted. Every packet ends with CRC-16 over
all preceding bytes in that packet.

### Checksums

| Use | Algorithm | Parameters |
| --- | --- | --- |
| Per-packet integrity | CRC-16/CCITT-FALSE | polynomial `0x1021`, init `0xFFFF`, refin/refout false, xorout `0x0000` |
| Whole-product proof | CRC-32/IEEE (`CRC-32/ISO-HDLC`) | reflected polynomial `0xEDB88320`, init `0xFFFFFFFF`, refin/refout true, xorout `0xFFFFFFFF`; `"123456789" -> 0xCBF43926` |

CRC-32 is computed over exactly `totalBytes` source bytes in ascending byte
order. It replaces the current whole-product CRC-16 as the C3M-DL v2 identity
and completion proof. Packet CRC-16 remains local corruption detection.

### Common prefix

Every C3M-DL v2 packet begins with this six-byte prefix:

| Offset | Size | Field | Value |
| ---: | ---: | --- | --- |
| 0 | 2 | `magic` | `0x43 0x32` (`C2`) |
| 2 | 1 | `type` | packet type below |
| 3 | 2 | `bootEpoch` | persistent flight boot epoch |
| 5 | 1 | `transferId` | transfer number within the epoch |

Proposed type assignments:

| Value | Type | Direction |
| ---: | --- | --- |
| `0x01` | `HEADER` | flight to ground |
| `0x02` | `DATA` | flight to ground |
| `0x03` | `TRANSMIT_STATUS` | flight to ground |
| `0x04` | `GROUND_STATUS` | ground to flight |
| `0x05` | `RETRY_REQUEST` | ground to flight |
| `0x06` | `FINAL_CRC_CONFIRM` | ground to flight |
| `0x07` | `FINAL_ACK` | flight to ground |
| `0x08` | `ABORT_STATUS` | flight to ground |
| `0x09` | `PHASE_CONTROL` | bidirectional |
| `0x0A` | `RESTART_STATUS` | flight to ground |

These numeric assignments are proposed constants and require approval before
they become an ICD.

### Canonical identity

The complete identity is:

```text
(bootEpoch, transferId, productId, totalBytes, wholeCrc32)
```

`bootEpoch` increments once, durably, before flight communications start after
each process/flight restart. It may also be advanced durably when the current
epoch exhausts its transfer IDs. `transferId` is nonzero and increments for
each accepted transfer in that epoch. Full tuple equality, not `transferId`
alone, defines a duplicate or current transfer.

- A repeated `HEADER` with the same tuple is idempotent.
- The same `(bootEpoch, transferId)` with different product fields is a
  conflict and is quarantined, not merged.
- A packet from a nonmatching epoch cannot mutate the active transfer. On-wire
  epoch comparison is equality-only; implementations must not infer chronological
  "older" or "future" ordering across `U16` wrap.
- After a flight restart, the new epoch begins transmission at packet `0`.
  Ground preserves the old epoch as an honest partial session and opens a new
  session; it never splices bytes across epochs.
- A durably verified tuple is retained and continues answering repeated
  confirmations until the product attempt's absolute lifetime expires. This
  ACK-service tombstone survives process restart and does not block admission
  of a different full identity.

`RETRY_REQUEST` omits the product fields to preserve a 30-byte bitmap. It is
accepted only when its prefix matches the active epoch/transfer and its
`requestId` was announced by a matching full-identity `GROUND_STATUS` in the
current ground-owned window.

### Encoded bounds and allocation

- `totalPackets` and `packetIndex` are `U16`; the wire encoding can represent at
  most `65,535` packets and `2,162,655` bytes at 33 bytes per packet. One
  canonical repair snapshot can announce at most 255 pages of 240 packet
  positions, so the approved configured maximum is at most `61,200` packets
  and `2,019,600` bytes unless a separate multi-request snapshot partition is
  specified and approved. The expected operational maximum is substantially
  smaller.
- `transferId` allocation is `1..255` with no reuse in one epoch. Before a
  256th transfer is accepted, flight must atomically advance `bootEpoch` or
  inhibit C3M-DL v2; it must never silently wrap the transfer ID.
- `bootEpoch` is compared only for equality. C3M-DL v2 is inhibited before an epoch
  value would be reused unless an approved namespace-rotation policy or wider
  field has been deployed.
- `requestId`, `confirmId`, and `leaseId` are nonzero. They are not reused while
  their transfer or durable tombstone remains live. Exhaustion is an explicit
  error, not silent wrap.

### Durable flight checkpoint

Flight restart recovery requires more than a persistent epoch counter. Before
the first C3M-DL v2 packet of an accepted product, flight atomically checkpoints:

- schema version and complete identity
- source descriptor, transfer-owned immutable source identity, canonical source
  path, size, CRC-32, and a source-file fingerprint sufficient to detect
  replacement
- original accepted wall time, accumulated active elapsed time, and clock
  domain identifier
- transfer state, nominal cursor, transmitted/verified milestones, current
  lease/request/confirm IDs, and pending repair bitset
- terminal reason or verified-tombstone state when applicable

Before transmission, flight creates or validates a transfer-owned immutable
source snapshot, or otherwise proves that the retained source cannot change for
the transfer lifetime. Flight never streams a mutable path under an already
published CRC identity. A later source identity, size, or fingerprint mismatch
enters `ERROR` and cannot advance a transmit cursor or verified state.

Checkpoint updates use write-temporary, file sync, atomic rename, and
parent-directory sync where the platform supports them. Restart reads and
validates the prior checkpoint before constructing any recovery RF evidence,
while `bootEpoch` is still advanced durably before any communication. A valid
active checkpoint creates a new identity in the new epoch, emits
`RESTART_STATUS`, and restarts nominal transmission at packet 0. The old ground
identity remains an honest partial. Epoch gaps caused by crashes during this
sequence are allowed; epoch reuse is not.

If the complete old identity is recoverable but source or time continuity
fails, flight persists an identity-associated terminal recovery record. If a
missing or corrupt checkpoint cannot authenticate the old identity, no RF
packet may invent one: flight records a local durable terminal error and the
unknown ground session expires honestly. Restart does not reset the product
attempt's absolute lifetime; if elapsed lifetime cannot be established
conservatively, flight fails closed to `PARTIAL_EXPIRED`.

### Packet layouts

`crc16` is always the last two bytes and covers the prefix and all fields/data
before it.

#### `HEADER` — 23 bytes

```text
common[6] | productId U32 | totalBytes U32 | totalPackets U16 |
dataBytes U8 (=33) | wholeCrc32 U32 | crc16 U16
```

The receiver rejects a header when `totalPackets != ceil(totalBytes/33)`, the
declared size exceeds the approved product limit, or the tuple conflicts with
an active session.

#### `DATA` — 12 to 44 bytes

```text
common[6] | packetIndex U16 | validLen U8 | data[validLen, 1..33] |
crc16 U16
```

`packetIndex * 33` is the destination byte offset. Non-final packets require
`validLen == 33`; the final packet requires the exact remaining length. A
duplicate packet with identical bytes is counted and ignored. A duplicate with
different bytes is a session error and must not overwrite persisted data.

The 33-byte payload is two bytes smaller than N2 because C3M-DL v2 adds
`bootEpoch`. A 38,480-byte C3M product therefore uses 1,167 data packets rather
than 1,100.

#### `TRANSMIT_STATUS` — 27 bytes

```text
common[6] | productId U32 | totalBytes U32 | wholeCrc32 U32 |
totalPackets U16 | nominalPacketsAccepted U16 | pendingRepairCount U16 |
statusCode U8 | crc16 U16
```

`statusCode` distinguishes at least `NOMINAL_IN_PROGRESS`,
`TRANSMIT_COMPLETE`, `REPAIR_IN_PROGRESS`, and `WAIT_GROUND_STATUS`. Flight
repeats `HEADER` and `TRANSMIT_STATUS` in recovery/status cycles; it does not
assume a single END marker arrived.

#### `GROUND_STATUS` — 28 bytes

```text
common[6] | productId U32 | totalBytes U32 | wholeCrc32 U32 |
requestId U16 | receivedPackets U16 | missingPackets U16 |
retryPageCount U8 | receiverStatus U8 | crc16 U16
```

`receiverStatus` distinguishes at least `RECEIVING`, `NEEDS_REPAIR`,
`READY_TO_VERIFY`, `CRC_MISMATCH`, and `PARTIAL_SAVED`. The complete identity
must match before flight associates later `RETRY_REQUEST` frames with
`requestId`. `retryPageCount` is zero unless this status announces a repair
snapshot. For `NEEDS_REPAIR`, it is the exact nonzero number of request pages
that follow in the same ground-owned window.

#### `RETRY_REQUEST` — 15 to 44 bytes

```text
common[6] | requestId U16 | pageIndex U8 | startIndex U16 | bitmapBytes U8 |
missingBitmap[bitmapBytes, 1..30] | crc16 U16
```

Bit `n` requests packet `startIndex + n`; set means missing. Multiple frames
with the same `requestId` describe one repair snapshot. `pageIndex` is in
`0..retryPageCount-1`; `(requestId, pageIndex)` duplicates must be byte-exact.
The canonical layout requires `startIndex == pageIndex * 240`; pages are
nonoverlapping; every non-final page uses 30 bitmap bytes; the final page uses
the exact remaining bytes; and unused high bits are zero. At 30 bitmap bytes
per page, the 1,167-packet C3M product requires five pages when all packet
positions are represented.

Flight remains in the ground-owned window until all announced pages and a valid
`YIELD(REPAIR)` arrive. It ORs every valid page into pending repair work
immediately, but it never treats receipt of the first page as snapshot
completion. An incomplete snapshot is retransmitted with the same `requestId`,
page count, and byte-exact pages. A new request ID begins a new snapshot but
never clears pending bits accumulated from earlier snapshots. Later request
IDs may overlap earlier work and are merged, never substituted.

#### `FINAL_CRC_CONFIRM` — 29 bytes

```text
common[6] | productId U32 | totalBytes U32 | expectedCrc32 U32 |
actualCrc32 U32 | confirmId U16 | receivedPackets U16 |
confirmStatus U8 | crc16 U16
```

Flight accepts confirmation only when:

- the complete tuple matches the retained transfer
- `confirmStatus == CRC_OK`
- `receivedPackets == totalPackets`
- `actualCrc32 == expectedCrc32 == retained wholeCrc32`

Ground sends the same valid confirmation three times per attempt until it sees
a valid `FINAL_ACK` or the transfer expires. Duplicate confirmations are
idempotent.

#### `FINAL_ACK` — 23 bytes

```text
common[6] | productId U32 | totalBytes U32 | wholeCrc32 U32 |
confirmId U16 | ackCode U8 | crc16 U16
```

On an accepted confirmation, flight enters `GROUND_VERIFIED_COMPLETE`,
durably records the verified tombstone, and sends the same ACK three times.
Ground records complete after the first valid matching ACK. The tombstone also
schedules bounded `FINAL_ACK` leases until the product attempt expires, so loss
of all three initial ACK copies is recoverable without changing verification
state. Ground in either `CRC_VALID_ACK_PENDING` or `COMPLETE` grants a matching
tombstone ACK lease and treats repeated ACKs idempotently.

#### `ABORT_STATUS` — 27 bytes

```text
common[6] | productId U32 | totalBytes U32 | wholeCrc32 U32 |
outcome U8 | reason U16 | detail U32 | crc16 U16
```

`outcome` distinguishes terminal `PARTIAL_EXPIRED`, `ABORTED`, and `ERROR`
outcomes. Ground persists the terminal reason with the partial artifact.

#### `PHASE_CONTROL` — 26 bytes

```text
common[6] | productId U32 | totalBytes U32 | wholeCrc32 U32 |
leaseId U16 | phase U8 | action U8 | leaseMs U16 | crc16 U16
```

This is the only channel-1 packet allowed while a bridge is holding channel 1
in `CONTROL`. The full identity, CRC, nonzero `leaseId`, legal phase/action
pair, and locally allowed transition must all validate before either bridge
changes ownership. Proposed actions are `REQUEST`, `GRANT`, `YIELD`, and
`CANCEL`. One flight-allocated `leaseId` identifies one legal
request/grant/yield phase chain until `CONTROL`, cancellation, conflict, or
expiry. Different expected actions in that legal chain are not conflicting
reuse. Duplicate byte-exact actions are idempotent. A different identity,
unexpected action, unexpected target phase, or incorrect configured phase
bound using the same ID is a conflict that returns both peers to `CONTROL` and
is counted. A terminated ID is not reused while its transfer or durable
tombstone remains live.

#### `RESTART_STATUS` — 25 bytes

```text
common[6] | previousBootEpoch U16 | previousTransferId U8 |
productId U32 | totalBytes U32 | wholeCrc32 U32 | reason U16 | crc16 U16
```

The common prefix carries the new identity. The remaining fields identify the
prior ground session that must be closed as partial. This status is repeated in
bounded metadata slots until a matching `GROUND_STATUS` proves that ground has
opened the new identity or the transfer expires; it is recovery evidence, not
an abort or terminal outcome.

Proposed one-byte field codes:

| Field | Values |
| --- | --- |
| `statusCode` | `0=NOMINAL_IN_PROGRESS`, `1=TRANSMIT_COMPLETE`, `2=REPAIR_IN_PROGRESS`, `3=WAIT_GROUND_STATUS` |
| `receiverStatus` | `0=RECEIVING`, `1=NEEDS_REPAIR`, `2=READY_TO_VERIFY`, `3=CRC_MISMATCH`, `4=PARTIAL_SAVED` |
| `confirmStatus` | `1=CRC_OK`; all other values are invalid/reserved |
| `ackCode` | `0=VERIFIED` |
| `outcome` | `1=PARTIAL_EXPIRED`, `2=ABORTED`, `3=ERROR` |
| `phase` | `1=PAYLOAD_BURST`, `2=GROUND_STATUS`, `3=REPAIR`, `4=FINAL_CONFIRM`, `5=FINAL_ACK` |
| `action` | `1=REQUEST`, `2=GRANT`, `3=YIELD`, `4=CANCEL` |

Unknown values are counted and ignored; they never cause a state advance.

## Flight Transfer State Machine

```text
IDLE
  -> PREPARING
  -> NOMINAL_TX
  -> TRANSMIT_COMPLETE
  -> WAIT_GROUND_STATUS
       -> REPAIR_TX -> WAIT_GROUND_STATUS
       -> GROUND_VERIFIED_COMPLETE -> IDLE

Recovery/terminal states:
  RECOVERING_AFTER_RESTART
  PARTIAL_EXPIRED
  ABORTED
  ERROR
```

| State | Meaning and permitted exit |
| --- | --- |
| `IDLE` | No retained active transfer; accept one new descriptor. |
| `PREPARING` | Validate source, compute CRC-32, allocate identity/bitmap, retain metadata. Failure enters `ERROR`. |
| `NOMINAL_TX` | Send bounded bursts from packet 0. Duplicate same-tuple starts are idempotent; conflicting starts are rejected. |
| `TRANSMIT_COMPLETE` | All nominal packets received downstream `LOCAL_ACCEPTED`; product remains retained. Immediately proceed to a status window. |
| `WAIT_GROUND_STATUS` | Repeat header/status and await matching ground status, repair, or confirmation. |
| `REPAIR_TX` | Service the additive bitset in bounded round-robin bursts; return to a ground window. |
| `GROUND_VERIFIED_COMPLETE` | Exact final CRC confirmation accepted; notify `CommsApp`, persist a verified ACK-service tombstone, and send three ACKs. Source data may be released after the tombstone is durable. A different identity may be admitted while duplicate confirmations for the tombstone continue receiving ACKs. |
| `RECOVERING_AFTER_RESTART` | Increment epoch, load and validate the durable active checkpoint, advertise the old-to-new identity transition, then retransmit from packet 0. Exit to `PREPARING`/`NOMINAL_TX`, or to one terminal state on validation/expiry failure. |
| `PARTIAL_EXPIRED` | Inactivity or absolute product-attempt lifetime expired before verification. Persist/emit the terminal reason, release active ownership after evidence is durable, then permit an explicit operator reset or new descriptor to return to `IDLE`. |
| `ABORTED` | Explicit operator/system abort. Persist/emit the reason, preserve the source according to retention policy, then permit explicit reset or a new descriptor to return to `IDLE`. |
| `ERROR` | Local invariant, source, identity, allocation, or persistent I/O failure. Persist/emit the reason; no false completion. Recovery requires explicit reset or a new descriptor after the failed identity is durably closed. |

`CommsApp` must clear its active product and emit mission downlink completion
only on `GROUND_VERIFIED_COMPLETE`. Transmit completion is independently
observable and cannot advance the mission to verified completion.

## Ground Receiver State

Transport recovery and transfer integrity are separate state dimensions. A
USB loss must not reset the transfer state.

```text
Transport: DISCONNECTED -> DISCOVERING -> OPENING -> READY
                         -> BACKPRESSURED -> READY
                         -> RECOVERING -> DISCOVERING
                         -> FAILED

Transfer:  IDLE -> RECEIVING -> REQUESTING_REPAIR -> RECEIVING
                         -> VERIFYING -> CRC_VALID_ACK_PENDING -> COMPLETE
                         -> CRC_FAILED
                         -> PARTIAL_SAVED
```

- The receiver persists the full identity, header, bitmap, packet bytes,
  request/confirm IDs, retry round, and timestamps incrementally.
- Reopening a re-enumerated stable device identity resumes the same transfer;
  a path string alone is not identity.
- `VERIFYING` requires every indexed packet and exact CRC-32 equality.
- `CRC_VALID_ACK_PENDING` means the complete product is locally CRC-32-valid
  and confirmation has been sent, but flight acknowledgement is not yet proven.
  It repeats `FINAL_CRC_CONFIRM` in bounded attempts. Loss of the receiver
  process reloads this state rather than fabricating flight acknowledgement.
- `COMPLETE` requires both local CRC-32 equality and a matching `FINAL_ACK`.
- Expiry or unrecoverable I/O before CRC equality writes `.fdp.partial`, a
  missing map, identity, byte positions, and an explicit failure reason.
  Expiry in `CRC_VALID_ACK_PENDING` preserves the complete CRC-valid product
  with terminal status `CRC_VALID_FLIGHT_ACK_UNCONFIRMED`; it must not relabel
  valid bytes as partial or claim that flight acknowledged them.

### Time and retention

- Inactivity expiry: 10 minutes without a valid identity-matching peer packet,
  successful new local delivery, or repair progress. Byte-for-byte duplicate
  data does not count as delivery progress, but a valid peer status or phase
  heartbeat does prove link liveness and resets inactivity.
- Absolute product-attempt lifetime: 30 minutes from the first accepted
  descriptor; activity and process/flight restart cannot extend it.
- Verified ACK-service retention: until the product attempt's absolute lifetime
  expires, including across flight restart. Ground retains
  `CRC_VALID_ACK_PENDING` for the same bound.
- Every timeout transitions once, emits a reason, and leaves a durable ground
  result: `PARTIAL`, `EXPIRED`, `ABORTED`, or
  `CRC_VALID_FLIGHT_ACK_UNCONFIRMED` as appropriate.

Each durable timestamp is a triple: `clockDomainId`, monotonic time in that
domain, and UTC/wall time plus a validity flag. Monotonic values are used for
live deadlines only and are never compared across clock domains or process
restarts. Cross-restart absolute age is reconstructed from the durable accepted
UTC and accumulated persisted elapsed time. If neither can establish a safe
upper bound, the transfer expires rather than receiving fresh lifetime.

## Additive Repair Contract

Flight owns a fixed bitset with one bit per possible packet in the approved
maximum product. A valid repair request is processed as follows:

1. Validate packet CRC, full active identity association, current-window
   `requestId`, announced page count, page index, bounds, and bitmap length.
2. Record each byte-exact page idempotently and OR all valid requested bits into
   the pending bitset. A conflicting duplicate page invalidates that snapshot
   but does not clear already valid pending work.
3. Count received/missing/duplicate/conflicting pages plus newly set, already
   set, and out-of-range bits separately.
4. Stay in the ground window until every announced page arrives and a valid
   `YIELD(REPAIR)` is received. Lease expiry without that yield returns to
   `CONTROL` and preserves all accumulated pending bits. Flight then opens a
   fresh metadata/status cycle so ground can retransmit the same incomplete
   snapshot; expiry alone never authorizes repair transmission.
5. Clear a bit only when the channel-1 output operation returns
   `LOCAL_ACCEPTED`.
6. Send the accumulated set bits round-robin so repeatedly requested low
   indices cannot starve later indices, then return to a ground status window
   after each bounded repair burst.

`LOCAL_ACCEPTED` means only that the complete UART frame was accepted by the
local copy-and-status output boundary. It is not proof that the satellite
Teensy, radio, or ground received the frame. A local acceptance clears flight's
pending-send bit, not ground's missing bit. If the RF frame is lost afterward,
ground requests that index again and the OR operation restores it. Duplicate
and overlapping requests therefore remain safe and idempotent.

Malformed, stale, or out-of-range requests are counted and ignored. They do not
clear valid pending work or replace the active repair set.

## Half-Duplex Arbitration

### Phases and leases

```text
CONTROL
  -> REQUEST/GRANT
  -> TURNAROUND -> PAYLOAD_BURST
  -> TURNAROUND
  -> GROUND_STATUS
       -> TURNAROUND -> REPAIR -> TURNAROUND -> GROUND_STATUS
       -> FINAL_CONFIRM -> TURNAROUND -> FINAL_ACK -> CONTROL
```

In this table, owner means application-data transmitter. Mandatory RadioHead
or Artemis link-layer ACK responses remain allowed regardless of phase; they
must complete before the next turnaround timer begins.

| Phase | Application owner | Proposed bound | Allowed channel-1 traffic |
| --- | --- | --- | --- |
| `CONTROL` | normal control arbitration | indefinite | channel 1 held except valid `PHASE_CONTROL` |
| `REQUEST/GRANT` | flight then ground | one matching request/grant pair | `PHASE_CONTROL` only |
| `PAYLOAD_BURST` | flight | scheduling stop at 15 frames or 650 ms; hard lease 750 ms | metadata, nominal data, and terminal yield |
| `TURNAROUND` | neither | 50 ms quiet | none |
| `GROUND_STATUS` | ground | 500 ms | ground status, repair requests, and terminal yield |
| `REPAIR` | flight | scheduling stop at 15 frames or 650 ms; hard lease 750 ms | metadata, repair data, and terminal yield |
| `FINAL_CONFIRM` | ground | three confirms within the 500-ms ground lease | final confirmation and terminal yield only |
| `FINAL_ACK` | flight | three ACKs within 250 ms | final ACK only |

The flight-side scheduling stop leaves margin below the bridge's hard lease.
Before admitting payload data, the scheduler reserves one slot for a `HEADER`
or `TRANSMIT_STATUS` and one slot for the terminal `PHASE_CONTROL YIELD`.
Payload data cannot consume either reserved slot. Before every frame, the
worker verifies that measured worst-case frame time, link-layer ACK/retry time,
the reserved yield, and the approved guard margin still fit before the 650-ms
scheduling stop. The bridge independently enforces the 750-ms hard stop.
Target-Pi measurement, not arithmetic alone, must qualify these bounds. If no
valid ground response arrives, flight re-establishes a new lease and repeats
bounded metadata/status cycles until inactivity or absolute expiry.

Flight is the sole initiator from `CONTROL`: it sends an identity-bearing
`PHASE_CONTROL REQUEST` for the next required flight-owned phase
(`PAYLOAD_BURST`, `REPAIR`, or `FINAL_ACK`) with a fresh `leaseId`; ground
returns an exact matching `GRANT`. The encoded `leaseMs` must equal the
configured bound for that phase, not an arbitrary peer-selected duration.

When ground is `IDLE`, a valid initial `PAYLOAD_BURST REQUEST` may create a
provisional identity from the complete tuple carried by `PHASE_CONTROL`.
Ground validates packet CRC, encoded bounds, policy, and active-session
conflicts before granting. The provisional identity does not enter
`RECEIVING` until an exact matching `HEADER` arrives. A conflicting request is
not granted, an exact duplicate is idempotent, and an expired provisional
identity returns to `IDLE`. This is the only path that may provision a ground
identity before its header arrives.

After the grant and 50-ms quiet interval, flight may transmit. A `YIELD`
terminates the current phase and names the next phase. It may preserve the
current application owner: a same-owner transition such as
`GROUND_STATUS -> FINAL_CONFIRM` begins after the yielding packet's link-layer
transaction completes and requires no turnaround. An owner-changing transition
requires the 50-ms quiet interval. A ground repair snapshot yields to `REPAIR`;
a valid final confirmation yields to `FINAL_ACK`.

The new owner's local lease begins only after the preceding link-layer
transaction completes and any required turnaround ends. The non-owner's
protective hold begins after its corresponding local send/receive transaction
completes and lasts through the advertised owner bound plus an approved
clock/implementation guard. All timers use local monotonic clocks; peers do not
compare deadlines. A fresh request is retransmitted with bounded backoff until
the peer has also returned to `CONTROL` and grants it.

Each bridge validates the full C3M-DL v2 `PHASE_CONTROL` layout and packet CRC
before changing phase; other C3M-DL v2 interpretation remains above the bridge.
Each peer independently returns its local policy and channel 1 to `CONTROL`/held after a
lease expiry, conflict, or illegal transition. The protective hold and bounded
request retry prevent an earlier local timeout from granting application-data
ownership while the peer may still be transmitting. A valid fresh
`PHASE_CONTROL REQUEST` is explicitly allowed through the `CONTROL` hold.
Stale grants or yields from a prior lease ID are counted and ignored.

Final-ACK recovery uses the same rule. If confirmation, yield, or all initial
ACK copies are lost, timeout returns both peers independently to `CONTROL`.
Flight then opens a fresh identity-matching status or `FINAL_ACK` lease. A
verified tombstone replays the same `confirmId` ACK without changing state, and
ground in `CRC_VALID_ACK_PENDING` or `COMPLETE` grants the matching lease.

### Channel-0 priority

- A critical channel-0 frame preempts queued channel-1 work at an RF frame
  boundary; it does not corrupt an RF frame already in progress.
- Satellite-originated critical traffic may use the next frame boundary during
  a flight-owned lease.
- Ground-originated critical traffic uses a ground-owned or `CONTROL` window;
  it must not transmit over a flight-owned payload frame.
- Routine SOH and event bursts are deferred/suppressed during bulk phases and
  released at a bounded rate in `CONTROL`.
- The exact critical allowlist and emergency-seize behavior remain approval
  decisions; bridge firmware cannot infer F Prime command importance from raw
  bytes without an explicit classification contract.
- Link-layer ACK responses are protocol maintenance, not channel-0 application
  traffic, and remain legal during every phase. A new ownership transition
  waits for any outstanding ACK transaction to finish or time out.

## Scheduling and Component Boundaries

`PayloadDownlinkApp` remains active, and its `run` port remains `async ... drop`.
The rate group only enqueues that tick; the component's active thread is the
sole C3M-DL v2 state owner and executes at most one bounded burst directly in the run
handler. It does not enqueue a second work item onto its own queue. The handler
stops scheduling at 15 frames or 650 ms and must return before the one-second
tick period in target-Pi qualification.

Synchronous receive callbacks do not mutate C3M-DL v2 state. They validate only the
outer size needed for safe copying, copy complete control packets into a fixed,
mutex-protected mailbox, and return promptly. The active thread drains that
mailbox and performs identity, page, phase, and state transitions. Mailbox
overflow is explicit drop telemetry and never overwrites unread control data.
Commands and health pings share the active queue, so qualification must prove
bounded command/ping latency, no queue assertion, and sufficient queue reserve
while run ticks use drop behavior. If the target cannot meet those bounds, the
approved fallback is a dedicated active payload worker with its own queue—not
an unbounded work item on the existing queue.

### Local output result and buffer ownership

The proposed payload boundary is a project-local synchronous
copy-and-status port, not an unmodified `Drv.ByteStreamSend` port. Its contract
is:

1. `PayloadDownlinkApp` retains ownership of its reusable packet buffer.
2. `UartChannelMux` copies the complete packet into its own bounded UART-frame
   storage before returning and never retains the caller's pointer.
3. It returns `LOCAL_ACCEPTED` only after the underlying synchronous
   `Drv.ByteStreamSend` returns `OP_OK`; retryable busy and terminal failure are
   distinct results.
4. `LOCAL_ACCEPTED` proves local UART acceptance only. It is not RF or ground
   delivery and is the boundary used for nominal cursor/repair-bit retirement.

The pinned `ByteStreamDriverModel` SDD is internally inconsistent: its
synchronous prose says ownership returns to the caller, while its status table
says `OP_OK` and `OTHER_ERROR` transfer ownership to the driver. C3M-DL v2 adopts the
stricter table until the pinned contract is clarified. The current
`LinuxUartDriver` performs one synchronous `write()` and does not retain or
deallocate the send pointer, but that implementation detail does not make a
reusable component array contract-safe. If implementation instead adopts
`Drv.ByteStreamSend` directly, it must allocate pool-owned buffers and
implement the corresponding return/deallocation lifecycle.

The copy-before-return rule resolves the `PayloadDownlinkApp` to
`UartChannelMux` boundary only. The mux's wrapped-buffer handoff to the Linux
UART driver must be separately made ownership-correct. Before C3M-DL v2 can be
enabled, approval must select and test either a project-local synchronous
copy/write adapter with an explicit caller-ownership contract, or pool-owned
wrapped buffers with an audited deallocation path. Qualification must prove no
leak, stale pointer, or reuse-before-write under every result.

`LOCAL_ERROR` is delivery-indeterminate: Linux `write()` may emit a strict
prefix before returning an error. It never retires a nominal cursor or repair
bit. The UART path must recover and resynchronize before retransmitting the
complete packet; C3M-DL v2 duplicate handling makes that full retransmission safe.

Proposed ownership:

| Owner | Responsibility |
| --- | --- |
| `PayloadDownlinkApp` | C3M-DL v2 state, identity, CRC-32, burst cursor, repair bitset, timeouts, confirmation |
| `CommsApp` | duplicate/conflict admission and mission completion only after ground verification |
| channel-1 output/UART adapter | synchronous copy-before-return and explicit local-accepted/busy/failure result |
| `UartChannelMux` | channel framing and bounded forwarding; no reinterpretation of C3M-DL v2 identity |
| ground receiver/web app | dual N2/C3M-DL v2 decode, durable bitmap/bytes, repair/status/confirm, honest artifacts |
| ground and satellite Teensy bridges | RF frame priority, phase lease, turnaround guard, counters |

## Compatibility and Migration

C3M-DL v2 is not silently compatible with N2. Compatibility is explicit:

- `N2` remains `0x4E 0x32`; C3M-DL v2 uses `C2` (`0x43 0x32`).
- Ground selects the decoder by magic and never combines N2 and C3M-DL v2 packets.
- An N2 CRC-valid product is labeled `LEGACY_UNVERIFIED_COMPLETE` because N2
  has no final ground-confirmation/ACK contract.
- The 44-byte segment maximum is unchanged, so C3M-DL v2 does not require additional
  RF fragmentation.
- CRC-32 metadata must be computed end to end; the current `U32 sourceCrc`
  carrying a CRC-16 value cannot be treated as a C3M-DL v2 CRC-32 without migration.

Proposed rollout order:

1. Land the duplicate-start guard and move payload work out of the blocking
   rate-group path without changing the wire protocol.
2. Add N2/C3M-DL v2 dual decode and durable C3M-DL v2 identity storage to the
   ground receiver first; keep N2 as the transmitted default.
3. Add the expanded flight states; change `CommsApp` to clear only verified
   completion.
4. Add the explicit copy-and-status output contract, fixed additive repair
   bitset, C3M-DL v2 codec, and CRC-32 behind a disabled feature flag.
5. Pass all deterministic local N2 regressions and C3M-DL v2 failure tests, then
   enable C3M-DL v2 only in local emulation.
6. Implement and validate the phase lease on the ground bridge first, then the
   satellite bridge. Do not change RF PHY or pacing in the same patch.
7. Only after local gates pass, proceed through the focused indoor HIL matrix;
   outdoor qualification remains a later gate.

Rollback is the feature flag back to N2, with the UI explicitly showing legacy
unverified semantics. Rollback must not reinterpret a C3M-DL v2 partial as N2.

## Required Telemetry and Durable Evidence

### Flight transfer

- state, state age, prior state, and transition reason
- full identity and source-retained flag
- nominal accepted, repair pending, repair accepted, and total packet counts
- new/duplicate/out-of-range repair bits and current `requestId`
- transmit-complete time and ground-verified time separately
- confirmation/ACK attempts, inactivity age, absolute age, and expiry reason
- local-accepted/busy/failure output counts, control-mailbox drops, and queue
  high-water

### Link phase

- current phase, owner, lease age/deadline, and last transition reason
- payload/repair burst frames and duration
- turnaround violations, invalid phase packets, lease expiries, and forced
  channel-1 holds
- critical channel-0 preemptions and deferred routine channel-0 work

### RF, USB, and ground receiver

- RF TX/RX by channel, RSSI last/min/average, and RadioHead good/bad counts
- TX-completion timeouts, radio recovery/terminal failures, and reset cause
- USB zero/partial writes, backpressure/recovery, queue high-water, and discard
- receiver connection/USB identity independent from GDS health
- persisted bitmap count, duplicates/conflicts, missing count, repair rounds,
  CRC-32 result, reconnect/restart count, and terminal artifact reason

Every state transition and fault counter change used for qualification must
carry the clock-domain/monotonic/UTC-validity timestamp triple in the durable
run artifact.

## Deterministic Local Test Gates

No build or happy-path-only run approves C3M-DL v2. Before any flash, local tests must
cover:

| Area | Required cases |
| --- | --- |
| Codec | golden bytes for all ten packet types; exact lengths/endian; CRC-16 known vectors; truncated, oversize, invalid type, bad CRC |
| Identity | duplicate tuple; conflicting tuple; provisional initial identity and expiry; nonmatching epoch; inhibited epoch/transfer/request-ID wrap; corrupt/missing epoch store; no cross-epoch byte merge |
| CRC-32 | standard check vector; empty, one-byte, final-short-packet, and 38,480-byte product; source/ground equality and mismatch |
| Nominal state | every state/transition; transmit complete without verification; exact confirmation required; duplicate confirmation/ACK |
| Repair | loss of `1`, `10`, `100`, `300`, and about `550` packets; canonical page offsets/lengths and 61,200-packet cap; missing/reordered/conflicting pages; duplicate, overlapping, out-of-range requests; incomplete-snapshot lease expiry; round-robin fairness |
| Delivery result | busy, delayed local acceptance, terminal and delivery-indeterminate partial-write failure, UART resynchronization, queue saturation, buffer copy/ownership, and proof that a bit is not cleared before local acceptance |
| Loss recovery | lost header, data, transmit status, ground status, retry page, phase request/grant/yield, final confirmation, final ACK, abort status, and restart status |
| Timing | reserved metadata/yield slots; 15-frame/650-ms scheduling stop and 750-ms hard lease; same-owner phase transition; timer origins and protective-hold guard; 50-ms quiet; 500-ms ground window; inactivity/absolute expiry; lease expiry to channel-1 hold and fresh-request recovery |
| Restart | receiver restart/resume at 25/50/90%; USB re-enumeration; atomic flight-checkpoint reload/corruption and unauthenticated-old-identity failure; immutable-source mismatch; flight restart to new epoch/packet 0; old session saved partial; verified tombstone ACK replay after restart |
| Compatibility | unchanged N2 happy path; dual decoder separation; N2 labeled legacy unverified; feature-flag rollback with no state mixing |
| Integration | complete local C3M capture/downlink/decode with exact product CRC/hash and no rate-group cycle-slip/assertion |

Terminal assertions must show that partial bytes preserve positions, complete
products require CRC-32 equality, and no failure path emits verified complete.

## Open Approval Decisions

Nothing below is approved merely because it is written here:

1. Freeze the `C2` magic, all ten packet type values/layouts, little-endian
   encoding, and 33-byte data capacity.
2. Approve `PHASE_CONTROL`, flight-only initiation from `CONTROL`, provisional
   initial ground identity, lease-ID chain rules, same-owner and owner-changing
   request/grant/yield transitions, link-layer ACK exception, and the bridge's
   limited C3M-DL v2 validation responsibility.
3. Approve canonical paged repair snapshots: 30-byte page bitmap, page
   indexing/count, 61,200-packet single-snapshot cap, lease-expiry behavior,
   request-ID retransmission/allocation, and additive handling of incomplete or
   reordered snapshots.
4. Select durable `bootEpoch` storage and approve epoch/transfer/request/
   confirm/lease ID exhaustion, inhibition, namespace rotation, and corruption
   policy. C3M-DL v2 must remain inhibited whenever safe non-reuse cannot be proven.
5. Approve the durable flight checkpoint schema, immutable source retention and
   fingerprint, atomic update/recovery behavior, verified ACK-service
   tombstone, and fail-closed local-only outcome when an old identity or time or
   source continuity cannot be established.
6. Approve CRC-32/IEEE and decide where source CRC-32 is first computed,
   persisted, and passed through the existing F Prime descriptor ports.
7. Approve reserved metadata/yield slots, the 15-frame/650-ms scheduling stop,
   750-ms hard lease, timer origins, protective-hold clock/implementation guard,
   50-ms turnaround, 500-ms ground window, 250-ms final-ACK window, bounded
   tombstone replay, three-confirm/three-ACK repetition, and inter-repeat
   spacing after target-Pi timing measurement.
8. Approve 10-minute inactivity, 30-minute absolute product-attempt lifetime,
   verified ACK-service retention through that lifetime, clock-domain/UTC
   evidence rules, and product-file retention after expiry.
9. Define the channel-0 critical allowlist, routine SOH suppression budget, and
   whether any emergency seize mechanism is safe for this RF hardware.
10. Approve the fixed repair-bitset maximum product size and memory budget,
    bounded control-mailbox size, and overflow policy.
11. Approve the flight-restart rule: new epoch, `RESTART_STATUS`, old ground
    session remains partial, and retransmission restarts at packet 0 rather
    than splicing.
12. Resolve the pinned synchronous ByteStream ownership contradiction and
    approve the local copy-and-status F Prime port, copy-before-return contract,
    delivery-indeterminate partial-write recovery, and an ownership-correct
    mux-to-Linux-UART wrapped-buffer lifecycle; or select and audit the
    pool-owned `Drv.ByteStreamSend` alternative and its deallocation lifecycle
    end to end.
13. Approve the active-thread worker model, queue reserve, command/ping latency,
    health monitoring, and dedicated-worker fallback threshold.
14. Define the N2 feature-flag lifetime, operator warning, rollback procedure,
    and final removal criteria.

After these decisions, this proposal can become the implementation ICD and its
constants can move into the generated transport manifest. Until then, it is a
review artifact only.
