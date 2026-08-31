# Satellite Teensy F Prime + Zephyr port

This directory contains the satellite-only Teensy 4.1 controller port. The
Raspberry Pi F Prime deployment and mission policy remain unchanged. The
working Arduino satellite implementation under
`ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy` remains the regression
oracle and fallback; `GDS_Teensy` remains the Arduino interoperability peer.

## Implemented controller

- Minimal F Prime topology: Zephyr rate driver and time service plus a passive
  `BridgeShell` scheduled by an active 5 ms rate group. The queue is sized for
  the bounded two-attempt TX timeout path, and the shell publishes real
  controller status and counters.
- Allocation-free UART framing/router for exact channels 0 (CCSDS), 1
  (payload), and 2 (Teensy-local), with generated constants from
  `config/transport_constants.json` and `config/rf_networks.json`.
- Bounded RF segmentation/reassembly, per-channel message IDs, ACK timeout and
  retries, duplicate re-ACK suppression, malformed-input drops, and wrap-safe
  timeouts.
- Native Zephyr RFM23BP driver using SPI, GPIO, nIRQ-to-work signaling, safe
  SDN power cycling, amplifier sequencing, bounded 100 ms chip-ready and 500 ms
  TX waits, FIFO recovery, TX-to-RX re-arm, and factual local-TX safe-off after
  one recovered timeout retry.
- Exact 424.0 MHz RadioHead frequency encoding, 125-kbps GFSK register preset,
  register `0x58 = 0xC0`, and 30 dBm radio setting.
- Channel-2 RF status/enable service, asynchronous 350 ms PDU proxy, fixed
  192 KiB payload cache with CRC/retry repair, fixed 4,800-byte preview, and
  cache/preview mutual exclusion.
- Real RT1062 watchdog with the existing 12-second contract and boot reset-cause
  reporting.

## Memory strategy

The board's supported 512 KiB `ocram2` region remains Zephyr's general SRAM.
The adjacent 256 KiB `ocram` region is a distinct linker region and holds the
fixed controller object, including the 192 KiB science cache. The 128 KiB DTCM
region holds the two IRQ-driven UART rings; ITCM remains separate. These regions
are intentionally not represented as one heap.

The controller/runtime sources contain no `new`, `delete`, `malloc`, or STL
dynamic containers. F Prime/Zephyr creates its statically pooled thread objects
during startup; nominal controller polling uses fixed storage and bounded
queues.

Latest F Prime target link (2026-08-30):

| Region | Used | Capacity | Headroom |
| --- | ---: | ---: | ---: |
| Flash | 139,304 B | 8 MiB | 8,249,304 B |
| RAM (`ocram2`) | 41,980 B | 512 KiB | 482,308 B |
| DTCM | 4,352 B | 128 KiB | 126,720 B |
| OCRAM | 207,380 B | 256 KiB | 54,764 B |
| ITCM | 0 B | 128 KiB | 131,072 B |

The linker map is generated at
`fprime/build-fprime-automatic-zephyr/zephyr/zephyr.map`.

## Build and validate on macOS

Prerequisites used for the recorded build:

- existing Zephyr 4.3 workspace at `~/Developer/fprime-zephyr-reference`;
- Zephyr SDK 0.17.4 at `~/Developer/zephyr-sdk-0.17.4`;
- the repository's existing F Prime Python environment.

Run the complete local validation (no hardware or flashing):

```bash
cd ~/Developer/fprime-artemis-cubesat
./FprimeZephyrSatellite/tools/validate.sh
```

Build only the deployable F Prime image:

```bash
./FprimeZephyrSatellite/tools/build_teensy41.sh
```

The deployable image is
`FprimeZephyrSatellite/fprime/build-fprime-automatic-zephyr/zephyr/zephyr.elf`.
The target is deliberately built with `ninja -j1`: this F Prime/FPP checkout
has parallel generated-source dependency races. Building the specific
`zephyr.elf` target also avoids unrelated F Prime framework test helpers that
are not part of the deployment.

For Windows student development, use WSL2 and the same scripts from a WSL
checkout. Native PowerShell/CMD F Prime builds are not the supported path.

## Proof boundary and remaining migration

Host tests prove framing, CRC, segmentation, malformed data handling, queue
bounds, timeouts, ACK behavior, state transitions, local RPC, PDU response
parsing, cache/repair, preview exclusion, and watchdog model behavior. The two
`BOARD=teensy41` links prove source, FPP topology, device-tree, toolchain, and
memory-layout compatibility.

No board was flashed and no HIL or RF test was run. Pin electrical behavior,
RFM23BP register effects, PDU UART traffic, watchdog reset, and interoperability
with the Arduino `GDS_Teensy` remain hardware validation work. Migrating the
ground Teensy is explicitly out of scope until that interoperability is proven.

This is an engineering port, not HIL evidence, RF qualification, flight
qualification, or flight-ready software.
