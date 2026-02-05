# Artemis CubeSat Flatsat Notes (Current System + F Prime Refactor Path)

## 1. Scope and Intent

This file documents how the current demo flatsat works in code today and turns that understanding into a concrete migration path to:

- Keep Teensy baremetal for radio + hardware-facing sensor interfaces
- Move mission and payload decision logic to Raspberry Pi
- Refactor Raspberry Pi software into F Prime components/topology
- Use a robust UART bridge between Teensy and Raspberry Pi

The analysis is based on the folder "espcor_teensy_demo"

## 2. What Exists Today

### 2.1 Major Runtime Nodes

- Satellite Raspberry Pi:
  - `SatellitePayload/thermal_camera_controller.py`
  - Captures thermal camera frames, averages for still capture, downsamples for stream mode, and sends framed UART payloads
- Satellite Teensy 4.1:
  - `SatellitePayload/satellite_teensy/satellite_teensy.ino`
  - Controls RPi power line, receives UART payloads, packets image data over RF23BP, handles sensor readouts, and handles retry requests
- Ground Station Teensy 4.1:
  - `dev-teensyGroundStation/ground_station_teensy/ground_station_teensy.ino`
  - Receives and reassembles thermal packets, verifies CRC, requests missing packets, exports CSV, and forwards console/status text
- Ground Station PC Python CLI:
  - `dev-teensyGroundStation/ground_station_serial_cli_teensy.py`
  - Interactive operator CLI over USB serial; detects CSV blocks, writes files, and drives stream viewer

### 2.2 Physical/Link Topology

Current link path:

- Thermal camera -> RPi (USB/UVC)
- RPi -> Satellite Teensy (UART)
- Satellite Teensy -> Ground Teensy (433 MHz RF23BP using RadioHead RH_RF22)
- Ground Teensy -> PC (USB serial)
- PC -> matplotlib viewer / CSV file


## 3. Satellite Teensy Architecture (Current)

File: `SatellitePayload/satellite_teensy/satellite_teensy.ino`

### 3.1 Core Responsibilities

- RF command listener from ground (`u`, `r`, `v1/v0`, `p1/p0/ps`, `s*`, `g`, `~`)
- UART framed receive from Pi for:
  - Thermal payload image bytes
  - Status messages (`STATUS:*`)
  - Livestream frame packets
- Thermal data packetization into 49-byte RF packets
- Per-packet CRC16 + end-of-image CRC16
- Retry response based on missing-packet bitmap
- Sensor polling/reporting:
  - 7 temperature channels (TMP36 analog)
  - 5 current sensors (INA219 over `Wire2`)
  - GPS (`Adafruit_GPS` on `Serial7`)
  - IMU (`LIS3MDL` + `LSM6DSOX`)

### 3.2 Radio/Packet Settings

- RF payload cap: `RADIO_PACKET_MAX_SIZE = 49`
- Thermal packet overhead:
  - 2 bytes `packetIndex`
  - 2 bytes CRC16
- Thermal payload chunk per packet:
  - `PACKET_DATA_SIZE = 45`
- Modem currently active in code:
  - `GFSK_Rb125Fd125`
- Frequency:
  - `433.0 MHz`
- TX power:
  - `RH_RF22_RF23BP_TXPOW_30DBM`

### 3.3 UART Framing from Pi

Still capture/status frame format:

- Header magic: `DE AD BE EF`
- Length: `uint16` little-endian
- Payload bytes
- End marker: `FF FF`

Timeouts:

- Header timeout: 15 s
- Payload timeout: 30 s
- End timeout: 1 s

### 3.4 Ground Command Semantics

Inbound RF command bytes:

- `u`: trigger RPi capture over UART (`TRIGGER\n`) and receive framed image
- `r`: transmit the currently captured image via RF packets
- `v1`: start stream mode
- `v0`: stop stream mode
- `p1`, `p0`, `ps`: RPi power on/off/status from Teensy GPIO
- `s[g|i|b|G|I]`: GPS/IMU query or reinit
- `g`: ping/pong
- `~`: reset satellite Teensy
- `0xBB` typed packet: retry request bitmap

### 3.5 Thermal Transfer Protocol (Satellite -> Ground)

1. Header packet:
   - Marker `FF FF`
   - `imageLength`
   - `totalPackets`
   - Magic words `DEAD BEEF`
2. Data packets:
   - `packetIndex` (`uint16`)
   - `0..45` bytes payload
   - CRC16 over actual payload bytes in this packet
3. End packet:
   - Marker `EE EE`
   - `packetCount`
   - image CRC16 over full image buffer

### 3.6 Retry Protocol

Retry request packet type:

- `0xBB` + `packetCount` + `offset` + 45-byte bitmap (360 bits)
- Bit value `1` means packet missing

Satellite behavior:

- Resends marked packets
- Sends end packet after resend window with current image CRC


## 4. Raspberry Pi Payload Controller (Current)

File: `SatellitePayload/thermal_camera_controller.py`

### 4.1 Core Responsibilities

- UVC camera initialization via `uvctypes`
- Continuous frame callback ingestion
- Still capture mode:
  - Grab up to 10 valid frames
  - Average to reduce noise
  - Send full 160x120 16-bit thermal payload over UART
- Livestream mode:
  - Wait for `FRAME\n` requests from Teensy
  - Pull latest frame snapshot
  - Downsample to 80x60
  - Convert to 8-bit
  - Send stream UART frame with stream magic

### 4.2 Status Packet Contract

Pi sends `STATUS:*` messages over same UART framing to inform Teensy state machine, including examples:

- `BOOT`
- `CAM_READY`
- `NO_CAMERA`
- `IDLE`
- `CAPTURE_START`
- `CAPTURE_DONE`
- `NO_FRAMES`
- `TX_FAIL`
- `SHUTDOWN`
- `ERROR`

Satellite Teensy marks `RPI_IDLE_READY = true` only when `STATUS:IDLE` is received.

### 4.3 Stream UART Contract (Pi -> Satellite Teensy)

Per-frame format:

- `CA FE BA BE`
- `frameSeq` (1 byte)
- `frameSize` (2 bytes, expected 4800)
- frame payload (4800 bytes)
- end marker `FF FF`

The stream loop is request/response, not push:

- Teensy sends `FRAME\n`
- Pi returns exactly one stream frame

This avoids UART buffer overrun and backpressure issues.


## 5. Ground Station Teensy Architecture (Current)

File: `dev-teensyGroundStation/ground_station_teensy/ground_station_teensy.ino`

### 5.1 Core Responsibilities

- Interactive command interpreter over USB serial
- RF receive and packet classification:
  - Thermal header/data/end
  - serial message forwarding packets (`0xAA`)
  - stream packets (`0xCC`) when stream mode is active
- Thermal image reassembly and integrity checks
- Missing packet tracking + retry request transmission
- CSV export over serial
- Stream frame reassembly and binary forwarding to host Python

### 5.2 Thermal Reassembly Details

- Buffer `imgBuffer[40000]`
- `packetReceived[1200]` tracks duplicates/missing
- Per-packet CRC checked before accepting data
- End packet triggers:
  - full image CRC verification
  - retry logic if missing packets remain

Retry strategy:

- Wait grace period after end packet
- Request retries if missing count exceeds threshold
- Max retry rounds: 2

### 5.3 Operator Commands (Ground Teensy CLI)

Main commands:

- `capture` -> forwards `u`
- `request` -> forwards `r`
- `export` -> emits CSV block markers and grid data
- `stream start|stop` -> sends `v1`/`v0`
- `sensor ...` -> forwards `s*`
- `rpi on|off|status` -> forwards `p*`
- `ping`, `radio status`, `sat_reset`, etc.

### 5.4 Host Stream Binary Contract (Ground Teensy -> PC)

When stream frames are assembled, Teensy emits binary packet:

- Magic `"WRM!"` (`57 52 4D 21`)
- `seq` (1 byte)
- `pkts` (1 byte, packet count received)
- checksum (2 bytes, simple sum of 4800 payload bytes)
- frame payload (4800 bytes)

PC Python CLI detects this and renders live frames.


## 6. Ground Station Python CLI (Current)

File: `dev-teensyGroundStation/ground_station_serial_cli_teensy.py`

### 6.1 Core Responsibilities

- Auto-detect serial port
- Duplex session:
  - read thread for Teensy output
  - main thread for operator input
- CSV capture using markers:
  - `=== START CSV ===`
  - `=== END CSV ===`
- Auto-save to next `thermal_data_###.csv`
- Auto-launch viewer
- Parse and cache satellite GPS/IMU text blocks into metadata comments in CSV
- Stream binary parsing (`WRM!`) and queueing for live viewer process

### 6.2 Important Behavior

- Text and binary data are mixed on same serial stream and handled with a raw buffer scanner
- Viewer process is separate (`multiprocessing`) to keep matplotlib on main thread context


## 7. What Is and Is Not Implemented

### 7.1 Implemented and Active

- End-to-end still capture and downlink
- End-to-end livestream (best-effort)
- Missing packet retry requests
- Per-packet and whole-image CRC verification
- GPS/IMU and power sensor reporting from satellite Teensy
- RPi power control from ground through satellite Teensy

### 7.2 Present but Partial / Inconsistent

- PDU communications exist as separate test utility:
  - `pdu_comm/pdu_comm.ino`
  - not integrated into satellite operational command path
- Documentation mismatch in places:
  - some README sections still mention 38.4 kbps defaults while code currently sets 125 kbps
- Some command help text references temperature/current command shortcuts on satellite side, but radio command parser currently routes sensor queries through `s*` GPS/IMU paths

## 8. Key Operational Sequence Today (Single Capture)

1. Ground operator enters `capture`
2. Ground Teensy sends `u` over RF
3. Satellite Teensy checks `RPI_IDLE_READY`
4. Satellite Teensy sends `TRIGGER\n` to Pi
5. Pi captures and averages thermal frames
6. Pi sends framed UART thermal payload
7. Satellite Teensy stores payload in image buffer
8. Ground operator enters `request`
9. Ground Teensy sends `r`
10. Satellite Teensy transmits header + data packets + end packet
11. Ground Teensy validates and reassembles
12. Ground Teensy may request missing packets via bitmap
13. Ground operator enters `export`
14. Ground Teensy prints CSV block
15. Python CLI writes CSV and opens viewer
