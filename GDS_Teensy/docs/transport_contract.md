# GDS Teensy RF Transport Contract

## Purpose
Bridge RF23BP segmented transport into two virtual channels:

- Channel `0`: raw F' / `ComCcsds` bytes for laptop `fprime-gds` UART input.
- Channel `1`: fixed 44-byte generic payload-blob protocol packets for `payload_receiver.py`.

## RF Segment Packet
Each RF packet carries one segment:

1. `seg_magic` (1 byte): `0xA5` for channel `0`, `0xA6` for channel `1`
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
- Channel `0` reassembled bytes are written directly to USB `Serial` without additional framing.
- Channel `1` reassembled fixed 44-byte packets are written directly to `SerialUSB2` when triple serial is enabled.
- Channel `0` uses per-segment ACK/retry.
- Channel `1` does not use per-segment ACK; payload retry happens at the blob protocol layer.

## Uplink Rules (Laptop USB -> RF)
- Ground Teensy reads raw GDS bytes from USB `Serial` and sends channel `0`.
- Ground Teensy reads raw 44-byte payload retry/control packets from `SerialUSB2` and sends channel `1`.
- Bytes are accumulated into one RF message until either:
  - buffer reaches `44` bytes for payload packets or the configured raw chunk limit, or
  - no new bytes arrive for `12 ms`.
- The accumulated message is segmented using the RF segment format above and transmitted to satellite Teensy.
- This mode is intended for simple command uplink and payload retry/control packets where bursts are short.

## USB Endpoints

Build the ground bridge with `teensy:avr:teensy41:usb=serial3`.

- `Serial`: byte-clean `fprime-gds` data stream.
- `SerialUSB1`: debug counters.
- `SerialUSB2`: payload blob packet stream.
