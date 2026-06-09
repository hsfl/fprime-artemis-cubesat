---
name: fprime-cross-compilation
description: "Cross-compile F' (F Prime) deployments for ARM/Linux targets, especially on macOS using the F' ARM Docker container. Use for any F' cross-compilation tasks: installing the ARM toolchain, building a deployment for aarch64-linux or arm-hf-linux, or running the ARM GDS workflow (scp + fprime-gds --ip-client)."
---

# F' Cross-Compilation (Tutorial 3)

## This Repo — Pi Zero W

**Read first:** `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md` — covers the full handoff state, known issues, and what has/hasn't been validated on the actual hardware.

**Critical landmine — ARMv6, not ARMv7:** The Raspberry Pi Zero W uses an ARMv6 CPU (BCM2835). Do NOT use `aarch64-linux` or standard `arm-hf-linux` toolchains — they target ARMv7+ and will produce binaries that SIGILL on the Pi Zero W. You must use an ARMv6-compatible toolchain (e.g. `arm-linux-gnueabihf` from the Raspberry Pi Foundation toolchain, not the ARM GNU toolchain). See `docs/CROSS_COMPILE_HANDOFF_PI_ZERO_W.md` for the exact toolchain path and CMake platform file.

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

## Notes for tutorial tracking

- Keep tutorial notes under `docs/tutorial-3-cross-compilation/` when following the repo tutorial workflow.
- macOS users must run cross-compilation commands inside the Docker container.
- For repo-specific context and prior attempts, read `references/context.md`.
