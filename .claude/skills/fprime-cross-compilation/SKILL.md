---
name: fprime-cross-compilation
description: "Cross-compile F' (F Prime) deployments for ARM/Linux targets, especially on macOS using the F' ARM Docker container. Use for any F' cross-compilation tasks: installing the ARM toolchain, building a deployment for aarch64-linux or arm-hf-linux, or running the ARM GDS workflow (scp + fprime-gds --ip-client)."
---

# F' Cross-Compilation (Tutorial 3)

## This Repo — Pi Zero W

**Read first:** `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md` — covers the full handoff state, known issues, and what has/hasn't been validated on the actual hardware.

**Critical landmine — ARMv6, not ARMv7:** The Raspberry Pi Zero W uses an ARMv6 CPU (BCM2835). Do NOT use `aarch64-linux` or standard `arm-hf-linux` toolchains — they target ARMv7+ and will produce binaries that SIGILL on the Pi Zero W. You must use an ARMv6-compatible toolchain (e.g. `arm-linux-gnueabihf` from the Raspberry Pi Foundation toolchain, not the ARM GNU toolchain). See `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md` for the exact toolchain path and CMake platform file.

### KISS workflow for this repo

Run from the active F' project root:

```sh
cd ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh
```

Default mode is the normal iterative path. It reuses the Docker image, Pi
sysroot, `.cross-venv-linux` Python environment, and F Prime build cache when
present, then still builds and verifies the final ARMv6 binary with `readelf`.

Use local-only when you only need the ARM artifact and dictionary:

```sh
./tools/docker_cross_compile_pi_zero_w.sh --local-only
```

Use clean mode when the toolchain, sysroot, Docker base image, or Python
dependencies may be stale:

```sh
./tools/docker_cross_compile_pi_zero_w.sh --clean
```

`--clean` refreshes the sysroot, rebuilds the Docker image, recreates the cross
Python venv, and force-regenerates the F Prime build cache. Do not use it for
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
