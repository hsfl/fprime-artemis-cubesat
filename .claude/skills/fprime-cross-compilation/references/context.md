# Repo Context and Notes — Pi Zero W Cross-Compilation

## Repo layout
- Active F' project root: `ArtemisRpiTeensy_N2/`
- Deployment: `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/`
- Cross-compile driver script: `ArtemisRpiTeensy_N2/tools/docker_cross_compile_pi_zero_w.sh`
- Sysroot sync helper: `ArtemisRpiTeensy_N2/tools/sync_pi_zero_w_sysroot.sh`
- Full handoff/known-issues doc: `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`
- Student-facing guide: `docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`

## Target hardware
- Raspberry Pi Zero W — BCM2835, **ARMv6** with VFPv2, armhf userland.
- Standard `aarch64-linux` and `arm-hf-linux` F' toolchains target ARMv7+;
  their binaries SIGILL on this board. Use the ARMv6-compatible toolchain
  documented in `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`.

## Build outputs
- ARM binary + dictionary land under:
  `ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/`
- Verify architecture before shipping to the Pi:
  `readelf -A <binary> | grep -i arch` (expect ARMv6, VFPv2).

## Prior gotchas
- Docker/Rancher Desktop must be running before the script is invoked.
- `--clean` is slow (rebuilds sysroot, Docker image, cross venv, build cache);
  use it only when toolchain/sysroot/Python deps are suspect.
- The slow part of a clean run is Python dependency setup, not compilation.
