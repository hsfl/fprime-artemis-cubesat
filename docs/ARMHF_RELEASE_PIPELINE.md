# ARMHF Release Pipeline (Manual Deploy)

Use this document for cross-build + package context only.  
The automated deploy scripts were removed in favor of manual Wi-Fi/SSH/SCP steps for classroom use.

## Important Compatibility Note (Pi Zero W)

- Confirmed on 2026-02-20:
  - Pi Zero W target is `armv6l`
  - Current cross-built deployment reports `Tag_CPU_arch: v7`
  - On Pi Zero W this fails at runtime with `Illegal instruction`
- Therefore:
  - Do not treat current `arm-hf-linux` cross-build output as deployable to Pi Zero W
  - Use native build on the Pi for Pi Zero W runtime validation

## Build + Package

```bash
cd ArtemisRpiTeensy_N2
./tools/cross_build_armhf.sh --pull
./tools/package_armhf_release.sh
```

Release output:
- `ArtemisRpiTeensy_N2/releases/release-<timestamp>-<gitsha>-armhf/`

Optional validation (on target Pi):

```bash
readelf -A /home/pi/artemis/current/ArtemisRpiTeensyDeployment | egrep "Tag_CPU_arch|Tag_ABI_VFP_args"
```

If output shows `Tag_CPU_arch: v7` on Pi Zero W (`armv6l`), expect runtime incompatibility.

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

For Pi Zero W specifically:
- prefer native compile/run on the Pi over cross-built artifacts from this pipeline.
