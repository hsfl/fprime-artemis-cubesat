# Cross-Compile Handoff (Pi Zero W, Validated)

Date: 2026-03-06 (HST)

## Scope

This note is for future agents working on Docker-based F' cross-compilation for the Neutron 2 Raspberry Pi target:

- Hardware: Raspberry Pi Zero W Rev 1.1
- CPU: `armv6l`
- Userspace: Raspberry Pi OS Lite 32-bit (`armhf`)

This flow is now validated for:

- local Docker cross-build
- ARMv6 binary verification
- deploy to the Pi
- smoke test with `-d /dev/null`

It is not a replacement for real UART validation on `/dev/serial0`.

## Source Docs

Use the F' v4 docs as the external baseline:

- [Cross-Compilation tutorial](https://fprime.jpl.nasa.gov/latest/docs/tutorials/cross-compilation/)
- [Raspberry Pi toolchain reference](https://fprime.jpl.nasa.gov/latest/docs/reference/api/cmake/toolchain/raspberrypi/)

Do not assume the stock `arm-hf-linux` or Debian cross packages are Pi Zero W safe just because they are 32-bit ARM hard-float.

## What Was Failing Before

The first Docker cross-builds produced a binary that linked and deployed but had ARMv7 attributes:

- `Tag_CPU_arch: v7`
- `Tag_FP_arch: VFPv3-D16`

Those builds were invalid for Pi Zero W and matched the earlier `Illegal instruction` failures.

The root cause was not the project source. The root cause was the host Debian cross-toolchain runtime objects:

- host `crtbeginS.o`
- host `crtendS.o`
- host `libgcc.a`
- host `libstdc++.so`

Those host runtime objects were ARMv7, even though the compile flags requested `arm1176jzf-s`.

## What Actually Fixed It

The validated solution is:

1. Build in Docker on macOS/Linux host.
2. Sync a sysroot from the actual Pi.
3. Sync the Pi's GCC runtime directory into that sysroot.
4. Force the linker to use the Pi-sourced runtime/startup objects instead of the Debian ARMv7 ones.

That logic now lives in:

- `ArtemisRpiTeensy_N2/cross/pi-zero-w/cmake/toolchain/pi-zero-w-armv6hf.cmake`
- `ArtemisRpiTeensy_N2/tools/sync_pi_zero_w_sysroot.sh`
- `ArtemisRpiTeensy_N2/tools/docker_cross_compile_pi_zero_w.sh`

## Repo Files Added or Changed

Cross-build registration:

- `ArtemisRpiTeensy_N2/settings.ini`
- `ArtemisRpiTeensy_N2/cross/pi-zero-w/library.cmake`

Toolchain and Docker:

- `ArtemisRpiTeensy_N2/cross/pi-zero-w/cmake/toolchain/pi-zero-w-armv6hf.cmake`
- `ArtemisRpiTeensy_N2/cross/pi-zero-w/docker/Dockerfile`

Automation scripts:

- `ArtemisRpiTeensy_N2/tools/sync_pi_zero_w_sysroot.sh`
- `ArtemisRpiTeensy_N2/tools/docker_cross_compile_pi_zero_w.sh`

Smoke-test behavior:

- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/ArtemisRpiTeensyDeploymentTopology.cpp`

## Important Runtime Detail: `/dev/null`

The original topology assumed that any non-null device path was a real UART and asserted if `Drv::LinuxUartDriver::open()` failed.

That breaks the documented smoke path:

```bash
./ArtemisRpiTeensyDeployment -d /dev/null
```

because `/dev/null` is not a termios UART device.

The topology now special-cases `/dev/null` as a smoke-test mode:

- skip Linux UART open
- skip Linux UART receive thread start
- skip Linux UART thread teardown

This allows the deployment to come up far enough for a no-UART smoke test without pretending `/dev/null` is real serial hardware.

## Normal Flow

macOS:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
export PI_ZERO_W_SSH_HOST=pi@artemis-pi.local
export PI_ZERO_W_REMOTE_DIR=/home/pi/artemis/cross
./tools/docker_cross_compile_pi_zero_w.sh
```

Windows WSL2:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
export PI_ZERO_W_SSH_HOST=pi@artemis-pi.local
export PI_ZERO_W_REMOTE_DIR=/home/pi/artemis/cross
./tools/docker_cross_compile_pi_zero_w.sh
```

The default flow is now the fast iterative path:

- reuse the Docker image if it already exists
- reuse the synced Pi Zero W sysroot if it is complete
- reuse `.cross-venv-linux` if the F Prime tools still run
- reuse the F Prime build cache unless `--clean` is used
- build and verify the ARMv6 binary every time
- deploy to the Pi and run the `/dev/null` smoke test unless `--local-only` is used

This script is meant to be reused on another workstation. Do not assume the
SSH host alias or remote directory from the original setup will exist.

Students or future agents should change the target using either environment
variables:

```bash
export PI_ZERO_W_SSH_HOST=pi@artemis-pi.local
export PI_ZERO_W_REMOTE_DIR=/home/pi/artemis/cross
export PI_ZERO_W_SYSROOT_DIR="$PWD/cross/pi-zero-w/sysroot"
```

or explicit flags:

```bash
./tools/docker_cross_compile_pi_zero_w.sh --host pi@artemis-pi.local --remote-dir /home/pi/artemis/cross
```

The Pi-side requirements are:

- reachable over SSH without interactive prompts during automation
- `rsync` installed
- permission for `sudo rsync` so the sysroot copy can include system libraries

What the script does:

1. syncs the sysroot from the Pi only when needed, or when `--clean` is used
2. ensures the loader symlink exists in the sysroot
3. builds inside Docker, reusing the F Prime build cache unless `--clean` is used
4. verifies the binary with `file` and `readelf`
5. copies the binary to the Pi unless `--local-only` is used
6. runs the remote smoke test with `/dev/null` unless `--local-only` is used

Useful flags:

```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh --local-only
./tools/docker_cross_compile_pi_zero_w.sh --clean
./tools/docker_cross_compile_pi_zero_w.sh --skip-sync
./tools/docker_cross_compile_pi_zero_w.sh --skip-image-build
./tools/docker_cross_compile_pi_zero_w.sh --host pi@artemis-pi.local
./tools/docker_cross_compile_pi_zero_w.sh --remote-dir /home/pi/artemis/cross
```

Use `--clean` for a deliberate full refresh. It refreshes the sysroot,
rebuilds the Docker image, recreates the cross Python venv, and force-regenerates
the F Prime build cache. For normal FPP/C++ iteration, avoid `--clean`; the slow
part is usually Python dependency download/install, and forced regeneration also
throws away incremental compile state.

## Verification Artifacts

Local verification outputs are written to:

- `ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/file.txt`
- `ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/readelf-A.txt`
- `ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/readelf-l.txt`
- `ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/binary-path.txt`

Expected ISA signals:

- `Tag_CPU_arch: v6` or `Tag_CPU_arch: v6KZ`
- `Tag_FP_arch: VFPv2`
- interpreter `/lib/ld-linux-armhf.so.3`

If you see ARMv7 again, stop and inspect the runtime objects being used by the linker before deploying.

These outputs, along with the synced sysroot and build trees, are local-only
artifacts and should stay ignored by Git.

## Validated Result

The validated binary signature from this flow was:

- ELF 32-bit ARM PIE
- hard-float loader `/lib/ld-linux-armhf.so.3`
- `Tag_CPU_arch: v6KZ`
- `Tag_FP_arch: VFPv2`

The remote `/dev/null` smoke test reached:

- command registration
- version events
- rate group start

and exited cleanly under timeout, which is sufficient for the current smoke definition.

## Non-Blocking Warning You Will Still See

The Pi may log a task-permissions warning like this:

- insufficient permissions to create a task with priority and/or cpu affinity

This comes from F' POSIX task startup and means the process fell back to default scheduler behavior.

It did not block the smoke test.

It only matters if you need strict runtime scheduling behavior under load.

## Remaining Work

Still not validated by this note:

- real UART run on `/dev/serial0`
- end-to-end Teensy bridge path
- GDS traffic over the real link

So the current project state is:

- Docker cross-build: validated
- ARMv6 output verification: validated
- Pi deploy: validated
- `/dev/null` smoke: validated
- `/dev/serial0` runtime: still pending

## Decision Rule

If a future change causes any ambiguity in ISA, ABI, or loader compatibility:

- treat it as a regression
- do not deploy blindly
- re-check the runtime objects used during linking

The mistake to avoid is assuming that compile flags alone are enough. For Pi Zero W, the linker inputs matter just as much as the compiler flags.
