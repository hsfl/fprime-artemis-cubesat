# Get Started Testing (Temporary MVP Guide)

This is a temporary first-pass test flow for students.

Assumes `/Users/sozodennis/Developer/fprime-artemis-cubesat/RPI_SETUP.md` is already completed.

## MVP intent

- Teensy powers/enables the Raspberry Pi.
- Raspberry Pi runs the F' deployment.
- Laptop is used for operator UI (F' GDS dashboard).
- For MVP, skip radio path and use direct RPi <-> Teensy UART wiring.

## Full chain test matrix (what you asked for)

1. **GDS process on RPi**
   - Goal: verify `fprime-gds` itself launches on RPi.
   - Verified CLI note: `--communication-selection uart` is a valid option in local `fprime-gds --help`.
   - Command:
   ```bash
   cd /path/to/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment
   . ../fprime-venv/bin/activate
   fprime-gds --no-app --communication-selection none
   ```
   - Pass: dashboard opens at port 5000.

2. **F' bytes between RPi and Teensy (observe Teensy USB logs)**
   - Goal: verify RPi UART traffic reaches Teensy.
   - Current status: **partially blocked** for packet visibility.
   - Why: current Teensy firmware does not mirror raw UART packet bytes to USB, only status logs.

3. **F' bytes between RPi -> Teensy -> laptop (no radio)**
   - Goal: laptop receives the same stream over USB from Teensy.
   - Current status: **blocked in current firmware**.
   - Why: Teensy currently uses a custom UART framing contract (`0xD4 0xC3 + len + crc`) and does not provide a transparent USB bridge path for GDS packets.
   - Once transparent bridge mode exists, laptop-side command will be:
   ```bash
   fprime-gds --no-app \
     --communication-selection uart \
     --uart-device /dev/ttyACM0 \
     --uart-baud 115200 \
     --framing-selection space-packet-space-data-link
   ```

4. **F' bytes between RPi -> Teensy -> RF23 -> laptop USB SDR**
   - Goal: full RF chain to laptop.
   - Current status: **blocked for MVP**.
   - Why: SDR demod/decoder path is not implemented in this repo yet.
   - Practical near-term alternative: use a second RF23+Teensy ground node, then USB into laptop.

## Important architecture note

- Teensy has two different serial paths in this setup:
1. `Serial2` (pins 7/8): UART link to Raspberry Pi (`/dev/serial0`) for mission data path.
2. USB serial (`Serial`): debug console to your laptop.

The USB serial link is for logs/debug only.  
F' GDS does not command through Teensy USB serial in this MVP.

Also important:
- Current Teensy UART contract is custom-framed in `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`.
- So today, packet framing is **not purely end-to-end F' framing** across the chain.

## Verified GDS communication flags (local check)

From local CLI help (`fprime-gds --help`):
- `--communication-selection {uart,ip,none}`
- `--uart-device DEVICE`
- `--uart-baud BAUD`
- `--uart-skip-port-check`

## 1) Build software

### On Raspberry Pi (F')

```bash
cd /path/to/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

### On your laptop or Pi (Teensy firmware)

```bash
cd /path/to/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## 2) Power-on order (important)

1. Power on Teensy first.
2. Teensy should assert Pi enable pin and onboard LED high at boot.
3. Wait ~30-60 seconds for Pi to fully boot.
4. Then connect/SSH to the Pi from laptop.

## 3) Quick hardware sanity check from laptop (optional but recommended)

Open Teensy USB serial monitor at `115200` baud.  
You should see boot logs like:

- `RPI power enable asserted (pin 36 HIGH)`
- `LED asserted (pin 13 HIGH)`

If those are missing, stop and fix Teensy first.

## 4) Run F' deployment on the Raspberry Pi

SSH into the Pi and run:

```bash
cd /path/to/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

Expected:
- Process starts.
- No immediate assert/crash.
- Process stays alive.

Leave this terminal running.

## 5) Run F' GDS on the Raspberry Pi

Open a second SSH terminal to the Pi:

```bash
cd /path/to/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment
. ../fprime-venv/bin/activate
fprime-gds --no-app
```

Note: `--no-app` is used because the app is already running in step 4.

## 6) View GDS on your laptop

Use SSH port forwarding from laptop:

```bash
ssh -L 5000:127.0.0.1:5000 <user>@<rpi-ip>
```

Then open on laptop:

- `http://127.0.0.1:5000`

## 7) MVP pass/fail checks

Pass if all are true:
1. Teensy boots and prints RPi enable + LED high logs on USB serial.
2. RPi deployment process stays running.
3. GDS dashboard opens from laptop (via tunnel).
4. You can send at least one command (example: `TeensyLink.LINK_STATUS`) and receive resulting data/events.

## 8) If something fails

- No `/dev/serial0` on Pi:
  - Recheck UART setup in `/Users/sozodennis/Developer/fprime-artemis-cubesat/RPI_SETUP.md`
- Deployment exits immediately:
  - Recheck UART wiring and common ground.
- GDS page not opening:
  - Confirm SSH tunnel command is active.
- GDS connected but no useful data:
  - Verify app is running in step 4 and Teensy is powered.
