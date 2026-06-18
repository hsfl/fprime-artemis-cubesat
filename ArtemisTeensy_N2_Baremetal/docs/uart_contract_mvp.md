# UART Contract (RPi <-> Satellite Teensy)

## Scope
- Single Raspberry Pi <-> satellite Teensy UART link.
- Fixed serial settings: **115200, 8N1**.
- The Pi UART is shared by tagged virtual channels. It is not a raw byte tunnel anymore.
- `fprime-gds` still uses `--framing-selection space-packet-space-data-link`; channel framing is below the F Prime/GDS endpoint layer.
- The satellite Teensy owns the local PDU UART and terminates PDU requests on channel 2.

## Pi <-> Satellite UART Frame

Every Pi <-> satellite Teensy frame uses:

1. `magic0` (1 byte): `0xD4`
2. `magic1` (1 byte): `0xC3`
3. `channel` (1 byte)
4. `length` (2 bytes LE): payload size (`1..220`)
5. `payload` (`length` bytes)
6. `crc16` (2 bytes LE): CRC-16/CCITT over payload bytes only

## Channel Map

| Channel | Name | RF forwarded? | Purpose |
|---:|---|---|---|
| `0` | CCSDS | Yes, RF magic `0xA5` | Normal F Prime/GDS command and telemetry stream |
| `1` | Payload | Yes, RF magic `0xA6` | Payload/science bulk packets, surfaced on ground Teensy payload USB when enabled |
| `2` | Teensy local | No | Pi-side subsystem RPC to the satellite Teensy, currently PDU/EPS |

Channel 2 must never be transmitted over RF. It is consumed by the satellite Teensy and answered back over the same Pi UART channel.

## CCSDS Path (Channel 0)

Downlink:
1. F Prime `ComCcsds` writes CCSDS bytes to `UartChannelMux`.
2. `UartChannelMux` wraps the bytes as channel 0 on `/dev/serial0`.
3. Satellite Teensy unwraps channel 0 and sends it over RF using segment magic `0xA5`.
4. Ground Teensy reassembles RF segments and writes raw CCSDS bytes to laptop USB serial.
5. Laptop `fprime-gds` decodes those bytes with `space-packet-space-data-link`.

Uplink:
1. Laptop `fprime-gds` writes raw CCSDS bytes to ground Teensy USB serial.
2. Ground Teensy sends them over RF using segment magic `0xA5`.
3. Satellite Teensy reassembles RF segments and wraps them as channel 0 to the Pi.
4. `UartChannelMux` unwraps channel 0 and forwards bytes to `ComCcsds`.

## PDU/EPS Path (Channel 2)

The Pi does not open a second PDU serial device. F Prime sends PDU work to the satellite Teensy over channel 2:

1. `EpsService` issues a typed EPS/PDU request.
2. `EpsAdapter_Artemis` builds a PDU v2 frame using `external/artemis-pdu/src/pdu_protocol_v2.h`.
3. `UartChannelMux` wraps the local request as channel 2.
4. Satellite `PduProxy` consumes channel 2, writes the inner PDU frame to `Serial1` at `9600` baud, and waits for the PDU response.
5. `PduProxy` returns a channel 2 response to the Pi.
6. `EpsAdapter_Artemis` validates the PDU v2 response and emits EPS status/telemetry.

Channel 2 request payload:

1. `target` (1 byte): `1` = PDU
2. `request_id` (1 byte): adapter sequence/request ID
3. `pdu_frame_len` (1 byte)
4. `reserved` (1 byte): `0`
5. `pdu_frame` (`pdu_frame_len` bytes)

Channel 2 response payload:

1. `target` (1 byte): `1` = PDU
2. `request_id` (1 byte): matches request
3. `local_status` (1 byte): `0=OK`, `1=BAD_REQUEST`, `2=BUSY`, `3=TIMEOUT`, `4=TARGET_ERROR`
4. `pdu_frame_len` (1 byte)
5. `pdu_frame` (`pdu_frame_len` bytes, only meaningful when `local_status=OK`)

## RF Segment Format

Only channels 0 and 1 are RF forwarded. Each RF packet has:

1. `seg_magic` (1 byte): `0xA5` for channel 0, `0xA6` for channel 1
2. `msg_id` (1 byte): rolling message ID
3. `seg_idx` (1 byte): segment index
4. `seg_count` (1 byte): total segments in message
5. `chunk_len` (1 byte): payload bytes in this RF packet (`1..44`)
6. `chunk` (`chunk_len` bytes)

Project RF packet max length is `49` bytes, so RF chunk max is `44` bytes.

## Timeout Behavior
- UART frame parser inter-byte timeout: **250 ms**.
- RF reassembly timeout: **500 ms**.
- PDU response timeout in the satellite proxy: **350 ms**.

On timeout while assembling data:
- Drop partial data
- Increment relevant timeout/drop counters
- Reset parser/reassembler state

## Counter Semantics
- `uartRxBytes`, `uartTxBytes`: bytes read/written on local UART/USB side.
- `rfRxPackets`, `rfTxPackets`: RF packets received/sent.
- `rfRxMessages`, `rfTxMessages`: complete opaque messages reassembled/sent.
- `rfRxSegments`, `rfTxSegments`: segment-level counters.
- `crcDrops`: UART frame CRC mismatch drops.
- `framingDrops`: malformed UART/RF framing or illegal channel drops.
- `timeoutEvents`: UART frame parser timeouts.
- `rfReassemblyTimeouts`: RF reassembly timeout resets.
- `rfReassemblyDrops`: dropped partial RF messages due to mismatch/order issues.
- `rfOversizeDrops`: messages exceeding configured buffer limits.
- `rfTxDrops`: RF send failures.
