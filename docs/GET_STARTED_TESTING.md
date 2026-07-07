# Get Started Testing

This is the student-facing router for testing the Neutron 2 MVP demo. Keep the
actual detailed procedures in the focused runbooks under `docs/`.

## Which Guide To Use

Use these docs first:

- `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`
  - real hardware-in-the-loop demo
  - Raspberry Pi -> satellite Teensy -> RFM23BP -> ground Teensy -> laptop
  - use this for team rehearsal and demo proof
- `EMULATION.md`
  - fastest laptop-only check
  - no hardware required
  - good for command/event/telemetry and payload-flow rehearsal
- `docs/NEUTRON2_LOCAL_EMULATION_RUNBOOK.md`
  - manual walkthrough of the local Neutron 2 demo story
- `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`
  - layer-by-layer software troubleshooting when a runbook fails
- `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md`
  - Windows student setup
  - native Windows for browser/viewer tasks
  - WSL2 for F' builds, repo scripts, UART/GDS, and USB device workflows
- `docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`
  - preferred Raspberry Pi Zero W build/deploy path
  - native Pi builds work, but cross-compile is faster

## MVP intent

- Raspberry Pi runs the F' deployment and owns mission logic.
- Satellite Teensy handles microcontroller-side subsystem/radio work.
- Laptop is used for operator UI through `fprime-gds` and the Neutron 2 payload
  viewer.
- For the current MVP/HIL path, use the full RPi -> satellite Teensy -> RF -> ground Teensy -> laptop chain when hardware is available.
- The Pi <-> satellite Teensy link is a single physical UART with tagged virtual
  channels because of the current hardware implementation.

## Current Full-Chain Test

For the current team demo, the important proof is:

1. `fprime-gds` receives live events/telemetry over the RF channel 0 path.
2. Operator commands `Base Mode`, SOH snapshot, scheduled collection, and
   science downlink through GDS or `fprime-cli`.
3. `tools/payload_receiver.py` reconstructs the channel 1 payload file.
4. Local payload hash matches the Pi latest payload file.
5. `ground-station/neutron2-payload-viewer` parses the reconstructed payload.
6. Pi journal shows `PayloadDownlinkProgress`, `PayloadDownlinkComplete`, and
   `CommsManager.DownlinkFinished`.

The maintained procedure for this is `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`.

## Important architecture note

- Teensy has two different serial paths in this setup:
1. `Serial2` (pins 7/8): UART link to Raspberry Pi (`/dev/serial0`) for the
   channelized mission path.
2. USB serial (`Serial`): satellite Teensy debug console to your laptop.

The USB serial link is for logs/debug only.  
On the satellite Teensy, USB serial is for logs/debug only.
On the ground Teensy, USB serial paths are operator data paths:

- channel 0 data port for `fprime-gds`
- debug port for counters/status
- payload port for channel 1 receiver when triple-serial USB is enabled

Also important:
- Current Teensy UART/RF contract is documented in `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`.
- Nominal endpoint framing is still end-to-end `ComCcsds` / `space-packet-space-data-link`.
- The Pi <-> satellite Teensy UART adds a channel tag below that endpoint layer: channel 0 for CCSDS over RF, channel 1 for payload over RF, channel 2 for satellite-local PDU/EPS RPC.

## Verified GDS communication flags (local check)

From local CLI help (`fprime-gds --help`):
- `--communication-selection {uart,ip,none}`
- `--uart-device DEVICE`
- `--uart-baud BAUD`
- `--uart-skip-port-check`

## Build Software

### Raspberry Pi F'

Preferred path:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh
```

Windows WSL2:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh
```

Native Pi fallback is documented in `docs/RPI_BUILD.md`.

### macOS Laptop Teensy Firmware

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### Windows Laptop Teensy Firmware (WSL2)

Attach the Teensy USB device to WSL first, then run:

```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

Ground Teensy firmware uses the same pattern from `GDS_Teensy/`.

## Power-on order

1. Power on Teensy first.
2. Teensy should assert Pi enable pin and onboard LED high at boot.
3. Wait ~30-60 seconds for Pi to fully boot.
4. Then connect/SSH to the Pi from laptop.

## Quick hardware sanity check from laptop

Open Teensy USB serial monitor at `115200` baud.  
You should see boot logs like:

- `RPI power enable asserted (pin 36 HIGH)`
- `LED asserted (pin 13 HIGH)`

If those are missing, stop and fix Teensy first.

## MVP pass/fail checks

Pass if all are true:
1. Teensy boots and prints RPi enable + LED high logs on USB serial.
2. RPi deployment process stays running.
3. GDS opens at `http://127.0.0.1:5050` from the laptop.
4. Live events/telemetry move in GDS.
5. The RF MVP runbook command sequence completes.
6. Payload receiver reconstructs a file and the payload viewer parses it.

## If something fails

- No `/dev/serial0` on Pi:
  - Recheck UART setup in `docs/RPI_SETUP.md`
- Deployment exits immediately:
  - Recheck UART wiring and common ground.
- GDS page not opening:
  - Confirm `./tools/run_gds_uart.sh --port <ground-data-port>` is running.
  - Use GUI port `5050`, not `5000`, unless you intentionally override it.
- GDS connected but no useful data:
  - Verify app is running and Teensy is powered.
  - Verify framing is `space-packet-space-data-link`.
  - Confirm the selected serial device is the ground Teensy channel 0 data port.
