---
name: fprime-cross-compilation
description: "Cross-compile F' (F Prime) deployments for ARM/Linux targets, especially on macOS using the F' ARM Docker container. Use for any F' cross-compilation tasks: installing the ARM toolchain, building a deployment for aarch64-linux or arm-hf-linux, or running the ARM GDS workflow (scp + fprime-gds --ip-client)."
---

# F' Cross-Compilation (Tutorial 3)

## This Repo — Pi Zero W

**Read first:** `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md` — covers the full handoff state, known issues, and what has/hasn't been validated on the actual hardware.

**Critical landmine — ARMv6, not ARMv7:** The Raspberry Pi Zero W uses an ARMv6 CPU (BCM2835). Do NOT use `aarch64-linux` or standard `arm-hf-linux` toolchains — they target ARMv7+ and will produce binaries that SIGILL on the Pi Zero W. You must use an ARMv6-compatible toolchain (e.g. `arm-linux-gnueabihf` from the Raspberry Pi Foundation toolchain, not the ARM GNU toolchain). See `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md` for the exact toolchain path and CMake platform file.

### KISS workflow for this repo

The driver lives at the repo root and is shared by every F Prime project here.
Run it from the repo root, not from a project directory:

```sh
cd fprime-artemis-cubesat
./tools/cross/cross_compile.sh
```

That builds the default target: `ArtemisRpiTeensyDeployment` in the
`ArtemisRpiTeensy_N2` project. To build something else, name it:

```sh
./tools/cross/cross_compile.sh --local-only \
    --project fprime-artemis-core \
    --deployment PayloadComputerDeployment
```

The deployment is located by directory name anywhere under the project, so flat
layouts (`ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment`) and nested ones
(`fprime-artemis-core/FprimeArtemisCore/Deployments/PayloadComputerDeployment`)
both work without extra flags. Pass a name that does not exist and the script
lists the deployments it can see.

### Cross Python environments

The container's Python environments live under `tools/cross/venv/<hash>` and are
shared between projects, not rebuilt per project. The hash is the first 12 chars
of `sha256sum <project>/lib/fprime/requirements.txt`.

That means two projects pinning the same F Prime release reuse one environment,
and a project on a different release gets its own. This repo currently needs
two: `ArtemisRpiTeensy_N2` pins fprime-tools 4.2.1 / fprime-fpp 3.2.0, while
`fprime-artemis-core` pins 4.3.0 / 3.3.0. Sharing one venv across those would
autocode a project with the wrong FPP compiler — a silent failure, so the split
is deliberate. A venv is only rebuilt when that project's requirements.txt
actually changes.

Both projects cross-compile today. `fprime-artemis-core` builds per target:
`ARTEMIS_TARGET_ZEPHYR` (derived from `CMAKE_TOOLCHAIN_FILE` before `project()`)
selects Zephyr + the Teensy deployments, or skips Zephyr and builds
`PayloadComputerDeployment` for Linux/ARM. `PayloadComputerDeployment` keeps its
own config overrides in `PayloadComputerConfig/` so the shared project config
stays sized for the Teensy.

Default mode is the normal iterative path. It reuses the Docker image, Pi
sysroot, shared cross Python environment, and F Prime build cache when
present, then still builds and verifies the final ARMv6 binary with `readelf`.

Use local-only when you only need the ARM artifact and dictionary:

```sh
./tools/cross/cross_compile.sh --local-only
```

Use clean mode when the toolchain, sysroot, Docker base image, or Python
dependencies may be stale:

```sh
./tools/cross/cross_compile.sh --clean
```

`--clean` rebuilds the Docker image, recreates this project's cross Python venv, and
force-regenerates the selected project's F Prime build cache. It deliberately
leaves the sysroot alone, because `tools/cross/sysroot` is now shared by every
project — use `--resync-sysroot` to re-copy it from the Pi. Do not use it for
every small C++/FPP iteration; the slow part is usually Python dependency setup,
and forced regeneration also throws away incremental compile state.

## Quick start (macOS on Apple Silicon)

1) Ensure Docker or Rancher Desktop is running.
2) Pull the ARM container:

```sh
docker pull nasafprime/fprime-arm:latest
```

3) Start the container and mount the project:

```sh
docker run --platform=linux/amd64 --net host -e USER=$USER \
  -u "`id -u`:`id -g`" -v "/path/to/project:/project" \
  -it nasafprime/fprime-arm:latest
```

4) Inside the container, cross-compile:

```sh
cd /project/MyProject/HelloWorldDeployment
export ARM_TOOLS_PATH=/opt/toolchains
fprime-util generate aarch64-linux
fprime-util build aarch64-linux
```

## Linux toolchain install (if not using the container)

Use a Linux VM or host, then:

```sh
sudo mkdir -p /opt/toolchains
sudo chown $USER /opt/toolchains
curl -Ls https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu.tar.xz \
  | tar -JC /opt/toolchains --strip-components=1 -x
```

Verify:

```sh
/opt/toolchains/bin/aarch64-none-linux-gnu-gcc -v
```

## ARM runtime workflow (optional)

1) Copy the binary to the device:

```sh
scp build-artifacts/aarch64-linux/<deployment>/bin/<deployment> <user>@<device-address>:deployment
```

2) Run GDS in client mode with the ARM dictionary:

```sh
fprime-gds -n --dictionary build-artifacts/aarch64-linux/<deployment>/dict/<App Dictionary>.json \
  --ip-client --ip-address <device-address>
```

3) SSH into the device and start the deployment:

```sh
ssh <user>@<device-address>
./deployment -a 0.0.0.0 -p 50000
```

## Notes

- macOS users must run cross-compilation commands inside the Docker container.
- Windows users run the same Docker flow inside WSL2 (Docker Desktop with the
  WSL2 backend); native PowerShell/CMD is not a supported F Prime path.
- For repo-specific context and prior attempts, read `references/context.md`.
