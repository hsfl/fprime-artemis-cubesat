# C3M On-Demand Teensy Cache Downlink Plan

## BLUF

The Raspberry Pi must not stage a captured `.fdp` file into the satellite
Teensy automatically. A ground `REQUEST_SCIENCE_DOWNLINK` command starts one
logical transaction:

1. F Prime opens the selected `.fdp` file once.
2. F Prime streams it once over the satellite-local UART channel into a fixed
   Teensy RAM cache.
3. The Teensy verifies the complete byte count and CRC.
4. The Teensy immediately sequences the cached bytes into the existing `N2`
   payload packets and sends them over RF.
5. The Teensy retains that cache for retry packets until another product is
   requested.

This removes the current one-second F Prime packet-pump bottleneck without a
ground-receiver redesign. The existing ground packet format, file
reconstruction, CRC validation, and retry request format remain compatible.

## Scope

### In scope

- One on-demand F Prime request that performs cache upload and starts RF
  transmission.
- A 64 KiB fixed satellite Teensy cache.
- Whole-product byte-count and CRC verification before RF transmission.
- Explicit transfer identity and offsets on every cache upload message.
- Existing `N2` header, data, end, and retry packets over RF.
- Satellite-side interception and servicing of matching retry requests.
- A bounded non-blocking Teensy transmit state machine.
- Focused unit/build validation and a three-picture HIL timing run.

### Out of scope for this pass

- Automatic staging immediately after capture.
- A second mission command to start a staged image.
- Major ground Teensy or Python receiver changes.
- A new ground completion protocol.
- Multiple simultaneous products, dynamic allocation, or A/B caches.
- Changing RF modulation or the existing over-the-air packet contract.

## State and ownership

F Prime remains the source-of-truth owner of captured files. The satellite
Teensy cache is an acceleration and repair cache only.

The Teensy cache states are:

- `EMPTY`: no valid product.
- `RECEIVING`: accepting one ordered upload.
- `READY`: byte count and whole-file CRC verified.
- `SENDING`: producing compatible `N2` packets without blocking the relay loop.
- `ERROR`: the current upload was rejected; a new begin request may replace it.

Each cached product is identified by protocol version, transfer ID, product ID,
byte count, and CRC. A new product invalidates the old cache before writing.
An exact repeated request may reuse the verified cache instead of uploading the
same bytes again.

## Local upload protocol

The existing UART virtual channel `2` remains satellite-local and does not
cross RF. Local target `3` is reserved for the payload cache.

- `BEGIN`: identity, total byte count, and expected CRC.
- `CHUNK`: identity, explicit byte offset, byte count, and data.
- `COMMIT_AND_SEND`: verify the cache and begin RF sequencing.
- `ABORT`: invalidate an incomplete transaction.

Only one request is outstanding at a time. Every accepted message returns a
cumulative next offset. Duplicate chunks are acknowledged without duplicating
data; gaps, conflicting identities, oversized products, and bad commits are
rejected. This stop-and-wait contract provides flow control while allowing the
payload-cache UART path to avoid the legacy fixed 37 ms delay per RF packet.

## RF transmission and repair

The cached transmitter produces the current ground-compatible packet sequence:

- three repeated `N2` transfer headers;
- 35 file bytes in each indexed `N2` data packet;
- one `N2` end packet.

It emits at most one RF packet per relay-loop poll so CCSDS, acknowledgements,
watchdog work, and local RPC remain responsive. The cached path uses a small,
separate payload pacing value rather than changing the normal relay path.

Matching `N2` retry requests are consumed on the satellite and serviced
directly from RAM. The Raspberry Pi is not asked to reopen or regenerate the
file. Unrelated payload control packets continue through the existing path.

## F Prime simplification

`PayloadDownlinkApp` remains the single active downlink worker; no new F Prime
component is added. On a science-downlink descriptor it:

1. rejects a conflicting active request;
2. opens and validates the source file once;
3. sends `BEGIN`;
4. sends the next chunk only after the previous cumulative acknowledgement;
5. sends `COMMIT_AND_SEND` after all bytes are acknowledged;
6. closes the source file and reports that the cached transmitter started.

Capture and storage code only records the `.fdp` and descriptor. It does not
contact the Teensy. The redundant storage-manager request echo is removed from
the command path; storage remains the file/catalog owner.

## Failure behavior

- F Prime retries a lost local request a bounded number of times and then
  reports an error.
- The Teensy never transmits an uncommitted or CRC-invalid cache.
- A partial upload cannot be mistaken for a reusable cache.
- A conflicting product receives `BUSY` while RF transmission is active.
- Abort and a new accepted begin invalidate incomplete metadata.
- The last verified cache remains available for existing bitmap repair
  requests until a different product is accepted.

## Pass gate

The implementation passes only when a FlatSat run completes three sequential
fresh-picture requests:

1. each capture creates a different product descriptor;
2. no Teensy cache upload occurs before the downlink command;
3. each downlink performs one Pi-to-Teensy file pass;
4. the existing ground receiver reports a complete file with matching CRC;
5. each `.fdp` decodes to the expected 160 by 120 image;
6. all three requests complete without reboot, manual cache clearing, or a
   ground-receiver code change;
7. every measured request is at least 2x faster than the current approximately
   65 second path (each must complete in 32 seconds or less).

The stretch target is under 15 seconds per current approximately 38 KiB image,
but the required gate is the repeatable 3/3 result at 32 seconds or less.

## HIL acceptance result (2026-07-29)

The connected C3M FlatSat passed the gate:

- three consecutive fresh pictures completed without restarting the Pi, either
  Teensy, GDS, or the payload receiver between pictures;
- all three ground products completed CRC validation and decoded as 160 by 120
  images with 19,200 pixels;
- each optimized downlink completed in approximately 10 seconds total;
- the pre-change compatibility run on the legacy Pi release completed in
  64.7 seconds for the same 38,480-byte product;
- no ground Teensy or payload-receiver protocol redesign was required.

The accepted Pi release is:

```text
/home/pi/artemis/releases/c3m-on-demand-cache-20260729T233825Z
```

`/home/pi/artemis/current` points to that release. The enabled
`artemis-fprime.service` uses `/home/pi/artemis/current` for both
`WorkingDirectory` and `ExecStart`, so the accepted release starts
automatically after a Pi reboot. The previous release remains under
`/home/pi/artemis/releases/` for rollback.

## Command-uplink hardening (2026-07-29, HIL pending)

Static analysis after the three-picture acceptance run isolated the selective
command failure below F Prime and the Pi UART. RadioHead's `RH_RF22` driver
reuses `_buf` and `_bufLen` for transmit and receive, leaves the transmitted
length populated after packet-sent, and does not clear it when returning to RX.
A following command can therefore be rejected as shorter than the stale TX
packet or accepted with stale TX bytes prefixed. In the observed case, a stale
five-byte ACK makes the next argument-bearing command look like another ACK, so
the satellite relay silently ignores it before acknowledging or forwarding it
to the Pi.

The project-owned RF wrapper now:

- returns both radios to RX through a clean FIFO, interrupt, and software-buffer
  transition after TX;
- consumes an already-complete RX packet without redundantly re-entering RX;
- defers only normal satellite telemetry/cache TX for six milliseconds after a
  detected incoming preamble, preventing `setModeTx()` from erasing a command
  still in flight;
- leaves immediate satellite ACKs and ground command transmission unguarded so
  uplink retains priority during the fast cached downlink.

No runtime debug hooks, third-party RadioHead edits, RF packet-format changes,
or ground-receiver redesign were added. Offline validation passes 95 Python
transport/receiver tests, both Teensy builds, the native F Prime build, and all
7 F Prime component tests.

Hardware acceptance remains pending. After flashing, require:

1. 20 alternating no-argument and `U32` commands with 20 command
   acknowledgements and 20 Pi-side executions;
2. successful commands during and immediately after one fresh-picture
   downlink;
3. the existing three-picture CRC/decode gate near 10 seconds per image, with
   no reset or persistent retry/timeout wedge.

### Operator environment note

Start the C3M payload receiver after activating the F Prime virtual
environment, not merely by invoking the venv's Python through an absolute
path. The decoder launches `fprime-dp` as a subprocess and therefore requires
the venv `bin` directory on `PATH`:

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py
```
