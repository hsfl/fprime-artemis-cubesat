# Repo Context and Notes — Pi Zero W Cross-Compilation

## Repo layout
- Cross-compile assets are shared at the repo root under `tools/cross/`:
  - driver script: `tools/cross/cross_compile.sh` (run from the repo root)
  - sysroot sync helper: `tools/cross/sync_sysroot.sh`
  - CMake toolchain: `tools/cross/cmake/toolchain/pi-zero-w-armv6hf.cmake`
  - container: `tools/cross/docker/Dockerfile`
  - local-only, gitignored: `tools/cross/sysroot/`, `tools/cross/verify/`
- Cross Python envs: `tools/cross/venv/<first-12-of-sha256(requirements.txt)>`,
  shared across projects with matching pins. The repo needs two today: the RPi
  project pins fprime-tools 4.2.1/fpp 3.2.0, core pins 4.3.0/3.3.0.
- Registered with each project through `library_locations` in its `settings.ini`
  (colon-separated, and `../tools/cross` climbing above the project root is fine).
- Default build target: `ArtemisRpiTeensyDeployment` in `ArtemisRpiTeensy_N2/`;
  override with `--project` / `--deployment`.
- The toolchain no longer finds the F Prime ARM helper by relative path. The
  driver passes `-DARTEMIS_ARM_HELPERS_DIR=<project>/lib/fprime/cmake/toolchain/helpers`,
  and the toolchain file adds it to `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES` so it
  survives into try_compile sub-projects.
- Full handoff/known-issues doc: `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`
- Student-facing guide: `docs/CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`

## Target hardware
- Raspberry Pi Zero W — BCM2835, **ARMv6** with VFPv2, armhf userland.
- Standard `aarch64-linux` and `arm-hf-linux` F' toolchains target ARMv7+;
  their binaries SIGILL on this board. Use the ARMv6-compatible toolchain
  documented in `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md`.

## Build outputs
- ARM binary + dictionary land under:
  `<project>/build-artifacts/pi-zero-w-armv6hf/<deployment>/`
- Verification evidence is kept per target so `--copy-only` can never ship the
  wrong binary: `tools/cross/verify/<project>/<deployment>/`
- Verify architecture before shipping to the Pi:
  `readelf -A <binary> | grep -i arch` (expect ARMv6, VFPv2).

## Prior gotchas
- Docker/Rancher Desktop must be running before the script is invoked.
- `--clean` rebuilds the Docker image, cross venv, and the selected project's
  build cache. It no longer re-syncs the sysroot: that is shared repo-wide now,
  so wiping it from one project's --clean would break every other target.
  `--resync-sysroot` is the explicit opt-in.
- The slow part of a clean run is Python dependency setup, not compilation.
- A sysroot copied by hand from another machine (rather than produced by
  `sync_sysroot.sh`) still contains the Pi's own ARM `cc1`/`cc1plus`/`collect2`.
  They sit in a `-B` search path, gcc searches `-B` for programs as well as
  libraries, so the cross compiler runs the Pi's `cc1` under qemu and dies with
  `qemu-arm: Could not open '/lib/ld-linux-armhf.so.3'`. `cross_compile.sh` now
  detects this before building and says how to fix it.
- `fprime-artemis-core` builds per target via `ARTEMIS_TARGET_ZEPHYR`. Deployments
  are registered in that branch, so a non-Zephyr toolchain builds only
  `PayloadComputerDeployment`. Its TlmPacketizer caps and CdhCore tlm config are
  overridden in `PayloadComputerDeployment/PayloadComputerConfig/` rather than in
  the shared project config, which stays sized for the Teensy.
