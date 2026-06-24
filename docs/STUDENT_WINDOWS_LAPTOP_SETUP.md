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
git clone --recurse-submodules git@github.com:hsfl/fprime-artemis-cubesat.git
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
git clone --recurse-submodules git@github.com:hsfl/fprime-artemis-cubesat.git fprime-artemis-cubesat
cd fprime-artemis-cubesat
```

Create or activate the F' environment:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
```

Build:

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f
fprime-util build
```

## USB Devices From Windows Into WSL2

For Teensy or serial workflows, the USB device must be visible inside WSL.
Use `usbipd-win` from an administrator PowerShell window to attach the device to
the Ubuntu distro, then check Linux device names from WSL:

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Use the detected device path in repo commands:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/run_gds_uart.sh --port "$PORT"
```

Do not use `COM3`/`COM4` style names inside WSL repo scripts. Use the Linux
device path that WSL exposes.

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
- Keep browser/viewer tasks native when possible. WSL is for development and
  device-facing workflows.
