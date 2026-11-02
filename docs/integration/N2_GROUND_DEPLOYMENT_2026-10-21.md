# N2 Pi 5 ground station — October 21, 2026

Dedicated ground host: `n2-fprime-yamcs`, wired IPv4 `192.168.0.203`,
Ethernet MAC `2C-CF-67-95-20-04`, SSH user `n2-fprime-yamcs`.
Observed OS: Raspberry Pi OS / Debian 13.7 aarch64, 4 GiB RAM, 29 GiB root filesystem.

## Installed layout

- Repository: `/home/n2-fprime-yamcs/fprime-artemis-cubesat`, branch `neutron2-develop`, source `a468d2899bce32157c7eb4f672cf22bdefe05278`.
- The clean Git clone was transferred through a Git bundle from the Mac after checking the GitHub branch SHA. Origin points to `https://github.com/hsfl/fprime-artemis-cubesat.git`; no GitHub credentials were copied. Ground operation needs no F′ framework/submodule build. Future pulls require separately configured GitHub access.
- Fresh Python 3.11.17 installed with uv, rather than changing the OS Python 3.13. Pinned ground dependencies installed with `tools/yamcs/bootstrap.sh`; `pip check` passes.
- Persistent station: `/home/n2-fprime-yamcs/yamcs-station/`: `config/`, `dictionary.json`, `data/`, `received-files/`, `logs/`.
- Ground-specific launcher and service templates: [`deploy/ground/`](../../deploy/ground/). These files are installed as a local overlay; they are not yet published to the shared branch.
- At the October 21 deployment, JSON dictionary SHA-256 was `033fcd67f6a79f3b34643add4078494292123cb9c36df9ea092f6a0f1855af82`, matching the [then-current OBC release](pi-zero-w/N2_OBC_DEPLOYMENT_2026-10-21.md). It was replaced on October 26 as recorded below. XTCE is regenerated from the dictionary at Yamcs startup.

## Access and operations

Open `http://192.168.0.203:8090` on the LAN, instance `fprime-project`, processor `realtime`.
`admin` is the full operator/administrator; `viewer` can read telemetry, packets,
events, alarms, command history, and received files, but has no command/write privileges.
Passwords are stored separately in the account-private `~/yamcs-station/Yamcs-access.txt`;
no passwords belong in this document or Git.

Authentication uses Yamcs YAML users/roles with hashed passwords. A separate
loopback-only identity gives the packaged event and telemetry helpers packet/MDB
read and event-write access. It has no commanding privilege. LAN HTTP remains
available without TLS or new firewall rules, as requested for this development board.
The packaged Yamcs UDP receive helpers also retain their LAN wildcard bindings;
account permissions enforce the HTTP/UI roles, not authentication of raw UDP input.

```sh
ssh n2-fprime-yamcs@192.168.0.203
systemctl status n2-yamcs n2-ground-bridge
journalctl -u n2-yamcs -u n2-ground-bridge -b --no-pager -n 50
sudo systemctl restart n2-yamcs n2-ground-bridge
```

`n2-yamcs.service` runs only Yamcs and its packaged helpers, with a 1 GiB JVM
heap. `n2-ground-bridge.service` runs the existing paced TCP/UDP adapter toward
`n2-obc.local:50000`; it restarts/retries when the OBC is unavailable.
Both services are enabled for boot startup and were verified after an actual reboot. Archives, signing key, user config,
and received files persist across service restarts. No spacecraft process runs
on this Pi 5.

## Verification

Live ground checks passed:

- Pinned dependencies install successfully; `pip check`, launcher syntax and systemd unit verification pass.
- Yamcs reports the `fprime-project` instance RUNNING and loads 193 parameters, 186 TM containers and 86 commands.
- Both LAN logins work. Admin is a superuser; viewer is not. Viewer reads events/MDB/processor state and receives HTTP 403 when attempting the actual PING command. Anonymous API access is rejected.
- Packaged event and telemetry helpers authenticate as the limited loopback identity and establish their WebSocket subscriptions.
- Following an actual reboot, both units are active with zero restarts, no failed systemd units, and the prior ground-setup event remains readable by viewer. Rotating server logs are present.
- Ignored local evidence: `yamcs/run/pi5-ground-deploy-20261021/`, including account-permission and archive readback JSON.

On October 21 the OBC was initially
unreachable at `n2-obc.local` and its last recorded `192.168.0.152`; no live
command/Pong, payload, PDU, RF, or end-to-end file-transfer acceptance is implied
by ground-server installation alone.

## October 26 two-Pi update and synthetic capture

The Pi 5 repository remained at `a468d2899bce32157c7eb4f672cf22bdefe05278`. Its station dictionary was replaced with the paired Zero W build dictionary, SHA-256 `07ae4fbb72a37358d3827d29fd438d675d475b92c9fd91b367f8e20a4b16eafe`; XTCE conversion passed. The prior dictionary is backed up at `/home/n2-fprime-yamcs/yamcs-station/dictionary-backup-20261026T223924Z`. Ground account-owned main processes received SIGTERM and their `Restart=always` units recovered, with one expected restart each; the Zero W had zero service restarts. Three PINGs produced matching Pongs before and three after the update; the earlier results were observed in the Zero W journal and Yamcs, and post-update results are recorded in JSON.

A GUI stock FileDownlink fixture produced a 2,086-byte received object. An earlier API fixture had an independent source SHA match; the GUI fixture was a separate received object. Next, the repository's `neutron_payload_sim.py` and `neutron_data.csv` were installed at the previously missing runtime-default relative location `/home/n2-obc/artemis/external/payload-neutron-simulation`, without changing the binary or service. GUI `scienceApp.SCIENCE_CAPTURE(durationSeconds=30)` completed with 30 synthetic rows; product 1 was 329 bytes with CRC16 11852. This simulator duration is not a real wall-clock capture.

GUI `FileHandling.fileDownlink.SendFile` sent `/home/n2-obc/artemis/data/science-products/product_1.bin` as `n2-gui-capture-product-1-20261026.bin`. An independent Yamcs object GET matched the separately measured source size and SHA-256 `668a75c3155e210974051da962e20b2f4238acfa090d8a8608a8f25f9dcc9821`. The existing Mac viewer at localhost:8062 displayed 30 received rows, total 423, mean 14.1, max 25, all 30 in the SAA.

Ignored local evidence under `yamcs/run/n2-obc-deploy-20261026/` includes `two-pi-ping-smoke-20261026T223852Z.json`, `pi5-dictionary-update-smoke-20261026.json`, `gui-downlink-proof.png`, `gui-capture-viewer-proof.png`, and the received product. The separate stock fixture evidence is on Pi 5 at `/home/n2-fprime-yamcs/yamcs-station/logs/sim-downlink-20261026T2241/stock-downlink-20261026-124041-d99a4d.json`. These are synthetic software and LAN checks. Dennis reported the embedded OBC connection absent. These tests used no Teensy/PDU, real-payload or RF data path and establish no acceptance of that hardware.

### Local pre-commit tooling checks

The station launcher's dictionary validation, logging configuration, XTCE
conversion and configured TM port lookup passed against a temporary copy of the
repository Yamcs configuration and the paired dictionary. Python/shell syntax
and whitespace checks passed. These checks used the pinned local Python 3.11
environment; no additional station restart or deployment was performed.
