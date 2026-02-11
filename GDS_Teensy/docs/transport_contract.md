# GDS Teensy RF Transport Contract

## Purpose
Bridge RF23BP segmented transport into raw F' bytes for laptop `fprime-gds` UART input.

## RF Segment Packet
Each RF packet carries one segment:

1. `seg_magic` (1 byte): `0xA5`
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
- Reassembled message bytes are written directly to USB serial without additional framing.

## Uplink Rules (Laptop USB -> RF)
- Ground Teensy reads raw bytes from USB serial.
- Bytes are accumulated into one RF message until either:
  - buffer reaches `220` bytes, or
  - no new bytes arrive for `8 ms`.
- The accumulated message is segmented using the RF segment format above and transmitted to satellite Teensy.
- This mode is intended for simple command uplink where packet bursts are short.
