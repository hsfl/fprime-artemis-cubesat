# UART Contract MVP (RPi <-> Teensy)

## Scope
- Single UART link only.
- Fixed serial settings: **115200, 8N1**.
- Teensy acts as transport relay/adapter and does not parse F' command/event/telemetry semantics.

## Frame Format (binary relay payload)
1. `magic0` (1 byte): `0xD4`
2. `magic1` (1 byte): `0xC3`
3. `length` (2 bytes LE): payload size (`1..49`)
4. `payload` (`length` bytes)
5. `crc16` (2 bytes LE): CRC-16/CCITT over payload bytes only

## Timeout Behavior
- Parser inter-byte timeout: **250 ms**.
- On timeout while mid-frame:
  - Drop partial frame
  - Increment `timeoutEvents`
  - Return parser to `WAIT_MAGIC_0`

## Control Commands (ASCII, line-based)
Commands are prefixed with `#` and newline terminated.

- `#PING\n` -> `#PONG\n`
- `#LINK_STATUS\n` -> one line with current counters
- `#RESET_COUNTERS\n` -> `#OK RESET_COUNTERS\n`

## Counter Semantics
- `uartRxBytes`: total bytes read from UART.
- `uartTxBytes`: total bytes written to UART.
- `rfRxPackets`: RF packets received and forwarded to UART.
- `rfTxPackets`: UART frames accepted and forwarded to RF.
- `crcDrops`: frames dropped due to CRC mismatch.
- `framingDrops`: frames dropped due to malformed framing/length.
- `timeoutEvents`: parser timeouts while waiting for frame completion.

## Deferred (Explicitly Out of MVP)
- Thermal payload protocol migration.
- Retry bitmap/data-plane reliability protocol.
- Livestream packetization contract.
- Multi-channel UART multiplexing.
