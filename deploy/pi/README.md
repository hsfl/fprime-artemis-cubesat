# Raspberry Pi Provisioning

BLUF: this folder versions the Pi service and the minimum blank-SD-card steps for
the Neutron 2 MVP demo. Do not run these commands against the bench Pi unless
you are in a bench session and intend to change the Pi.

## Target

- Hardware: Raspberry Pi Zero W on the satellite Artemis OBC.
- OS: Raspberry Pi OS Lite 32-bit.
- User account used by current docs: `pi`.
- Runtime UART: `/dev/serial0` to the satellite Teensy.
- Service name to use going forward: `artemis-fprime.service`.
- Runtime layout on the Pi:

```text
/home/pi/artemis/current/
  ArtemisRpiTeensyDeployment -> release or build artifact binary
  ArtemisRpiTeensyDeploymentTopologyDictionary.json -> matching dictionary
```

The Pi Zero W is ARMv6. Do not use generic ARMv7 Raspberry Pi artifacts. For
the cross-compile landmine and verification flow, use the existing docs:

- `docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`
- `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`
- `docs/RPI_BUILD.md` for the slow native fallback

## Parameter Persistence Note

The service runs with `WorkingDirectory=/home/pi/artemis/current`, so F Prime
writes `PrmDb.dat` inside the current release directory. When the `current`
symlink is swapped to a new release, saved parameters can silently reset because
the new directory has no old `PrmDb.dat`.

After installing a new release, either re-run the needed `PRM_SET`/`PRM_SAVE`
commands or copy `PrmDb.dat` forward intentionally.

## Blank SD To Demo-Ready Pi

### 1. Flash the OS

macOS first:

1. Open Raspberry Pi Imager.
2. Select Raspberry Pi Zero W.
3. Select Raspberry Pi OS Lite 32-bit.
4. In advanced settings, enable SSH, configure Wi-Fi, and set the username to
   `pi` unless the team intentionally changes the service user.
5. Flash the SD card, insert it into the Pi, boot, and SSH in:

```bash
ssh pi@artemis-pi.local
```

Windows/WSL2 note: use Raspberry Pi Imager on Windows, then SSH from WSL2 or
Windows Terminal:

```bash
ssh pi@artemis-pi.local
```

### 2. Update packages and enable UART

Run on the Pi:

```bash
sudo apt update
sudo apt upgrade -y
sudo raspi-config nonint do_serial_hw 0
sudo raspi-config nonint do_serial_cons 1
sudo systemctl disable --now serial-getty@serial0.service || true
sudo reboot
```

SSH back in and verify:

```bash
ls -l /dev/serial0
groups
```

If serial permissions are wrong:

```bash
sudo usermod -aG dialout pi
sudo reboot
```

### 3. Create the runtime directories

Run on the Pi:

```bash
mkdir -p /home/pi/artemis/releases /home/pi/artemis/current /home/pi/artemis/logs
```

### 4. Copy the frozen release artifacts

On macOS, download the release zip from GitHub:

```bash
cd ~/Developer/fprime-artemis-cubesat
gh release download v1.0.0-mvp-demo \
  --repo hsfl/fprime-artemis-cubesat \
  --pattern 'neutron2-v1.0.0-mvp-demo-artifacts.zip' \
  --dir /tmp/neutron2-release
unzip -l /tmp/neutron2-release/neutron2-v1.0.0-mvp-demo-artifacts.zip
```

Copy the zip to the Pi:

```bash
scp /tmp/neutron2-release/neutron2-v1.0.0-mvp-demo-artifacts.zip pi@artemis-pi.local:/home/pi/artemis/releases/
```

On the Pi, unpack and link the current binary and dictionary:

```bash
cd /home/pi/artemis/releases
mkdir -p v1.0.0-mvp-demo
unzip -o neutron2-v1.0.0-mvp-demo-artifacts.zip -d v1.0.0-mvp-demo

APP="$(find /home/pi/artemis/releases/v1.0.0-mvp-demo -type f -path '*/pi-zero-w-armv6hf/*/bin/ArtemisRpiTeensyDeployment' | head -n 1)"
DICT="$(find /home/pi/artemis/releases/v1.0.0-mvp-demo -type f -name 'ArtemisRpiTeensyDeploymentTopologyDictionary.json' -path '*/pi-zero-w-armv6hf/*/dict/*' | head -n 1)"

test -x "$APP"
test -f "$DICT"
ln -sfn "$APP" /home/pi/artemis/current/ArtemisRpiTeensyDeployment
ln -sfn "$DICT" /home/pi/artemis/current/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

Windows/WSL2 note: run the same `gh`, `scp`, and `ssh` commands from WSL2 with
the repo at `~/fprime-artemis-cubesat`.

### 5. Install and enable the systemd unit

From macOS:

```bash
cd ~/Developer/fprime-artemis-cubesat
scp deploy/pi/artemis-fprime.service pi@artemis-pi.local:/tmp/artemis-fprime.service
ssh pi@artemis-pi.local 'sudo install -m 0644 /tmp/artemis-fprime.service /etc/systemd/system/artemis-fprime.service && sudo systemctl daemon-reload'
```

Run on the Pi:

```bash
sudo systemctl enable artemis-fprime.service
sudo systemctl start artemis-fprime.service
systemctl is-active artemis-fprime.service
journalctl -u artemis-fprime.service --since "2 minutes ago" --no-pager | tail -100
```

Windows/WSL2 note: use the same commands from WSL2, adjusting the local repo
path to `~/fprime-artemis-cubesat`.

## Migration From The Existing `ln` Service

Run on the bench Pi at next bench session -- NOT done automatically.

The sprint audit says the currently deployed service is named `ln`, which is a
student debugging trap because it looks like the coreutils command. Migrate it
explicitly:

```bash
ssh pi@artemis-pi.local

systemctl status ln.service --no-pager -l || true
sudo systemctl disable --now ln.service || true
sudo rm -f /etc/systemd/system/ln.service

sudo install -m 0644 /tmp/artemis-fprime.service /etc/systemd/system/artemis-fprime.service
sudo systemctl daemon-reload
sudo systemctl enable --now artemis-fprime.service

systemctl is-active artemis-fprime.service
pgrep -af ArtemisRpiTeensyDeployment
journalctl -u artemis-fprime.service --since "2 minutes ago" --no-pager | tail -100
```

If `/tmp/artemis-fprime.service` is not already on the Pi, copy it first from
the repo:

```bash
scp deploy/pi/artemis-fprime.service pi@artemis-pi.local:/tmp/artemis-fprime.service
```

## Spare SD Card Bench Mitigation

The highest-value bench mitigation is a second flashed SD card kept in the bench
kit. It should have:

- Raspberry Pi OS Lite 32-bit for Pi Zero W.
- SSH and Wi-Fi configured for the bench network.
- User `pi` or a documented replacement with the service file updated.
- UART enabled on `/dev/serial0`, serial console disabled, and `pi` in `dialout`.
- `/home/pi/artemis/current` linked to the `v1.0.0-mvp-demo` binary and matching
  dictionary.
- `/etc/systemd/system/artemis-fprime.service` installed and enabled.
- A label with date flashed, release tag, hostname, username, and Wi-Fi profile.

## Quick Checks

```bash
ssh pi@artemis-pi.local 'readlink -f /home/pi/artemis/current/ArtemisRpiTeensyDeployment'
ssh pi@artemis-pi.local 'readlink -f /home/pi/artemis/current/ArtemisRpiTeensyDeploymentTopologyDictionary.json'
ssh pi@artemis-pi.local 'systemctl is-active artemis-fprime.service; pgrep -af ArtemisRpiTeensyDeployment || true'
ssh pi@artemis-pi.local 'journalctl -u artemis-fprime.service --since "2 minutes ago" --no-pager | tail -100'
```
