# Raspberry Pi Setup Guide (Neutron 2 MVP)

This guide sets up the Raspberry Pi side for the active architecture in this repo:

- RPi runs F' deployment: `ArtemisRpiTeensy_N2`
- Teensy runs baremetal relay: `ArtemisTeensy_N2_Baremetal`
- Transport is a single UART link at `115200 8N1`

## 1) Platform

Use Linux on the Raspberry Pi for this project.

Why:
- The current deployment already uses `Drv.LinuxUartDriver`.
- Runtime/startup flow and docs in this repo assume Linux (`-d /dev/serial0`).

## 2) Hardware Wiring (RPi <-> Teensy UART)

Use 3.3V TTL UART levels.

- RPi `GPIO14/TXD` (physical pin 8) -> Teensy `RX`
- RPi `GPIO15/RXD` (physical pin 10) -> Teensy `TX`
- RPi `GND` -> Teensy `GND`

Notes:
- Cross TX/RX.
- Do not use RS-232 voltage levels.
- Keep wiring short for bring-up.

## 3) Raspberry Pi OS UART Configuration

On the Raspberry Pi:

```bash
sudo raspi-config
```

In the menu:
1. `Interface Options` -> `Serial Port`
2. Login shell over serial: `No`
3. Enable serial hardware: `Yes`

Reboot:

```bash
sudo reboot
```

After reboot, verify UART device:

```bash
ls -l /dev/serial0
```

Optional checks:

```bash
# Bookworm commonly uses /boot/firmware/config.txt
# Older images may use /boot/config.txt
grep -E "^enable_uart=1" /boot/firmware/config.txt /boot/config.txt 2>/dev/null

# Ensure no serial console service is attached to the same UART
systemctl status serial-getty@serial0.service --no-pager
```

If `serial-getty@serial0.service` is active, disable it:

```bash
sudo systemctl disable --now serial-getty@serial0.service
```

## 4) Prepare Repo and F' Environment on RPi

Clone/copy this repo to the Pi, then from repo root:

```bash
cd /path/to/fprime-artemis-cubesat
```

If `ArtemisRpiTeensy_N2/fprime-venv` already exists, use it:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
```

If it does not exist yet, create one:

```bash
python3 -m venv ArtemisRpiTeensy_N2/fprime-venv
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
pip install --upgrade pip
pip install fprime-tools
```

## 5) Build the RPi Deployment

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

Expected runtime binary (Linux):

```bash
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment
```

## 6) Run the Deployment over UART

Default UART device in code is `/dev/serial0`.

```bash
cd ArtemisRpiTeensy_N2
./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0
```

You should see startup output and the process should remain alive.

Reference points in code:
- Default UART CLI arg: `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Main.cpp`
- UART open settings (`BAUD_115K`, no flow, no parity):
  `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/ArtemisRpiTeensyDeploymentTopology.cpp`

## 7) Teensy Side Expectations

Teensy firmware must match these UART settings:
- `115200 8N1`
- Framing/CRC behavior documented in:
  `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`

Build/upload Teensy from this repo:

```bash
cd /path/to/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh /dev/ttyACM0
```

## 8) Bring-Up Smoke Checklist

1. Teensy firmware is running and prints relay-ready logs on its debug serial.
2. RPi app starts with no init assertion failures.
3. UART cable disconnect/reconnect does not kill the RPi app process.
4. GDS command `TeensyLink.LINK_STATUS` shows counter updates.

## 9) Troubleshooting

- `/dev/serial0` missing:
  - Recheck `raspi-config` serial settings.
  - Reboot.

- Permission denied opening UART:
  - Add your user to `dialout` and re-login:

```bash
sudo usermod -aG dialout $USER
```

- No traffic / framing errors:
  - Check TX/RX cross wiring and common ground.
  - Confirm both sides are exactly `115200 8N1`.
  - Review counters (`crcDrops`, `framingDrops`, `timeoutEvents`) per UART contract doc.
