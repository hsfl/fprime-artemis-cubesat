# RF Bridge Reliability Refactor Notes

## BLUF

For the MVP demo, make the RFM23BP bridge reliable before making it faster.

The current bridge is appropriate for first HIL tests, but it is still an
unreliable datagram tunnel. A robust version should add full-message integrity,
ACK/retry, duplicate handling, flow control, and better counters while preserving
the clean endpoint contract:

```text
fprime-gds <-> raw ComCcsds bytes <-> Teensy RF bridge <-> raw ComCcsds bytes <-> RPi F' deployment
```

Do not mix the legacy UART wrapper into the nominal path.

## Current Constraints

The RFM23BP datasheet advertises:

- 64-byte TX FIFO
- 64-byte RX FIFO
- automatic packet handling
- preamble/sync/header/length/data/CRC packet fields
- 8-bit transmit packet length register
- data rates up to 256 kbps

Repo-local source: `docs/rfm23bp/RFM23BP_datasheet.txt`

However, the active firmware uses RadioHead `RH_RF22`, and the installed
RadioHead header currently defines:

```cpp
#define RH_RF22_MAX_MESSAGE_LEN 50
```

Therefore, the practical MVP design target is a 50-byte RadioHead payload, not a
255-byte theoretical RFM23BP packet.

The current custom RF segment format uses:

- `RF_PACKET_MAX_LEN = 49`
- `RF_SEGMENT_HEADER_LEN = 5`
- `RF_SEGMENT_MAX_DATA = 44`
- `FRAME_MAX_PAYLOAD = 220`

That means one 220-byte bridge message becomes up to 5 RF packets. If one segment
is lost, late, or out of order, the whole reassembled message is dropped.

## Design Goal

Preserve raw F' bytes at the UART endpoints while making the RF hop reliable.

The RF layer should:

- detect corrupted full messages
- recover lost segments through retry
- avoid duplicate UART emission after retry
- provide useful counters for HIL debugging
- fail visibly when the offered traffic is too high

## Recommended MVP Refactor

Use a simple stop-and-wait ARQ protocol.

Stop-and-wait is slower than a sliding window, but it is easy to debug and good
enough for the MVP traffic profile:

- command packets
- command acknowledgements
- events
- live SOH telemetry
- small science/status products

### RF Packet Types

Define typed RF packets:

```text
DATA
ACK
NACK
STATUS
RESET
```

### DATA Packet Fields

Keep each encoded RF packet within the RadioHead 50-byte payload cap.

Proposed fields:

```text
magic          1 byte
version        1 byte
type           1 byte
direction      1 byte
msg_id         1 byte
seg_idx        1 byte
seg_count      1 byte
total_len      2 bytes
payload_crc16  2 bytes
chunk_len      1 byte
chunk          N bytes
```

With this 12-byte header, each DATA packet carries up to 38 bytes of F' payload
inside the current RadioHead cap.

This is less efficient than the current 44-byte chunk, but it buys:

- full-message length validation
- full-message CRC validation
- protocol versioning
- packet typing
- clean ACK/NACK support

Optimization can come later.

### ACK Packet Fields

```text
magic      1 byte
version    1 byte
type       1 byte
direction  1 byte
msg_id     1 byte
status     1 byte
```

`status` should distinguish:

- complete and accepted
- duplicate already accepted
- rejected due to CRC
- rejected due to timeout
- rejected due to oversize

### NACK Packet Fields

```text
magic        1 byte
version      1 byte
type         1 byte
direction    1 byte
msg_id       1 byte
reason       1 byte
missing_seg  1 byte
```

For MVP, NACK can be optional. Timeout-based retransmission is enough if the
sender resends the whole message.

## Stop-and-Wait Behavior

### Sender

1. Pull one queued UART message.
2. Assign `msg_id`.
3. Compute full-message CRC.
4. Segment into DATA packets.
5. Send all segments in order.
6. Wait for `ACK(msg_id)`.
7. If ACK arrives, release the next queued message.
8. If timeout expires, resend the whole message.
9. If retry limit is exceeded, drop the message and increment counters.

Suggested initial values:

```text
ACK_TIMEOUT_MS = 250
MAX_RETRIES = 3
MAX_IN_FLIGHT_MESSAGES = 1
```

Tune after bench tests.

### Receiver

1. Accept DATA packet with valid magic/version/type.
2. Start reassembly only on `seg_idx = 0`.
3. Append in-order segments for the same `msg_id`.
4. Reject out-of-order or mismatched segment metadata.
5. When all segments arrive, validate `total_len` and `payload_crc16`.
6. Emit bytes to UART only after validation passes.
7. Send `ACK(msg_id)`.
8. If a duplicate completed `msg_id` arrives, re-send ACK but do not emit bytes
   again.

## Duplicate Handling

Retries are normal in a reliable protocol. The receiver must remember at least
the last completed message per direction.

Minimum MVP state:

```text
last_completed_msg_id
last_completed_crc
last_completed_len
```

If the same completed message is seen again:

- send ACK again
- do not write duplicate bytes to UART

## Flow Control

ACK/retry prevents silent loss, but it can make queues back up.

Add visible backpressure behavior:

- track queue high-water marks
- reject or pause UART batching when TX queue is nearly full
- avoid starting large messages while retrying a previous message
- expose queue pressure in `#LINK_STATUS`

For MVP, do not implement complex UART XON/XOFF or CTS/RTS unless the hardware
wiring already supports it. Start with bounded queues and visible drops.

## Counters to Add

Extend `#LINK_STATUS` with:

```text
ack_tx
ack_rx
nack_tx
nack_rx
retry_tx
retry_exhausted
ack_timeout
dup_msg_rx
msg_crc_drops
msg_len_drops
proto_version_drops
proto_type_drops
queue_high_water_up
queue_high_water_down
radio_rx_good
radio_rx_bad
radio_tx_good
radio_last_rssi_dbm
fifo_errors
radio_crc_errors
```

The existing helper in `ArtemisTeensy_N2_Baremetal/firmware/libs/rf23bp`
already exposes RadioHead stats, RSSI, interrupt status, CRC error state, and
FIFO recovery helpers. Use that before adding a second diagnostics path.

## Test Plan

Test the bridge before using F' traffic.

### Packet-Pattern Test

Send known payloads:

```text
1 byte
38 bytes
39 bytes
76 bytes
114 bytes
152 bytes
190 bytes
220 bytes
```

The 38-byte boundary matters if the v2 DATA header is 12 bytes and RadioHead cap
is 50 bytes.

Pass criteria:

- exact byte-for-byte recovery
- no duplicate UART bytes
- no queue drops
- no retry exhaustion

### Soak Test

Run:

```text
100 messages
1000 messages
10 minutes live idle + periodic packets
```

Track:

- packet error rate
- retries per message
- RSSI
- queue high-water marks
- radio CRC/fifo errors

### F' Test Order

1. `missionManager.PING`
2. live SOH channels for several minutes
3. `missionManager.SCHEDULE_COLLECTION delaySeconds=10`
4. event/channel-visible science product
5. small file downlink only after the earlier tests stay clean

## Later Optimization

After MVP reliability is proven, consider increasing throughput by:

- changing `RH_RF22_MAX_MESSAGE_LEN` and validating RAM/FIFO behavior
- using larger RadioHead payloads closer to the RFM23BP packet-handler limit
- replacing stop-and-wait with a small sliding window
- adding selective segment retransmission instead of whole-message retransmission
- tuning modem data rate and preamble settings after measuring RSSI/PER

Do not optimize packet size before ACK/retry and counters exist. Bigger packets
make each loss more expensive.

## HIL Readiness Rule

The current bridge is ready for cautious first HIL smoke testing, not for
unattended or high-volume downlink.

Use conservative traffic until these are proven:

- command ACK visible in GDS
- events decode correctly
- channels update continuously
- scheduled collection flow completes
- RF/queue/drop counters remain stable
- science visibility matches the promised demo scope

