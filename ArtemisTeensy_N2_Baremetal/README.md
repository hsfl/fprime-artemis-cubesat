# ArtemisTeensy_N2_Baremetal

Baremetal Teensy relay workspace for satellite-side RF23BP <-> UART bridging.

## Scope
- RPi <-> satellite Teensy uses custom UART wrapper (`0xD4 0xC3 + len + crc16`).
- UART payload carries opaque F' bytes.
- RF link uses segmented transport (`msg_id/seg_idx/seg_count/chunk_len`).
- Supports reassembly on receive before writing to local UART.

## Build (Arduino CLI)
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

## Upload
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## Source Layout
- `firmware/satellite_teensy/satellite_teensy.ino`: top-level sketch.
- `firmware/satellite_teensy/src/relay_uart_rf.*`: UART wrapper parsing + RF segmentation/reassembly.
- `firmware/satellite_teensy/src/rf23_driver.*`: RF23BP wrapper.
- `firmware/satellite_teensy/src/link_protocol.hpp`: framing constants.
- `firmware/satellite_teensy/src/link_counters.hpp`: observability counters.
- `docs/uart_contract_mvp.md`: current UART/RF transport contract.
