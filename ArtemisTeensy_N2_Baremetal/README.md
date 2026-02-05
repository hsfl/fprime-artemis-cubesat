# ArtemisTeensy_N2_Baremetal

Baremetal Teensy relay MVP workspace.

## Scope
- RF23BP <-> UART relay for RPi payload computer integration.
- One UART channel only (`115200 8N1`).
- Minimal link control commands (`PING`, `LINK_STATUS`, `RESET_COUNTERS`).

## Build (Arduino CLI)
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

## Upload
```bash
cd /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## Source Layout
- `firmware/satellite_teensy/satellite_teensy.ino`: top-level sketch.
- `firmware/satellite_teensy/src/relay_uart_rf.*`: relay/parsing logic.
- `firmware/satellite_teensy/src/rf23_driver.*`: RF23BP wrapper.
- `firmware/satellite_teensy/src/link_protocol.hpp`: command/frame constants.
- `firmware/satellite_teensy/src/link_counters.hpp`: link observability counters.
