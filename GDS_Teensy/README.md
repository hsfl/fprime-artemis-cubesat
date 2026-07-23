# GDS_Teensy

Ground Teensy/RFM23BP baremetal workspace. It is the C3M cold-fallback ground
bridge, not the current C3M student baseline. It also remains the ground
adapter for the separately qualified Neutron-2 `D2`/Windows workflow.

The primary ground station is the fixed HackRF launcher documented in
[`../docs/HACKRF_GROUND_STATION_RUNBOOK.md`](../docs/HACKRF_GROUND_STATION_RUNBOOK.md).
It pins `433 MHz`, TX gain `16`, RX LNA/VGA `8/8`, ACK mode, RF amplifier off,
antenna bias off, and no AGC or runtime tuning. Keep this firmware and its
known hardware together as a ready fallback; do not run it alongside the
HackRF stack.

Detailed docs live in:

- `../docs/GDS_TEENSY_RUNBOOK.md`
- `docs/transport_contract.md`

## Scope

- Receives RF channel 0 from the satellite Teensy and streams raw CCSDS bytes
  over USB `Serial` for laptop `fprime-gds`.
- Receives RF channel 1 payload/science bytes and streams them to the payload
  USB serial path when triple-serial USB is enabled.
- Accepts raw GDS uplink bytes from laptop USB `Serial`, batches them, and
  transmits them over RF channel 0.
- Does not handle channel 2. Channel 2 is satellite-local Pi <-> Teensy
  subsystem RPC, currently EPS/PDU.

## Build (Arduino CLI)
```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh
```

## Upload

Build first, list boards, and confirm the physical ground-board upload ID.
The current bench mapping is `usb:100000`; stop if it is absent or identifies
another board. Do not upload by a wildcard serial port when multiple Teensies
are connected.

```bash
cd GDS_Teensy
arduino-cli board list
./tools/arduino-cli/upload.sh usb:100000
```

## Source Layout
- `firmware/gds_teensy/gds_teensy.ino`: top-level sketch.
- `firmware/gds_teensy/src/relay_uart_rf.*`: RF segment reassembly, channel 0
  UART egress, and channel 1 payload egress.
- `firmware/gds_teensy/src/rf23_driver.*`: RF23BP wrapper.
- `firmware/gds_teensy/src/link_protocol.hpp`: framing and RF segment constants.
- `firmware/gds_teensy/src/link_counters.hpp`: link observability counters.
