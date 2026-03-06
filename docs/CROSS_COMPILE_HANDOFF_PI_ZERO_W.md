# Cross-Compile Handoff (Pi Zero W, Neutron 2)

Date: 2026-03-05 (HST)

## Scope

This note is for the next agent attempting F' cross-compilation for the Neutron 2 RPi target (`Raspberry Pi Zero W Rev 1.1`, `armv6l`).

## Use v4 docs only

Primary source of truth (v4/latest):
- https://fprime.jpl.nasa.gov/latest/docs/tutorials/cross-compilation/
- https://fprime.jpl.nasa.gov/latest/docs/reference/api/cmake/toolchain/raspberrypi/

Do not use v3 tutorial flow as primary guidance. v3 can be historical reference only.

Important device caveat for this project:
- v4 tutorial examples commonly emphasize newer Raspberry Pi/ARM64 flows.
- Our actual target is **Raspberry Pi Zero W v1.1** (`armv6`) on **Raspberry Pi OS Lite 32-bit**.
- Do not assume ARM64/aarch64 outputs are valid for this target.

## What is already proven

- F' cross-compilation is supported.
- macOS users are expected to run cross-compilation inside Docker.
- F' provides ARM Linux targets/toolchains (`aarch64-linux`, `arm-hf-linux`, and `raspberrypi` toolchain docs).

## Why prior attempts failed

Observed on this project:
- Target hardware is `armv6l` (Pi Zero W).
- Binaries built with wrong ARM generation caused `Illegal instruction` on device.

Interpretation:
- Cross-build itself is not the problem.
- Wrong ISA/ABI/sysroot defaults for Pi Zero W are the problem.

## Required constraints for Pi Zero W success

Any cross-build intended for Pi Zero W must match all of the following:
- ISA: ARMv6-compatible output
- ABI: hard-float expected by device userspace
- Loader/libs: compatible with Pi rootfs (sysroot must match target)

If these are not guaranteed, prefer native compile on Pi (`rpi_build.instructions`).

## Recommended next attempt checklist

1. Start from clean repo state and initialized submodules.
2. Use Docker on macOS for cross-compilation host environment.
3. Use latest (v4) tutorial steps as baseline.
4. Validate the selected toolchain/platform is appropriate for Pi Zero W specifically.
5. Ensure generated binary attributes match device requirements before deploy.
6. Deploy to Pi and run smoke test first:
   - `./ArtemisRpiTeensyDeployment -d /dev/null`
7. If smoke passes, run UART mode:
   - `./ArtemisRpiTeensyDeployment -d /dev/serial0`

## Verification gates (must capture in notes)

Before deploy:
- `file <binary>` output
- `readelf -A <binary>` output
- `readelf -l <binary>` output (interpreter)

On Pi:
- `uname -a`
- `lscpu` (or equivalent CPU info)
- smoke-test console output for `/dev/null`

## Decision rule for schedule

- If cross-build path produces any ISA/ABI mismatch ambiguity, stop and use native Pi build for deliverables.
- Revisit cross-build optimization only after native path is stable.
