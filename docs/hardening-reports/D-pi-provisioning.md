# Worker D - Pi Provisioning

## Created

- `deploy/pi/artemis-fprime.service`
  - systemd unit for the Raspberry Pi flight deployment
  - runs `/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0`
  - uses `User=pi`, `WorkingDirectory=/home/pi/artemis/current`
  - has `Restart=always`, `RestartSec=5`, journald stdout/stderr, and `WantedBy=multi-user.target`
- `deploy/pi/README.md`
  - blank-SD provisioning checklist
  - macOS instructions first, Windows/WSL2 notes second
  - release-artifact install flow for `v1.0.0-mvp-demo`
  - explicit migration section from `ln.service` to `artemis-fprime.service`
  - spare-SD-card bench mitigation checklist

No existing docs were modified. No Pi/hardware commands were run.

## Source Facts Used

- `docs/HARDWARE_PORT_MAP_AND_POWER.md`
  - Pi talks to the satellite Teensy on `/dev/serial0`
  - current docs say the service is `artemis-fprime.service`
  - service runs `ArtemisRpiTeensyDeployment -d /dev/serial0`
- `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`
  - HIL service checks use `systemctl is-active artemis-fprime.service`
  - log checks use `journalctl -u artemis-fprime.service`
- `docs/agents_notes.md`
  - verified Pi account/host: `pi@192.168.0.152`, alias `artemis-pi`
  - Pi architecture: `armv6l`
  - deployed binary/dictionary symlink layout:
    - `/home/pi/artemis/current/ArtemisRpiTeensyDeployment`
    - `/home/pi/artemis/current/ArtemisRpiTeensyDeploymentTopologyDictionary.json`
  - runtime command: `/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0`
  - older duplicate service `artemis-cross.service` was disabled, leaving one startup path
- `docs/RPI_BUILD.md`
  - UART setup commands:
    - `sudo raspi-config nonint do_serial_hw 0`
    - `sudo raspi-config nonint do_serial_cons 1`
    - `sudo systemctl disable --now serial-getty@serial0.service || true`
  - native-build fallback can link artifacts into `/home/pi/artemis/current`
  - direct runtime command is `/home/pi/artemis/current/ArtemisRpiTeensyDeployment -d /dev/serial0`
- `docs/RPI_SETUP.md`
  - target is Raspberry Pi Zero W with Raspberry Pi OS Lite 32-bit
  - enable UART and disable serial login shell
  - quick runtime shape from repo checkout:
    - `cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2`
    - `./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0`
- `README.md`
  - release tag/label: `v1.0.0-mvp-demo`
  - frozen Pi binary path inside build artifacts:
    - `ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment`
  - frozen dictionary path:
    - `ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json`
  - release includes `SHA256SUMS`
- `docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md` and `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`
  - Pi Zero W requires ARMv6-compatible artifacts
  - bad ARMv7 output can fail with `Illegal instruction`
  - cross-build flow verifies `readelf` attributes before use
- `docs/DEMO_RELIABILITY_GAP_AUDIT_2026-07-06.md` and `docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md`
  - sprint task says the live service is currently named `ln`
  - release asset name is `neutron2-v1.0.0-mvp-demo-artifacts.zip`

## Contradictions / Reconciliation

- Service name:
  - Sprint/audit say the live Pi service is named `ln`.
  - Current operational docs searched in this task (`HARDWARE_PORT_MAP_AND_POWER.md`, `SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`, `agents_notes.md`) now refer to `artemis-fprime.service`.
  - I treated `ln.service` as the bench migration source because the sprint explicitly assigns that as the current live risk, but versioned the new unit as `artemis-fprime.service`.
- Pi runtime path:
  - `CLAUDE.md` runtime smoke flow and `docs/RPI_SETUP.md` show a repo-checkout binary:
    - `~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment`
  - `docs/agents_notes.md` and `docs/RPI_BUILD.md` show the deployed/symlinked runtime path:
    - `/home/pi/artemis/current/ArtemisRpiTeensyDeployment`
  - I used `/home/pi/artemis/current` in the unit because it is the deployed-release layout and keeps native/cross/release artifact installs behind stable symlinks.
- Release naming:
  - `README.md` says GitHub release `v1.0.0` and internal label `v1.0.0-mvp-demo`.
  - Sprint/audit say tag/release asset path uses `v1.0.0-mvp-demo`.
  - README provisioning uses `gh release download v1.0.0-mvp-demo` because the sprint explicitly names that as the frozen tag used by workers.
- Dictionary:
  - The service itself does not pass the dictionary; the dictionary is for GDS on the ground side.
  - The Pi layout still keeps the matching dictionary beside the binary because docs and release artifacts treat the pair as one deployable unit.

## Open Questions For Next Bench Session

1. On the actual bench Pi, confirm which service exists now:
   - `systemctl status ln.service --no-pager -l || true`
   - `systemctl status artemis-fprime.service --no-pager -l || true`
2. Confirm the active process command:
   - `pgrep -af ArtemisRpiTeensyDeployment`
3. Confirm the symlink targets:
   - `readlink -f /home/pi/artemis/current/ArtemisRpiTeensyDeployment`
   - `readlink -f /home/pi/artemis/current/ArtemisRpiTeensyDeploymentTopologyDictionary.json`
4. Confirm whether the bench Pi still uses username `pi` or has moved to a custom account.
5. Confirm the GitHub Release tag name accepted by `gh release download` in the team repo: `v1.0.0-mvp-demo` vs `v1.0.0`.
6. After migration, verify one and only one service owns the deployment process.

## Verification Performed

- Read the required sprint and repository guidance docs.
- Read and reconciled the requested Pi/run-shape docs.
- Created only the two requested files under `deploy/pi/`.
- Did not run builds, systemd commands, SSH, or hardware-touching scripts.
