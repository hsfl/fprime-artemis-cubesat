# Local twin to physical Artemis kit: readiness and command map

Source audit: 2026-11-11 HST. No hardware operation performed by this audit.

## Status

The architecture is designed for the same flight/payload components to run on
Teensy/Zephyr and Pi/Linux. Physical operation requires different build targets,
real device bindings, camera selection, startup management and a hardware ground
transport. The complete hardware deployment is **not yet a verified turnkey
release** of the local twin.

| Item | Current evidence |
|---|---|
| Local FC/Pi/Yamcs and existing C3M file analysis | Complete recorded-camera twin and unchanged-app checks passed |
| Teensy41 USB/RFM23 firmware | Fresh target compile/link and cache placement passed; no flash/board run |
| Pi Linux binary | Linux ARM64 container build/run passed |
| Pi Zero W ARMv6 binary for this latest split core | Historical cross-build tooling exists; current F′ 4.4.2 configuration needs validation/fix |
| Ground Teensy/USB/RF into Yamcs | Existing physical bridge sources exist; twin ground launcher is wired to virtual device TCP |
| Real Lepton, UART, power/PDU, sensors, runtime memory | Physical integration/qualification remains open |

## 1. Build the real Teensy deployment

From the repository root on macOS, use the existing tested target helper:

```sh
python3 fprime-artemis-core/Platform/build_target.py \
  --sdk /path/to/zephyr-sdk-0.17.4 \
  --tools-bin /path/to/target-venv/bin \
  --build-dir /path/outside/source/teensy-rfm23 \
  --ground-link RFM23
```

Use a separate directory with `--ground-link USB` for USB bring-up. The helper
sets `BOARD=teensy41` and `ARTEMIS_TWIN=OFF`, builds FlightControllerDeployment,
and audits the ELF. Outputs include `zephyr/zephyr.hex` and `.elf`. It never
flashes a board. See [target evidence](../../fprime-artemis-core/Platform/evidence/README.md).
Windows development follows the project WSL/toolchain workflow.

The existing [Teensy upload guide](../../fprime-artemis-core/docs/uploading/teensy.md)
documents the loader interface:

```sh
teensy_loader_cli -v -mmcu=TEENSY41 -w /path/to/target-build/zephyr/zephyr.hex
```

This command writes physical firmware. It is shown as a command reference,
not a completed flash test or instruction to operate an unidentified device.
Use the intended satellite Teensy and applicable bench procedure. The generic
`build-flash-run.md` reference also contains STM32 examples; those do not flash
a Teensy.

## 2. Cross-build Pi for its actual CPU/OS

The local Docker payload executable is ARM64 Linux. A Pi Zero W is ARMv6
hard-float; copying the ARM64 binary cannot make it compatible. Pi models/OS
images differ, so verify the actual CPU, userland ABI, dynamic loader and libraries.

The repository's ARMv6 command interface is:

```sh
./tools/cross/cross_compile.sh --local-only --skip-sync \
  --project fprime-artemis-core \
  --deployment PayloadComputerDeployment
```

This reuses a previously prepared target sysroot and skips SSH deployment. It is
an existing interface, **not a newly verified successful command for this branch**.
The current wrapper inherits `fprime-artemis-core/settings.ini`, whose library
list includes fprime-zephyr. The working native Pi builder explicitly omits that
library for Linux. The ARMv6 core path needs the equivalent Linux library selection
before current-release cross-build compatibility can be claimed.

See [existing ARMv6 handoff](../CROSS_COMPILE_HANDOFF_PI_ZERO_W.md) for sysroot,
CPU/runtime-object verification and prior results. Those results predate this
latest split-core/F′ upgrade. Cross-build acceptance needs the current binary's
ARM attributes, interpreter and dependencies checked against the target.

## 3. Copy and start the Pi payload application

After that target build is validated, the deployment shape is:

```sh
scp /path/to/verified/PayloadComputerDeployment USER@PI:/path/to/release/
ssh USER@PI
```

The selected Pi release needs the actual built executable name, executable
permissions, required camera libraries, writable product storage, the matching
Pi dictionary retained on ground, and a startup/service definition for the new
PayloadComputerDeployment. Existing legacy Pi-primary services must not own the
same UART concurrently. Do not substitute the old mission-computer service unit
without reviewing its binary, arguments, working directory and environment.

The new payload CLI accepts `-d <FC-UART-device> -b 115200`. The physical UART
path and permissions must match the configured Pi and harness. On hardware,
leave `ARTEMIS_TWIN` unset and select the explicit real camera backend
`LEPTON_CAMERA_BACKEND=uvc`. The build must have libuvc/libusb enabled and the
camera accessible. `auto` can fall back to recorded/synthetic input, so it is
not evidence of real capture. Exact hardware start/rollback commands remain a
release/bench task, not a verified procedure in this document.

The cross wrapper's default mode can sync over SSH, copy and execute a smoke
run. Its default smoke uses `-d /dev/null`; that does not validate the real FC
link or camera. Do not treat a smoke timeout/success as a complete collection.

## 4. Replace the twin ground transport

`tools/twin/ground.py` currently reads packet-radio records from the virtual
device service on TCP port 6107. It is not an existing physical Ground Teensy
serial launcher. Hardware ground integration must connect the real bridge's
command/telemetry and science interfaces to the Yamcs adapter and validated
science publication path. Preserve independent channels, receiver ownership,
RF configuration and matching generated dictionaries.

The same operator commands and file contract are intended to remain valid after
that transport integration. See the [architecture](../architecture/ARTEMIS_LOCAL_TWIN.md)
and [operator walkthrough](../operations/walkthroughs/C3M_YAMCS_LOCAL_TWIN.md).

## Required hardware milestone

Before describing the branch as hardware ready: fix/validate the current Pi
cross-build path; package Pi service/environment/dictionaries and rollback;
validate the physical ground adapter; then perform an authorized bench run with
real camera, FC/Pi UART, power/PDU, sensors and radio. Repeat the full capture →
cache → Pi-off repair → Yamcs download → unchanged C3M app acceptance, including
actual target heap/stack peaks and timing. None of those physical operations
were performed by the local twin implementation or this audit.
