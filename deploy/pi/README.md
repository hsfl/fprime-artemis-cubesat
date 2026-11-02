> **N2 CDR service (2026-10-26):** The current `artemis-fprime.service` uses
> account `n2-obc`, `/home/n2-obc/artemis/state`, TCP port 50000, and
> `/dev/serial0`; the older `pi` instructions below are historical. Install
> `artemis-supervisor.py` as `/home/n2-obc/artemis/artemis-supervisor.py` and
> the unit as `/etc/systemd/system/artemis-fprime.service` together. The
> supervisor waits for the UART and declares readiness after a fresh EPS status
> text event and a TCP listener on port 50000 owned by the F′ process. It
> restarts a child that exits or stops producing that event (90 s
> startup grace, 30 s progress timeout). This detects EPS polling/log progress,
> not TCP commands, payload, PDU, or full-system health. It depends on the CDR
> topology and ACTIVITY_LO text events remaining enabled. The listener is checked
> for startup readiness only; runtime supervision continues to monitor EPS
> progress. Systemd's 45 s
> watchdog also detects a stalled supervisor after readiness. Check with
> `python3 deploy/pi/test_supervisor.py` on Linux; those tests use no hardware
> and require Linux `/proc` for socket ownership checks.

## Current release installer

For the Pi Zero W `n2-obc` only, stage a verified ARMv6 CDR Release binary,
its matching dictionary, `build-manifest.json`, the supervisor, service unit,
installer, and a basename-relative `SHA256SUMS`. Run on the Pi:

```sh
sudo bash /path/to/staging/install-n2-release.sh /path/to/staging
```

The current installer selects `d4fe283-cdr-tcp-20261026`, validates hashes and
shared libraries, preserves persistent state/science data, records rollback
files, and selects the paired release. It refuses an existing release directory
so a repeated install cannot overwrite the artifacts needed by rollback.
Choose a new release name in the installer for a subsequent release. Executable
rollback helpers are prepared before configuration changes; newly created
runtime directories belong to `n2-obc`. Active NFS mounts and unresolved
shared libraries stop installation explicitly. It uses supported zram-only swap,
masks unused NFS/display-test/EEPROM helpers and user audio services, disables
unused audio/camera/DSI autodetection, and caps persistent journals at 32 MiB.
HDMI console, Wi-Fi, SSH, Avahi, and the OS hardware watchdog remain available.
Reboot after applying the swap configuration. NetworkManager/Netplan packages
and network profiles are retained. The Pi 5 is outside this installer's scope.

The installer prints a `setup-backup-...-n2-update` directory containing
`rollback.sh`; run it with sudo, then reboot, to restore the prior release and
configuration. It retains all releases and science/state data.

For a desktop Raspberry Pi OS image, the additional
`disable-desktop-services.sh` helper masks installed GUI/VNC, printing,
Bluetooth, desktop disk/power/package helpers, keyrings, portals and GVFS
services. Run it with sudo on `n2-obc`, then reboot. It preserves packages,
network/SSH services and the text console, and prints a separate
`desktop-backup-.../rollback.sh` path. Most of these services are already idle
under `multi-user.target`, so masking them prevents later desktop activation
without implying a large additional boot or RAM saving.

The tests require Linux `/proc` socket ownership information:

```sh
python3 deploy/pi/test_supervisor.py
```

Run them on Linux or in a Linux container when developing from macOS. Readiness
requires an F′-owned listener on TCP 50000 plus fresh EPS polling progress.
A delayed UART waits in the same supervisor, without consuming restart attempts.
After readiness, EPS progress silence for 30 seconds triggers child cleanup and
restart. This is a partial application liveness check; it does not prove PDU,
payload, or end-to-end TCP command health. The 45-second systemd watchdog checks
the supervisor itself. See the dated deployment record for actual target checks.

# Historical MVP provisioning reference

All remaining instructions below describe the older user `pi` / UART-only
MVP deployment. Do not apply them over the current N2 CDR installation.


> Earlier Artemis/GDS/RFM23BP workflow reference. For current N2 Yamcs operations and integration status, start at [the documentation index](../../docs/README.md). This page does not establish deployment of the new N2 software baseline.

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

## Team Access And Wi-Fi Priority

Current bench policy:

- Keep the runtime service on user `pi`; do not change
  `artemis-fprime.service` to a separate team account unless the runtime layout
  is intentionally migrated too.
- Team SSH login can use username `pi` plus the shared password distributed
  out-of-band. Dennis's SSH key path should remain enabled as the recovery path.
- SSH should allow both password and public-key login. On a live Pi this can be
  made explicit with a small drop-in at
  `/etc/ssh/sshd_config.d/90-neutron2-team-login.conf`:

```text
PasswordAuthentication yes
PubkeyAuthentication yes
```

Wi-Fi profiles should be ranked with NetworkManager priorities:

| Priority | Network | Purpose |
| --- | --- | --- |
| `200` | `TP-Link_9080` | Primary HIL/team network |
| `100` | `TP-Link_A168` | Secondary bench fallback |
| `1` | phone hotspot profile | Last-resort fallback, if configured |

Do not commit Wi-Fi or SSH passwords into this repo. Share them out-of-band.

## Boot And Service Startup Policy

Current bench policy:

- Disable `cloud-init` after the Pi is provisioned. It is useful for first-boot
  image setup, but it adds avoidable boot time on a fixed HIL bench Pi.
- Keep `artemis-fprime.service` independent of Wi-Fi. The deployment talks to
  the satellite Teensy over `/dev/serial0`, so it should not wait for
  `network-online.target`.
- Keep SSH/network setup available for operators, but do not make the flight
  deployment depend on it.

The live bench Pi has `/etc/cloud/cloud-init.disabled` present and the cloud-init
boot units disabled. If a new SD card is flashed with cloud-init-based first-boot
customization, apply the disable step only after the first boot and SSH access
are confirmed.

The Pi Zero W is ARMv6. Do not use generic ARMv7 Raspberry Pi artifacts. For
the cross-compile landmine and verification flow, use the existing docs:

- `docs/integration/pi-zero-w/CROSS_COMPILE.md`
- `docs/integration/pi-zero-w/CROSS_COMPILE_HANDOFF.md`
- `docs/integration/pi-zero-w/NATIVE_BUILD.md` for the slow native fallback

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

### 3. Disable cloud-init after first boot

Run this only after SSH, Wi-Fi, hostname, and the `pi` account are confirmed.

```bash
sudo touch /etc/cloud/cloud-init.disabled
sudo systemctl disable --now \
  cloud-init-main.service \
  cloud-init-local.service \
  cloud-init-network.service \
  cloud-config.service \
  cloud-final.service \
  cloud-init.target \
  cloud-init-hotplugd.socket
```

### 4. Create the runtime directories

Run on the Pi:

```bash
mkdir -p /home/pi/artemis/releases /home/pi/artemis/current /home/pi/artemis/logs
```

### 5. Copy the frozen release artifacts

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

### 6. Install and enable the systemd unit

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
- Cloud-init disabled after first boot and confirmed SSH access.
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

## Pre-commit review, October 26

Installer review fixed release overwrite on repeat installs, ineffective Bash
negated preflight checks, late rollback-helper generation, and ownership of
new runtime directories. Desktop cleanup now snapshots every selected mask and
writes its rollback helper before applying masks. These revisions were reviewed
and checked locally; they were not reapplied to either Pi.

Seven supervisor lifecycle tests passed again in a Linux Python 3.11 container.
An isolated container with mocked platform/service commands verified rejection
of an existing release, active NFS mounts and unresolved libraries; an injected
unit-verification failure left an executable rollback helper, account-owned
runtime directories, and successfully restored the old supervisor/release link.
Shell syntax checks passed. These checks do not replace target installer or
power-loss qualification.
