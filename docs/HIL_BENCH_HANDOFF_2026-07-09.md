# HIL Bench Handoff — 2026-07-09

## BLUF

The C3M HIL bench now passes the real full-resolution Lepton RF downlink. Both
Teensy boards are flashed with the current transport firmware, the Pi service
runs the current ARMv6/libuvc deployment, and a 38,480-byte `160x120` `.fdp`
arrived byte-identically in `58.557 s` with no retry request or transport drops.
This result supersedes the earlier staging blockers recorded in this handoff.

## Repository State

- Repository: `/Users/sozodennis/Developer/fprime-artemis-cubesat`
- Branch: `epscorc3m/demo`
- Intent: small EPSCORC3M demo smoke test before broader Neutron 2 HIL flow.
- Do not copy `external/epscorc3m` architecture into the F' deployment; it is
  reference/implementation context only.

## Physical USB / Upload Map

Both Teensy 4.1 boards are connected.  Always use the physical `usb:*` IDs for
uploads while both are attached.

| Board | Upload ID | macOS serial ports | Role |
| --- | --- | --- | --- |
| Ground/GDS Teensy | `usb:100000` | `/dev/cu.usbmodem115553301`, `...303`, `...305` | Triple serial: GDS data, debug, payload receiver |
| Satellite Teensy | `usb:2100000` | `/dev/cu.usbmodem115565001` | Satellite debug serial |

Ground triple-serial roles:

```text
/dev/cu.usbmodem115553301 = GDS channel 0 data
/dev/cu.usbmodem115553303 = ground debug counters
/dev/cu.usbmodem115553305 = payload channel 1 receiver
```

Enumerate before acting:

```bash
cd ~/Developer/fprime-artemis-cubesat
export ARDUINO_CONFIG_FILE="$PWD/GDS_Teensy/tools/arduino-cli/arduino-cli.yaml"
arduino-cli board list
```

## Firmware Status

### Ground Teensy — done and proven

Current `GDS_Teensy` firmware was compiled and flashed to `usb:100000`.

```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000
```

Post-flash debug proof captured from `/dev/cu.usbmodem115553303`:

```text
[GDS_Teensy] hardware watchdog armed (12s)
[GDS_Teensy] debug port ready; data port is USB Serial
[GDS_Teensy] RF23 bridge ready (raw GDS channel + payload channel + RF segmentation)
[GDS_Teensy] counters uart_rx=0 ... rf_tx_pkt=0 ...
```

### Satellite Teensy — done and proven

The current satellite firmware compiled and was flashed to pinned physical ID
`usb:2100000`:

```text
ArtemisTeensy_N2_Baremetal/build/arduino-cli/satellite_teensy.ino.hex
```

Use the pinned upload ID whenever both boards are attached, then verify the
satellite debug stream:

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python - <<'PY'
import serial, time
with serial.Serial('/dev/cu.usbmodem115565001', 115200, timeout=0.5) as s:
    end = time.monotonic() + 6
    while time.monotonic() < end:
        print(s.read(4096).decode('utf-8', errors='replace'), end='')
PY
```

Expected marker: `[ArtemisTeensy]` counters. Final acceptance recorded zero
CRC, framing, parser-timeout, RF-TX, uplink-queue, and downlink-queue drops.

## Raspberry Pi C3M Access

- IP: `192.168.0.234`
- Network: `192.168.0.0/24`
- MAC observed: `b8:27:eb:4c:76:e6` (Raspberry Pi OUI)
- Hostname returned over SSH: `raspberrypi-c3m`
- Correct SSH alias: `artemis-pi-c3m` (or `c3m-pi`)
- SSH configuration: user `pi`, host `raspberrypi-c3m.local`, identity
  `~/.ssh/id_ed25519_artemis_pi`

Use the alias, not bare `raspberrypi-c3m` (bare hostname did not resolve on
this Mac):

```bash
ssh artemis-pi-c3m
ssh artemis-pi-c3m 'hostname; systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment'
```

Confirmed service state at handoff:

```text
raspberrypi-c3m
active
/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0
```

## F' Cross-Build and Pi Deployment

The final ARMv6/libuvc release is active at:

```text
/home/pi/artemis/releases/c3m-hil-uartflow37-channel
```

Binary SHA-256:

```text
7b2959e0e1957a6fa14b9a9ed2c2873234f0dccd4edf997e06a50189e9e47372
```

The service is active through `/home/pi/artemis/current`, uses
`LEPTON_CAMERA_BACKEND=uvc`, and opens `/dev/serial0` at `115200 8N1`. On this
Pi `/dev/serial0` resolves to `/dev/ttyS0`; that mapping was not introduced by
this work and is not the cause of the former bulk-transfer failure.

The validated transport configuration is:

- 37 ms base Pi UART drain margin
- 40 ms additional channel-0 drain margin
- 22 payload/retry messages per 1 Hz run
- 15 ms payload RF inter-packet gap
- ACKed ground-to-satellite CCSDS commands
- unACKed satellite telemetry and payload bulk
- RFM23BP `0x58=0xC0` at 125 kbps on both radios

Final acceptance evidence:

```text
source/ground SHA-256: 87b61b387647a4e732918b93a071fe51bf64b9b1a55ede6ff30e99289465ac26
receiver: 1100/1100, no retry request
application: sent=1100 total=1100 error=0
command-to-file: 58.557 s
viewer: 160x120, 19200 pixels
```

For a fresh build without Pi copy:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh --local-only
```

## Recommended Repeat Smoke

1. Re-enumerate both boards and preserve the pinned upload map.
2. Confirm the Pi service is active on `/dev/serial0` with the UVC backend.
3. Start GDS on ground channel 0 and `payload_receiver.py` on channel 1 using
   the exact command in `docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`.
4. Capture and downlink a fresh Lepton product; require final CRC/hash equality
   and an openable full-resolution viewer PNG.
5. Record command-to-file time and both Teensy counters. Do not claim a pass
   from process health or USB enumeration alone.
