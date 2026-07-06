# Student Windows Laptop Setup

BLUF: use native Windows for simple viewing/testing tools, and use WSL2 for F'
development, builds, and repo scripts.

## Which Path To Use

Use native Windows when you only need to:

- open `fprime-gds` in a browser
- run the Neutron 2 payload viewer
- inspect generated `.csv` or `.bin` payload files
- review demo output from another machine

Use WSL2 when you need to:

- build or edit the F' project
- run `fprime-util`
- run bash scripts under `ArtemisRpiTeensy_N2/tools`
- build or upload Teensy firmware with the repo Arduino CLI helpers
- run UART/GDS workflows against USB devices

## Native Windows Viewer Path

Install:

- Git for Windows
- Python 3.10 or newer
- Edge, Chrome, or Firefox

Clone the repo:

```powershell
git clone --branch neutron2-develop --recurse-submodules git@github.com:hsfl/fprime-artemis-cubesat.git
cd fprime-artemis-cubesat
```

Run the payload viewer:

```powershell
py -3 ground-station\neutron2-payload-viewer\neutron2_payload_viewer.py --port 8062
```

Open:

```text
http://127.0.0.1:8062
```

This path does not require WSL.

## WSL2 Developer Path

Install:

- WSL2 with Ubuntu
- Git inside Ubuntu
- Python, CMake, Ninja, and build tools inside Ubuntu
- Docker Desktop if using the Pi Zero W Docker cross-build path

Clone inside the WSL filesystem, not under `/mnt/c`, for fewer path and file
watching issues:

```bash
cd ~
git clone --branch neutron2-develop --recurse-submodules git@github.com:hsfl/fprime-artemis-cubesat.git fprime-artemis-cubesat
cd fprime-artemis-cubesat
```

For an existing checkout, update it to the student development branch and make
sure submodules are present:

```bash
cd ~/fprime-artemis-cubesat
git fetch origin
git switch neutron2-develop
git submodule update --init --recursive
```

Create the F' environment if it does not already exist, then activate it:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
python3 -m venv fprime-venv
. fprime-venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r lib/fprime/requirements.txt
```

After the first-time setup, activate the same environment before F' commands
from the repo root:

```bash
cd ~/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
```

Build:

```bash
cd ~/fprime-artemis-cubesat
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

## USB Devices From Windows Into WSL2

For Teensy or serial workflows, the USB device must be visible inside WSL.
Use `usbipd-win` from an administrator PowerShell window to attach the device to
the Ubuntu distro. Install `usbipd-win` from its official MSI, or use `winget`
when it is available on the laptop:

```powershell
winget install --id dorssel.usbipd-win --exact
```

Before plugging in the ground Teensy, list the current USB devices:

```powershell
usbipd list
```

Plug in the ground Teensy and list again:

```powershell
usbipd list
```

Find the new device entry and note its `BUSID`. From the same administrator
PowerShell window, bind and attach that `BUSID` to WSL:

```powershell
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
usbipd list
```

The attached device should show `Attached` in the `STATE` column. Then enter WSL:

```powershell
wsl
```

Check the Linux device names from WSL:

```bash
lsusb
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
dmesg | grep tty
```

Use the detected device path in repo commands:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/run_gds_uart.sh --port "$PORT"
```

Do not use `COM3`/`COM4` style names inside WSL repo scripts. Use the Linux
device path that WSL exposes.

When finished, detach from an administrator PowerShell window:

```powershell
usbipd detach --busid <BUSID>
```

Unplugging the USB cable also detaches the device.

If WSL sees the port but a repo command gets `Permission denied`, prefer fixing
Linux serial permissions and restarting WSL:

```bash
sudo usermod -aG dialout "$USER"
```

For a one-off demo emergency, running the specific serial command with `sudo`
can work, but make sure it still uses the repo's F' virtual environment and the
right port.

## Windows WSL2 HIL Demo Commands

Attach the ground Teensy to WSL before starting these commands. Use the first
ground Teensy triple-serial port for GDS and the third for payload reception.

Terminal 1, start `fprime-gds`:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
GDS_DATA_PORT=/dev/ttyACM0

./tools/run_gds_uart.sh \
  --port "$GDS_DATA_PORT" \
  --baud 115200 \
  --gui-port 5050
```

Open the GDS UI in a native Windows browser:

```text
http://127.0.0.1:5050
```

Terminal 2, start the payload receiver before requesting science downlink:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
GDS_PAYLOAD_PORT=/dev/ttyACM2
RUN_DIR=/tmp/neutron_hil/rf_demo_$(date +%Y%m%d_%H%M%S)
mkdir -p "$RUN_DIR"
echo "$RUN_DIR" | tee /tmp/neutron_hil/latest_rf_demo_dir

python3 -u tools/payload_receiver.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output "$RUN_DIR/payload_30s.bin" \
  --timeout 600
```

The full operator flow, expected events, and pass criteria live in
[`NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`](NEUTRON2_RF_MVP_DEMO_RUNBOOK.md).

## Pi Zero W Cross-Build

From WSL2 with Docker available:

```bash
cd ArtemisRpiTeensy_N2
export PI_ZERO_W_SSH_HOST=pi@artemis-pi.local
./tools/docker_cross_compile_pi_zero_w.sh
```

For a local artifact check only:

```bash
./tools/docker_cross_compile_pi_zero_w.sh --local-only
```

Use `--clean` only when the Docker image, Pi sysroot, or Python dependencies may
be stale.

## Common Windows Pitfalls

- Do not run F' build commands from native PowerShell unless a specific doc says
  that path is supported.
- Do not clone the developer workspace under `/mnt/c` unless you are only doing
  lightweight file edits.
- Do not paste another laptop's `/dev/cu.usbmodem...` path; Windows/WSL usually
  exposes attached serial devices as `/dev/ttyACM*` or `/dev/ttyUSB*`.
- Do not assume `/dev/ttyACM0` and `/dev/ttyACM2` forever; enumerate after each
  reconnect and map the ground Teensy's triple-serial ports before the demo.
- Keep browser/viewer tasks native when possible. WSL is for development and
  device-facing workflows.
