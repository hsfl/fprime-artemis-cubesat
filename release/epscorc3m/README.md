# EPSCoR C3M Demo Frozen Release

This bundle freezes the laptop-operated EPSCoR C3M Lepton RF demo validated by
three consecutive live HIL runs on 2026-07-09. The normal operator uses F Prime
GDS for channel-0 mission operations and the C3M payload receiver web app for
channel-1 progress, CRC proof, thermal decode, and history.

The exact tag, commit, submodule revisions, artifact sizes, and hashes are in
`RELEASE_INFO.txt` and `SHA256SUMS`. Run `shasum -a 256 -c SHA256SUMS` from the
bundle root before flashing or deploying.

## Contents

- `artifacts/teensy-ground/`: validated Triple Serial ground HEX and ELF.
- `artifacts/teensy-satellite/`: validated satellite HEX and ELF.
- `artifacts/rpi/`: validated Pi Zero W ARMv6/libuvc deployment and dictionary.
- `scripts/`: pinned-ID flash scripts and atomic Pi release installer.
- `ArtemisRpiTeensy_N2/tools/`: GDS launcher and CLI receiver fallback.
- `ground-station/`: primary C3M web receiver, decoder, and replay sample.
- `docs/`: operator runbook, HIL evidence, and bench handoff.
- `source/`: a complete tracked-source archive for the tagged commit. Git
  submodule contents are intentionally not embedded; their exact revisions are
  recorded in `RELEASE_INFO.txt`.

Build caches, virtual environments, sysroots, live captures, credentials, and
external reference repositories are deliberately excluded.

## Flash the Teensys

Prerequisite: `arduino-cli` with the Teensy core installed. When both boards
are connected, use only the physical `usb:*` IDs. Never substitute a
`/dev/cu.*`, `/dev/tty*`, or COM-port name.

```bash
./scripts/flash_ground_teensy.sh usb:100000
./scripts/flash_satellite_teensy.sh usb:2100000
```

After each flash, prove the runtime role from the debug stream:

- ground: `[GDS_Teensy]`
- satellite: `[ArtemisTeensy]`

The scripts upload the frozen HEX directly and do not rebuild it.

## Deploy the Raspberry Pi Zero W Release

The target must be a Pi Zero W/ARMv6 system with the runtime libraries for
`libuvc.so.0`, `libusb-1.0.so.0`, `libudev.so.1`, and `libjpeg.so.62` installed.
The installer rejects missing libraries, installs an immutable release under
`/home/pi/artemis/releases/`, atomically updates `current`, restarts the service,
and rolls back the symlink if health validation fails.

```bash
./scripts/deploy_rpi_release.sh artemis-pi-c3m
```

For a new Pi, install the included unit once before running the installer:

```bash
scp artifacts/rpi/artemis-fprime.service artemis-pi-c3m:/tmp/
ssh artemis-pi-c3m \
  'sudo install -m 0644 /tmp/artemis-fprime.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable artemis-fprime.service'
```

The service sets `LEPTON_CAMERA_BACKEND=uvc` and uses `/dev/serial0` at
`115200 8N1`. The release contains the flight deployment and dictionary; the
separate live-camera harness is not required to operate the service.

## Start the Laptop Ground Tools

Use Python 3.11 or another version supported by F Prime 4.2.1:

```bash
python3 -m venv .release-venv
. .release-venv/bin/activate
python3 -m pip install -r requirements-ground.txt
```

Start the primary channel-1 web app first:

```bash
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py
```

Wait for `Ready — awaiting downlink`. Then start GDS on the first ground
Triple Serial interface:

```bash
fprime-gds -n \
  --dictionary artifacts/rpi/ArtemisRpiTeensyDeploymentTopologyDictionary.json \
  --communication-selection uart \
  --uart-device /dev/cu.usbmodem115553301 \
  --uart-baud 115200 \
  --gui-port 5050 \
  --framing-selection space-packet-space-data-link
```

Select the actual enumerated port if it differs. macOS is the primary student
path; Windows F Prime operation uses WSL2 with the USB device attached.

Follow `docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md` for the exact command order.
The current frozen acceptance rule remains complete reception plus CRC proof.
The best-effort `NaN`/white missing-pixel behavior is documented follow-on work,
not part of this release.

## Offline Viewer Check

```bash
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py \
  --replay-fdp ground-station/c3m-lepton-test-data/Dp_20260707_120740.fdp \
  --no-open
```

This confirms the laptop receiver/decode path without hardware; it does not
replace live HIL proof.

## Rebuilding from the Tag

GitHub-generated source archives do not populate submodules. Clone recursively:

```bash
git clone --recurse-submodules --branch v1.0.0-epscorc3m-demo <repository-url>
```

Then follow the build instructions in the repository. Do not assume a generic
ARM hard-float binary is Pi Zero W compatible; the deployment must remain ARMv6
`v6KZ`, `VFPv2`, with interpreter `/lib/ld-linux-armhf.so.3`.
