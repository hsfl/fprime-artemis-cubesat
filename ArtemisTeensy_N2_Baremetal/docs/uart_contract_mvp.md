# UART Contract (RPi <-> Satellite Teensy)

## Scope
- Single UART link only.
- Fixed serial settings: **115200, 8N1**.
- Nominal MVP/HIL mode is transparent raw-byte tunnel mode for RPi<->satellite-Teensy.
- UART carries raw `ComCcsds` / space-packet bytes end-to-end between `fprime-gds` and the F' deployment.
- The Teensy bridges do not add the custom wrapper in the nominal path; they only segment/reassemble bytes for RF transport.
- `fprime-gds` must use `--framing-selection space-packet-space-data-link`.
- Custom UART wrapper mode is legacy/fallback only.

## Nominal Raw-Byte Tunnel

Downlink:
1. RPi F' deployment writes raw `ComCcsds` / space-packet bytes on `/dev/serial0`.
2. Satellite Teensy batches raw UART bytes and sends them over RF using the segment format below.
3. Ground Teensy reassembles RF segments and writes the same raw bytes to laptop USB serial.
4. Laptop `fprime-gds` decodes those bytes with `space-packet-space-data-link` framing.

Uplink:
1. Laptop `fprime-gds` writes raw `ComCcsds` / space-packet bytes to ground Teensy USB serial.
2. Ground Teensy batches raw USB bytes and sends them over RF using the segment format below.
3. Satellite Teensy reassembles RF segments and writes the same raw bytes to RPi UART.
4. The RPi F' deployment decodes the bytes at the `ComCcsds` endpoint.

## Legacy/Fallback UART Frame Format

This wrapper is retained only for fallback testing or legacy debugging. It is not mixed into the nominal MVP/HIL path.

1. `magic0` (1 byte): `0xD4`
2. `magic1` (1 byte): `0xC3`
3. `length` (2 bytes LE): payload size (`1..220`)
4. `payload` (`length` bytes)
5. `crc16` (2 bytes LE): CRC-16/CCITT over payload bytes only

## RF Bridge Behavior
- In nominal mode, both Teensy bridges treat UART/USB bytes as opaque raw bytes and do not parse endpoint framing.
- In fallback wrapper mode, satellite Teensy strips the legacy UART wrapper and treats payload as opaque message bytes.
- Messages are transmitted over RF23BP using segmentation when needed.
- Ground Teensy reassembles segments back into original message bytes.
- Ground Teensy outputs raw message bytes over USB UART to laptop GDS.
- For simple uplink, ground Teensy packetizes raw USB byte bursts into RF messages (12 ms idle flush or 220-byte cap).

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
