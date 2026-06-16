# UART Contract (RPi <-> Satellite Teensy)

## Scope
- Single UART link only.
- Fixed serial settings: **115200, 8N1**.
- Nominal channelized mode wraps the RPi<->satellite-Teensy UART with a small virtual-channel frame.
- Channel `0` carries byte-clean `ComCcsds` / space-packet bytes for `fprime-gds`.
- Channel `1` carries fixed 44-byte generic payload-blob protocol packets.
- The Teensy bridges do not parse F Prime command/event/telemetry semantics; they only route by virtual channel.
- `fprime-gds` must use `--framing-selection space-packet-space-data-link`.

## Nominal Channelized Tunnel

Downlink:
1. RPi F' deployment sends channel-wrapped frames on `/dev/serial0` through `UartChannelMux`.
2. Satellite Teensy strips the UART wrapper and sends RF segments tagged by channel magic.
3. Ground Teensy routes channel `0` to laptop USB `Serial` as raw GDS bytes.
4. Ground Teensy routes channel `1` to payload USB `SerialUSB2` as fixed 44-byte raw payload packets.

Uplink:
1. Laptop `fprime-gds` writes raw `ComCcsds` / space-packet bytes to ground Teensy USB serial.
2. Ground Teensy sends those bytes as channel `0` RF messages.
3. Payload receiver writes fixed 44-byte retry/control packets to ground Teensy `SerialUSB2`; ground Teensy sends those as channel `1`.
4. Satellite Teensy wraps received RF messages for `/dev/serial0`; `UartChannelMux` routes channel `0` to `ComCcsds` and channel `1` to `PayloadDownlinkManager`.

## Pi UART Frame Format

This wrapper is used on the Pi `/dev/serial0` side only. Ground GDS USB remains raw CCSDS, and ground payload USB remains raw payload packets.

1. `magic0` (1 byte): `0xD4`
2. `magic1` (1 byte): `0xC3`
3. `channel` (1 byte): `0` = CCSDS/GDS, `1` = payload blob protocol
4. `length` (2 bytes LE): payload size (`1..220`)
5. `payload` (`length` bytes)
6. `crc16` (2 bytes LE): CRC-16/CCITT over payload bytes only

## RF Bridge Behavior
- Messages are transmitted over RF23BP using segmentation when needed.
- RF segment magic carries the virtual channel, so no link mode switch is required.
- Channel `0` keeps per-segment ACK/retry for GDS stream cleanliness.
- Channel `1` does not use per-segment ACK; payload reliability is handled by indexed packets, CRCs, and retry bitmaps.
- Ground Teensy outputs channel `0` raw message bytes over USB `Serial` to laptop GDS.
- Ground Teensy outputs channel `1` fixed 44-byte raw payload packets over `SerialUSB2` to `payload_receiver.py`.

### RF Segment Format
Each RF packet has:
1. `seg_magic` (1 byte): `0xA5` for channel `0`, `0xA6` for channel `1`
2. `msg_id` (1 byte): rolling message ID
3. `seg_idx` (1 byte): segment index
4. `seg_count` (1 byte): total segments in message
5. `chunk_len` (1 byte): payload bytes in this RF packet (`1..44`)
6. `chunk` (`chunk_len` bytes)

Project RF packet max length is `49` bytes, so RF chunk max is `44` bytes.

## Timeout Behavior
- Legacy UART wrapper parser inter-byte timeout: **250 ms**.
- RF reassembly timeout: **500 ms**.

On timeout while assembling data:
- Drop partial data
- Increment relevant timeout/drop counters
- Reset parser/reassembler state

## Control Commands (ASCII, line-based)
Commands are prefixed with `#` and newline terminated.

- `#PING\n` -> `#PONG\n`
- `#LINK_STATUS\n` -> one line with current counters
- `#RESET_COUNTERS\n` -> `#OK RESET_COUNTERS\n`

## Counter Semantics
- `uartRxBytes`, `uartTxBytes`: bytes read/written on local UART/USB side.
- `rfRxPackets`, `rfTxPackets`: RF packets received/sent.
- `rfRxMessages`, `rfTxMessages`: complete opaque messages reassembled/sent.
- `rfRxSegments`, `rfTxSegments`: segment-level counters.
- `crcDrops`: UART frame CRC mismatch drops.
- `framingDrops`: malformed UART/RF framing drops.
- `timeoutEvents`: UART frame parser timeouts.
- `rfReassemblyTimeouts`: RF reassembly timeout resets.
- `rfReassemblyDrops`: dropped partial RF messages due to mismatch/order issues.
- `rfOversizeDrops`: messages exceeding configured buffer limits.
- `rfTxDrops`: RF send failures.
