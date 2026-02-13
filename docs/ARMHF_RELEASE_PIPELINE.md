# ARMHF Release Pipeline (Golden Pattern)

This runbook implements the release-grade flow for deploying `ArtemisRpiTeensyDeployment` to a 32-bit Raspberry Pi OS target:

1. Cross-compile on Mac using Docker (`arm-hf-linux`)
2. Package only deploy artifacts (binary + dictionary + checksums + provenance)
3. Deploy release bundle over SSH
4. Activate atomically on Pi with rollback pointer
5. Run smoke test for 30 seconds against `/dev/serial0`

## Prerequisites

- Docker (or Rancher Desktop) running on Mac
- SSH access to Raspberry Pi
- Existing F' venv in:
  - `ArtemisRpiTeensy_N2/fprime-venv`
- Active deployment name:
  - `ArtemisRpiTeensyDeployment`

Pull the cross-toolchain image once:

```bash
docker pull nasafprime/fprime-arm:latest
```

## One-command pipeline

From repo root:

```bash
cd ArtemisRpiTeensy_N2
./tools/release_armhf.sh \
  --pi-user <pi-user> \
  --pi-host <pi-host-or-ip>
```

Defaults:
- `--remote-base /home/<pi-user>/artemis`
- `--uart-device /dev/serial0`
- `--duration 30` (smoke pass requires app alive for full duration)

## Step-by-step commands

### 1) Cross-compile in Docker

```bash
cd ArtemisRpiTeensy_N2
./tools/cross_build_armhf.sh --pull
```

Optional:

```bash
./tools/cross_build_armhf.sh --jobs 8
```

### 2) Package release artifacts

```bash
cd ArtemisRpiTeensy_N2
./tools/package_armhf_release.sh
```

Outputs under:
- `ArtemisRpiTeensy_N2/releases/release-<timestamp>-<gitsha>-armhf/`

Release contents:
- `ArtemisRpiTeensyDeployment`
- `ArtemisRpiTeensyDeploymentTopologyDictionary.json`
- `SHA256SUMS`
- `RELEASE_INFO.txt`

### 3) Deploy and activate on Pi

```bash
cd ArtemisRpiTeensy_N2
./tools/deploy_armhf_release.sh \
  --release-dir ./releases/<release-name> \
  --pi-user <pi-user> \
  --pi-host <pi-host-or-ip>
```

Remote layout:
- `/home/<pi-user>/artemis/releases/<release-name>`
- `/home/<pi-user>/artemis/active_release` (symlink)
- `/home/<pi-user>/artemis/previous_release` (symlink)
- `/home/<pi-user>/artemis/current/ArtemisRpiTeensyDeployment` (symlink)
- `/home/<pi-user>/artemis/current/ArtemisRpiTeensyDeploymentTopologyDictionary.json` (symlink)

### 4) Smoke test on target

```bash
cd ArtemisRpiTeensy_N2
./tools/smoke_test_pi_release.sh \
  --pi-user <pi-user> \
  --pi-host <pi-host-or-ip> \
  --uart-device /dev/serial0 \
  --duration 30
```

Pass condition:
- process remains alive for full duration (timeout exit code `124`).

### 5) Rollback

```bash
cd ArtemisRpiTeensy_N2
./tools/rollback_armhf_release.sh \
  --pi-user <pi-user> \
  --pi-host <pi-host-or-ip>
```

Rollback flips active release back to `previous_release` and rewires `current/` symlinks.

## Optional modes

Use prebuilt release without rebuild:

```bash
./tools/release_armhf.sh \
  --release-dir /abs/path/to/release-dir \
  --pi-user <pi-user> \
  --pi-host <pi-host-or-ip> \
  --skip-build
```

Build/package only (no Pi deploy):

```bash
./tools/release_armhf.sh --skip-deploy --skip-smoke
```

## Notes

- This pipeline intentionally deploys artifacts only, never the full repo.
- Dictionary and binary are version-locked within each release bundle.
- Native build on Pi remains debug fallback only, not release source of truth.

