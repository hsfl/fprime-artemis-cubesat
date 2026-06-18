# GDS Teensy RF Transport Contract

## Purpose
Bridge RF23BP segmented transport into laptop-side streams:

- RF channel 0 -> raw F Prime CCSDS bytes on USB `Serial` for `fprime-gds`
- RF channel 1 -> payload/science packet bytes on USB `SerialUSB2` when triple-serial USB is enabled

The ground Teensy does not handle channel 2. Channel 2 is the satellite-local Teensy/PDU RPC path on the Pi <-> satellite UART only.

## RF Segment Packet
Each RF packet carries one segment:

1. `seg_magic` (1 byte): `0xA5` for channel 0, `0xA6` for channel 1
2. `msg_id` (1 byte): rolling message identifier
3. `seg_idx` (1 byte): zero-based segment index
4. `seg_count` (1 byte): total segment count in message
5. `chunk_len` (1 byte): payload bytes in this segment (`1..44`)
6. `chunk` (`chunk_len` bytes): opaque payload bytes

RF packet max length in this project: `49` bytes.

## Reassembly Rules
- A message starts only on segment `seg_idx=0`.
- Segments must arrive in order for a given `msg_id`.
- Any mismatch (`msg_id`, `seg_idx`, `seg_count`) drops current partial message.
- Reassembly timeout: `500 ms` from last segment.
- Reassembled channel 0 bytes are written directly to USB `Serial` without additional framing.
- Reassembled channel 1 bytes are written to `SerialUSB2` when available; otherwise they are emitted through the configured framed/raw output path.

## Uplink Rules (Laptop USB -> RF)
- Ground Teensy reads raw F Prime/GDS bytes from USB `Serial`.
- Bytes are accumulated into one channel 0 RF message until either:
  - buffer reaches `44` bytes, or
  - no new bytes arrive for `12 ms`.
- The accumulated message is segmented using RF magic `0xA5` and transmitted to the satellite Teensy.
- Payload-channel uplink can use `SerialUSB2` when triple-serial USB is enabled.
