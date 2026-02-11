# GDS_Teensy

Ground-station Teensy baremetal workspace for RF23BP receive + reassembly.

## Scope
- Receives segmented RF payloads from satellite Teensy.
- Reassembles original opaque F' message bytes.
- Streams reassembled bytes over USB serial (`115200`) to laptop `fprime-gds` UART device.
- Accepts raw USB UART bytes from laptop and packetizes them into RF messages (timeout/buffer based) for simple uplink commands.

## Build (Arduino CLI)
```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh
```

## Upload
```bash
cd GDS_Teensy
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## Source Layout
- `firmware/gds_teensy/gds_teensy.ino`: top-level sketch.
- `firmware/gds_teensy/src/relay_uart_rf.*`: RF segment reassembly + UART egress.
- `firmware/gds_teensy/src/rf23_driver.*`: RF23BP wrapper.
- `firmware/gds_teensy/src/link_protocol.hpp`: framing and RF segment constants.
- `firmware/gds_teensy/src/link_counters.hpp`: link observability counters.
