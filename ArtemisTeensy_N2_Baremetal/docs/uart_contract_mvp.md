# UART Contract (RPi <-> Satellite Teensy)

## Scope
- Single UART link only.
- Fixed serial settings: **115200, 8N1**.
- UART framing remains custom for RPi<->satellite-Teensy.
- UART payload is opaque bytes (intended to carry one full F' transport packet).

## UART Frame Format
1. `magic0` (1 byte): `0xD4`
2. `magic1` (1 byte): `0xC3`
3. `length` (2 bytes LE): payload size (`1..220`)
4. `payload` (`length` bytes)
5. `crc16` (2 bytes LE): CRC-16/CCITT over payload bytes only

## RF Bridge Behavior
- Satellite Teensy strips UART wrapper and treats payload as opaque message bytes.
- Message is transmitted over RF23BP using segmentation when needed.
- Ground Teensy reassembles segments back into original message bytes.
- Ground Teensy outputs raw message bytes over USB UART to laptop GDS.
- For simple uplink, ground Teensy packetizes raw USB byte bursts into RF messages (8 ms idle flush or 220-byte cap).

### RF Segment Format
Each RF packet has:
1. `seg_magic` (1 byte): `0xA5`
2. `msg_id` (1 byte): rolling message ID
3. `seg_idx` (1 byte): segment index
4. `seg_count` (1 byte): total segments in message
5. `chunk_len` (1 byte): payload bytes in this RF packet (`1..44`)
6. `chunk` (`chunk_len` bytes)

Project RF packet max length is `49` bytes, so RF chunk max is `44` bytes.

## Timeout Behavior
- UART parser inter-byte timeout: **250 ms**.
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
