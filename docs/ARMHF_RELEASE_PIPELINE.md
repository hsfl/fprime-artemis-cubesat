# ARMHF Release Pipeline (Manual Deploy)

Use this document for cross-build + package context only.  
The automated deploy scripts were removed in favor of manual Wi-Fi/SSH/SCP steps for classroom use.

## Build + Package

```bash
cd ArtemisRpiTeensy_N2
./tools/cross_build_armhf.sh --pull
./tools/package_armhf_release.sh
```

Release output:
- `ArtemisRpiTeensy_N2/releases/release-<timestamp>-<gitsha>-armhf/`

## Deploy to Raspberry Pi

Follow:
- `rpi_build.instructions`

That file covers:
- finding Pi IP on local Wi-Fi
- SSH with default `pi` account
- SCP transfer from laptop to Pi
- symlink activation (`active_release`, `current/`)
- checksum verification
- runtime smoke checks
