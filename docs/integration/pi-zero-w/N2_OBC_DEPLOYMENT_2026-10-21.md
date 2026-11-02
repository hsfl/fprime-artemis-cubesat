# n2-obc CDR deployment — October 21, 2026

Target: Raspberry Pi Zero W Rev 1.1, ARMv6, Raspberry Pi OS 13 (Trixie),
user `n2-obc`, hostname `n2-obc.local`, deployment-time IPv4 `192.168.0.152`.

## Build and installed files

Source: clean `neutron2-develop` at `a468d2899bce32157c7eb4f672cf22bdefe05278`;
F′ `a750219414881d3461054e73c25a0868a5e648f9`.
Docker image: `artemis-pi-zero-w-cross:local`.
The build explicitly selected `NEUTRON2_CDR_TCP=ON`,
`NEUTRON2_LOCAL_YAMCS=OFF`, and `CMAKE_BUILD_TYPE=Release` (`-O3 -DNDEBUG`).
The generic cross-build wrapper does not select CDR automatically.

The existing Pi ARMv6 Bookworm sysroot was retained. The resulting executable
has ARMv6KZ/VFPv2 hard-float attributes and `/lib/ld-linux-armhf.so.3` interpreter;
all shared libraries resolved against the target Trixie userspace.
The unstripped executable remains in local build artifacts. The deployed copy
was stripped from approximately 2.3 MiB to 1.5 MiB.

Release: `/home/n2-obc/artemis/releases/a468d28-cdr-tcp/`, selected through
`/home/n2-obc/artemis/current`.

| Artifact | SHA-256 |
| --- | --- |
| Deployed executable | `ae1c13affea35c2f26e1787c358ace132c91cae3940ee6a5f2b87ba016865e44` |
| Matching topology dictionary | `033fcd67f6a79f3b34643add4078494292123cb9c36df9ea092f6a0f1855af82` |

## Runtime configuration

`artemis-fprime.service` uses systemd, runs as `n2-obc` with `dialout`, and starts:

```sh
/home/n2-obc/artemis/current/ArtemisRpiTeensyDeployment -a 0.0.0.0 -p 50000 -d /dev/serial0
```

Working directory: `/home/n2-obc/artemis/state`.
`N2_STORAGE_ROOT=/home/n2-obc/artemis/data/science-products` overrides the
source's `/home/pi` default. The service checks that the UART exists, restarts
on failure after two seconds, and has a ten-second stop timeout. It does not
wait for Wi-Fi or `network-online.target`; wildcard TCP binding allows the
ground client to connect after networking becomes available.

Headless boot uses `multi-user.target`. Desktop/VNC, printing, Bluetooth,
NetworkManager wait-online, and UART login services are disabled. Completed
cloud-init provisioning is disabled through `/etc/cloud/cloud-init.disabled`.
Wi-Fi, NetworkManager, SSH, and Avahi remain available. Boot configuration
enables UART and `disable-bt` to put PL011 on GPIO14/15; the serial kernel
console and graphical splash are removed. No overclock is configured.

Pre-change files, enabled-unit list, default target, and boot measurements are
saved on the Pi under `/home/n2-obc/artemis/setup-backup-*`. The exact installer,
build log, and identity manifest are retained locally under ignored
`yamcs/run/n2-obc-deploy-20261021/`; the installer and manifest are also on the Pi.

## Verification and limits

Initial TCP-only runtime reached rate groups without an assertion. A bounded
TERM test without a client required forced termination: the pinned stock TCP
driver can remain blocked in `accept()` during teardown. The service's stop
timeout bounds this; graceful shutdown is not established by that test.

After an actual reboot, `/dev/serial0 -> ttyAMA0` appeared and the service
autostarted with zero restarts. F′ launched at 59.349 seconds from kernel start
and systemd marked it active at 60.736 seconds. The process used about 5.7 MiB
RSS and 1.3% average CPU in this short idle/network-command sample.
No systemd units were failed; the science-storage identity files were created
under the configured account-specific path without a storage warning.

Measured total boot changed from 184.914 seconds to 152.445 seconds (about
32.5 seconds, or 18%, faster). This is one before/after observation; the first
boot also included provisioning work, so it is not a controlled benchmark.
NetworkManager still took 88.906 seconds and dominated time to a usable
network; F′ started independently before networking finished. Further network
and OS-image tuning remains possible.

The local Mac's ground-only Yamcs client connected to the Pi's actual TCP
server using the matching ARM dictionary:

- `missionApp.PING(26102142)` returned a Yamcs-decoded Pong with the same token.
- Stock `FileHandling.fileDownlink.SendFile` returned the 729-byte test fixture.
  An independent Yamcs object GET matched every byte, CRC16 `52221`, and SHA-256
  `3fa9a43df9aa2bcbe3ced89cb99695e2c77ce73e7bf916c1e4d35188fb3fca72`.
- JSON command/receipt records are in the ignored deployment evidence folder.
  The temporary Mac ground stack was stopped; the Pi F′ service remains running.

The UART opened, but PDU status requests timed out and EPS health remained
UNKNOWN. This verifies Pi software startup and LAN command/file transport,
not an operational Teensy/PDU connection. The simulated payload driver is
unchanged. No payload or power-control commands were issued.

## Operator commands

```sh
ssh n2-obc@n2-obc.local
systemctl status artemis-fprime.service
journalctl -u artemis-fprime.service -b --no-pager -n 40
sudo systemctl restart artemis-fprime.service
```

The ground client connects to `n2-obc.local:50000`. Use its deployed matching
JSON dictionary; the TCP server supports one ground connection at a time.

Rollback: stop/disable `artemis-fprime.service`, restore `config.txt` and
`cmdline.txt` from the dated setup backup into `/boot/firmware/`, restore the
recorded default target and enabled units, remove `/etc/cloud/cloud-init.disabled`
if it was absent before setup, and reboot. Retain release/data directories.

This deploys the current CDR software profile. It still contains the simulated
payload driver and does not establish the real payload interface. Periodic
read-only PDU status requests occur on UART channel 2; no payload capture,
power-switching, flashing, or RF test is part of the smoke test.
