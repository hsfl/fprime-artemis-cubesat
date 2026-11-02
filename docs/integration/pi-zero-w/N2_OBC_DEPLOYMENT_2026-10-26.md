# n2-obc deployment and recovery verification — October 26, 2026

Pi Zero W only. Dennis authorized deployment of the latest `neutron2-develop`
application, service recovery improvements, boot tuning, and reboot verification.
The Pi 5 ground station was explicitly excluded from this initial update; it was subsequently updated and tested as recorded below.

## Installed identity

Source and live upstream `neutron2-develop`:
`d4fe283e723b6abb10391471b803aa6c7816a608`.
F′ remains pinned to `a750219414881d3461054e73c25a0868a5e648f9`.
The application source was clean; unrelated Teensy/ground working files were
preserved. Service tooling changes are separate from the application build.

Build: ARMv6KZ/VFPv2 hard-float Release, `NEUTRON2_CDR_TCP=ON`,
`NEUTRON2_LOCAL_YAMCS=OFF`, using the retained Pi Zero W cross-toolchain/sysroot.
ELF interpreter `/lib/ld-linux-armhf.so.3`; all target shared libraries resolved.
The unstripped executable was retained locally.

Selected release:
`/home/n2-obc/artemis/releases/d4fe283-cdr-tcp-20261026`, through
`/home/n2-obc/artemis/current`.

| Artifact | SHA-256 |
| --- | --- |
| Deployed executable | `7cf58c3ef583dcf67dda38f4481dbcc8eda6687c66f322ebc54d6d30827752c2` |
| Matching onboard dictionary | `07ae4fbb72a37358d3827d29fd438d675d475b92c9fd91b367f8e20a4b16eafe` |

Both artifacts came from the same build and were checked on the target. The
Pi 5 dictionary was intentionally left alone during this initial deployment. It was subsequently paired and tested as described below.

## Service and recovery

Maintained files are now in [`deploy/pi/`](../../../deploy/pi/README.md):
`artemis-fprime.service`, `artemis-supervisor.py`, `test_supervisor.py`, and
`install-n2-release.sh`.

The service runs as `n2-obc`, with `dialout`, `NoNewPrivileges`, persistent state
in `/home/n2-obc/artemis/state`, and science storage in
`/home/n2-obc/artemis/data/science-products`. It remains independent of Wi-Fi.
The application uses `/dev/serial0 -> ttyAMA0` and TCP `0.0.0.0:50000`.

- UART absence waits inside the supervisor rather than repeatedly failing the
  service and exhausting start limiting. Startup may wait indefinitely for UART.
- `Type=notify` readiness requires fresh `epsManager.EpsStatusUpdated` text
  progress and a TCP listener whose socket inode belongs to the F′ child.
- After child launch, startup grace is 90 seconds. After readiness, a 30-second
  EPS progress gap or any child exit causes cleanup and failure/restart.
- Cleanup sends TERM to the child process group, escalating to KILL after five
  seconds. Systemd also bounds stopping the service to ten seconds.
- A 45-second systemd watchdog monitors the supervisor after readiness. The
  separate 60-second OS hardware watchdog remains configured.

This is partial application liveness monitoring. It depends on periodic EPS
ACTIVITY_LO text logging and does not prove valid PDU responses, payload health,
all F′ task progress, or end-to-end TCP command handling. The TCP listener is
checked for startup readiness; post-ready progress monitoring uses EPS events.

Seven Linux lifecycle tests passed, including delayed UART, unexpected child
exit, startup/progress expiry, child cleanup, and rejecting a TCP listener owned
by another process. Tests run in a Linux container because they inspect `/proc`.

An actual target test sent SIGSTOP only to the F′ child. The supervisor detected
the missing progress, removed the frozen child, and systemd started a new
supervisor/application. Readiness returned in **102.2 seconds**, with one restart
and no old child remaining. Part of that delay was a TCP bind failure while the
old connection/port retired; readiness stayed false until the new listener was
owned by F′. This verifies recovery, not fast TCP rebind or graceful teardown.

## Boot changes

- Supported `rpi-swap` drop-in selects `Mechanism=zram`, retaining compressed
  swap while removing swap-file/writeback preparation from early boot.
- Masked unused RPC/NFS units and Pi-inapplicable EEPROM/display test helpers.
- Masked per-user PipeWire, WirePlumber, filter-chain and Bluetooth audio proxy
  units/sockets. These user services also started at login; their removal alone
  is not evidence of an early-boot saving.
- Disabled unused audio and camera/DSI autodetection. HDMI video/KMS and local
  console were retained for recovery.
- Persistent journals are capped at 32 MiB, with an 8 MiB runtime cap, so later
  boot/recovery investigations can read previous-boot evidence.

Wi-Fi profiles, SSH, Avahi, NetworkManager and its Netplan integration were
retained. Cloud-init remains skipped after provisioning; network wait-online
remains disabled for this headless bench. No package upgrade, overclock, GPIO,
Teensy/PDU flashing, payload capture, power switching, or RF operation occurred.

## Target verification

| Timing | Before | First updated boot | Final updated boot |
| --- | ---: | ---: | ---: |
| Normal system target | 103.869 s | 82.849 s | 82.855 s |
| F′ service active | 65.052 s (process execution) | 50.094 s (EPS/TCP readiness) | 50.133 s (EPS/TCP readiness) |
| Wi-Fi activation | 107.408 s | 85.314 s | 85.776 s |

The normal system target improved by 21.020 seconds, about 20%. The old/new F′
active timestamps use different readiness definitions; the new check is stronger.
Timings overlap and must not be added. NetworkManager/Netplan startup remains a
substantial part of the remaining boot time. These are reboot observations,
not a cold power-on benchmark or a guaranteed upper bound.

The final reboot passed: F′ active with zero restarts, an owned TCP 50000
listener, Wi-Fi `192.168.0.152`, key-only SSH, matching running-executable and
dictionary hashes, no failed systemd units, and `throttled=0x0`. The installed
supervisor/unit hashes match the final local sources. Zram has no backing file,
and both updated boots remain readable in the persistent journal.

EPS remains UNKNOWN with PDU status timeouts. Open UART, active service, periodic
events and a TCP listener do not establish a working Teensy/PDU/payload chain.
No ground-command or file-transfer acceptance was run during this initial update; subsequent synthetic two-Pi checks are recorded below.

## Evidence and rollback

Local ignored evidence:
`yamcs/run/n2-obc-deploy-20261026/` contains the paired artifacts, unstripped
binary, build/service manifests, compiler/ELF checks, reboot details, and
`hang-recovery.json`. The test fixture/smoke helper was prepared but no ground
smoke was run during the initial Pi 5 exclusion. Subsequent checks are recorded below.

Original unit/configuration and release selection were preserved at:
`/home/n2-obc/artemis/setup-backup-20261026T221458Z-n2-update`.

On the Pi, rollback is:

```sh
sudo bash /home/n2-obc/artemis/setup-backup-20261026T221458Z-n2-update/rollback.sh
sudo reboot
```

It restores the previous application selection and changed configuration files
and masks. Persistent science/state and retained releases are not removed.
The reboot is required to restore swap/boot/user-service configuration.

## Desktop-image cleanup follow-up

Dennis identified this as the desktop OS image and requested unused services
be disabled. `deploy/pi/disable-desktop-services.sh` masks installed GUI/VNC,
printing, Bluetooth, desktop disk/power/package helpers, keyrings, portals and
GVFS services. Packages and the HDMI text console are retained. The system
network, SSH, Avahi, logging, maintenance and F′ services are retained.

After another reboot, F′ reached readiness at 50.028 seconds with zero restarts;
the normal target took 82.975 seconds. Wi-Fi/SSH worked, no system units failed,
no user service was running in the sampled session, and throttling flags were
zero. The memory sample showed 130 MiB used and 296 MiB available. This does not
establish an additional RAM/boot saving: most desktop services were already idle.
Evidence is `desktop-cleanup-reboot.txt` and `desktop-cleanup-final.txt` in the
same local evidence pack. The Pi 5 was not accessed or changed during this cleanup.

To undo only this additional cleanup, run the following on the Zero W, then
reboot:

```sh
sudo bash /home/n2-obc/artemis/desktop-backup-20261026T223114Z/rollback.sh
sudo reboot
```

For a full return to the pre-update configuration, undo this cleanup first,
then use the earlier release/configuration rollback above.

## Subsequent two-Pi synthetic checks

After the initial Pi 5 exclusion, the ground dictionary was updated to the paired build above. Three matching Pongs were observed before and three after the update; the ground services recovered after their account-owned main processes received SIGTERM, with one expected restart each and zero Zero W service restarts. A GUI stock FileDownlink fixture produced a 2,086-byte received object; an earlier API fixture had independently matched its source SHA.

The repository simulator and CSV were installed at `/home/n2-obc/artemis/external/payload-neutron-simulation`, the previously missing default relative runtime path. No binary or service change was needed. A GUI 30-second science command completed 30 synthetic rows and retained a 329-byte product with CRC16 11852. The simulator duration does not establish real wall-clock capture. The GUI sent the product using stock FileDownlink; independent Yamcs GET size and SHA-256 matched the source (`668a75c3155e210974051da962e20b2f4238acfa090d8a8608a8f25f9dcc9821`). The Mac viewer displayed the received 30 rows.

See the [ground record](../N2_GROUND_DEPLOYMENT_2026-10-21.md#october-26-two-pi-update-and-synthetic-capture) for commands, viewer values and evidence paths. Dennis reported the embedded OBC connection absent. These checks used no Teensy/PDU, real-payload or RF data path and establish no acceptance of that hardware.
