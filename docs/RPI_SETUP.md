# Raspberry Pi Quick Start (Beginner Guide)

This guide is for new students.

Goal: get the satellite Raspberry Pi running the F' app in this repo, using the
single UART link to talk to the satellite Teensy.

For the current Artemis prototype, assume the target is a Raspberry Pi Zero W.
That means 32-bit Raspberry Pi OS and ARMv6 compatibility matter. Do not install
a 64-bit OS for the Pi Zero W path.

Normal iteration should use the Docker cross-compile flow in
`docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`; native builds on the Pi work,
but they are slow and should be treated as the simple fallback/manual path.

You only need to do 4 things:
1. Flash Raspberry Pi OS
2. Enable UART
3. Build or deploy F' for the Pi
4. Run a quick test

---

## What you need

- Raspberry Pi (with SD card)
- SD card reader for your laptop
- Power supply for the Pi
- Internet on the Pi
- This repo copied to the Pi
- Teensy board + USB cable
- 3 jumper wires for UART + GND

---

## 1) Flash Raspberry Pi OS

Use **Raspberry Pi Imager** on your laptop.

1. Open Raspberry Pi Imager.
2. Choose device: your Pi model.
3. Choose OS: **Raspberry Pi OS Lite (32-bit)** for Raspberry Pi Zero W.
4. Choose storage: your SD card.
5. Click **Next** and complete setup.

Recommended in the Imager advanced settings:
- Set hostname
- Enable SSH
- Set username/password
- Configure Wi-Fi

When flash is done:
1. Put SD card into Pi.
2. Boot the Pi.
3. Log in.
4. Update packages:

```bash
sudo apt update && sudo apt upgrade -y
```

---

## 2) Wire and enable UART

### A) Wire Pi <-> Teensy

Use 3.3V TTL UART only (not RS-232).

- Pi `GPIO14/TXD` (physical pin 8) -> Teensy 4.1 `pin 7` (`Serial2 RX2`)
- Pi `GPIO15/RXD` (physical pin 10) -> Teensy 4.1 `pin 8` (`Serial2 TX2`)
- Pi `GND` -> Teensy `GND`

Important:
- TX goes to RX (crossed)
- Share ground

### B) Enable UART on the Pi

Run:

```bash
sudo raspi-config
```

Then:
1. `Interface Options` -> `Serial Port`
2. "Login shell over serial?" -> **No**
3. "Enable serial hardware?" -> **Yes**

Reboot:

```bash
sudo reboot
```

Check UART exists (make sure the Teensy is on!)

```bash
ls -l /dev/serial0
```

If it prints a device link, UART is ready.

---

## 3) Build F' on the Pi

This native build path is simple but slow. Prefer the cross-compile guide for
normal development:

- `docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`

From the Pi:

```bash
cd ~/fprime-artemis-cubesat
```

Create/activate the project virtual environment:

```bash
python3 -m venv ArtemisRpiTeensy_N2/fprime-venv
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
```

Install tools (safe to re-run):

```bash
python -m pip install --upgrade pip
python -m pip install -r ArtemisRpiTeensy_N2/lib/fprime/requirements.txt
```

Build:

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

If build succeeds, your Pi can compile this F' project.

---

## 4) Quick runtime test (does F' work?)

Run the deployment:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

Pass condition:
- App starts
- You see startup logs
- Process stays running (no immediate crash)

Stop with `Ctrl+C`.

---

## Optional: build/upload Teensy firmware

### On The Pi

```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### From macOS Laptop

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### From Windows Laptop (WSL2)

Attach the Teensy USB device to WSL first, then run:

```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

---

## Fast troubleshooting

- `/dev/serial0` missing:
  - Re-run `raspi-config` serial settings
  - Reboot

- Permission denied on serial device:

```bash
sudo usermod -aG dialout $USER
```

Then log out and log back in.

- App starts but no UART traffic:
  - Recheck TX/RX crossed wiring
  - Recheck common ground
  - Confirm both sides are `115200 8N1`
