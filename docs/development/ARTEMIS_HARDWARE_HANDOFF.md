# Local twin to physical Artemis kit: readiness and command map

Updated 2026-11-12 HST. Builds and local tests only; no hardware operated.

## Status

Both named profiles are implemented. `yamcs-twin` runs the split FC/Pi system
locally; `yamcs-rfm23-hil` builds the physical targets, packages the Pi startup
inputs and runs Yamcs through explicit Ground Teensy serial interfaces. The
remaining acceptance step is an authorized physical bench run.

| Item | Current evidence |
|---|---|
| Local FC/Pi/Yamcs and C3M file analysis | Full recorded-camera flow and unchanged-app checks passed |
| Teensy41 RFM23 firmware | Target compile/link, HEX and full 192 KiB cache placement passed |
| Pi Zero W ARMv6/UVC executable | Current F′ 4.4.2 cross-build, ELF/dependency inspection and QEMU ARM1176 startup passed |
| Pi startup package | Explicit UVC wrapper, environment and systemd unit generated; installation/boot untested |
| Ground Teensy serial into Yamcs | Actual Yamcs + two local PTYs passed command forwarding, science packet-loss repair and exact file GET |
| Real camera/UART/PDU/sensors/RF; runtime memory/timing | Physical bench acceptance remains open |

Use the [named-profile runbook](YAMCS_DEPLOYMENT_PROFILES.md) for complete build,
artifact, environment and ground commands. The [architecture](../architecture/ARTEMIS_LOCAL_TWIN.md)
and [operator walkthrough](../operations/walkthroughs/C3M_YAMCS_LOCAL_TWIN.md)
explain ownership and the power → capture → cache → downlink → download flow.

## Build both physical applications locally

From the repository root on macOS, with Docker Desktop running:

```sh
python3 tools/deployments.py yamcs-rfm23-hil build \
  --sdk /path/to/zephyr-sdk-0.17.4 \
  --tools-bin /path/to/target-venv/bin \
  --sysroot /path/to/prepared-pi-zero-w-sysroot
```

Windows F′ development uses WSL2. This command never flashes, SSHs, copies to a
Pi or installs a service. It emits `.profiles/yamcs-rfm23-hil/artifacts/fc` and
`artifacts/pi`, each with its executable, producing dictionary and hash/target
manifest. FC includes `firmware/zephyr.hex`; Pi includes `run-payload.sh`,
`config/payload.env` and `config/artemis-payload.service`.

The Pi build now honors an external read-only sysroot, selects Linux-only F′
libraries and requires libuvc/libusb. The old wrapper imported Zephyr defaults
through `fprime-util`; the core cross path now uses direct CMake with an explicit
Linux library list. A Linux ARM64 twin executable cannot run on Pi Zero W.
The release executable is ARMv6KZ/VFPv2 hard-float with the Pi loader and recursive
runtime dependencies validated. Those matching libraries must also be installed
and discoverable on the physical Pi. See [cross-build details](../../tools/cross/README.md).

## Physical installation references

The [Teensy upload guide](../../fprime-artemis-core/docs/uploading/teensy.md)
documents the loader interface. After identifying the intended satellite board
and authorizing a bench operation, the artifact is:

```sh
teensy_loader_cli -v -mmcu=TEENSY41 -w .profiles/yamcs-rfm23-hil/artifacts/fc/firmware/zephyr.hex
```

For Pi, the generated artifact directory is the release root layout. Review
`payload.env` for the actual UART and writable release/product directory, then
copy that directory to the selected Pi release location. The service unit uses
that same location; `--pi-user`, `--pi-root` and `--pi-uart` customize generation.
Install/enable the reviewed unit using the Pi's bench procedure. Retain the prior
release and unit for rollback. Stop any legacy Pi-primary service that owns the
same UART. No copy, service installation, boot or rollback was exercised here.

The startup wrapper clears twin/recorded-input overrides and forces
`LEPTON_CAMERA_BACKEND=uvc`. The camera, permissions, libuvc/libusb runtime and
115200-baud FC UART must work on the actual Pi. The local QEMU check deliberately
selects a recorded fixture and proves CPU/loader/application startup only.

## Physical ground transport

```sh
python3 tools/deployments.py yamcs-rfm23-hil ground \
  --ground-python /path/to/ground-venv/bin/python \
  --ccsds-port /dev/cu.GROUND_CDC0 \
  --science-port /dev/cu.GROUND_CDC2
```

Run this hardware-facing command only during the authorized bench operation.
CDC0 carries raw CCSDS, CDC1 is debug, and CDC2 carries raw science/RETRY. Ground
firmware already handles RF framing. The host serial adapter adds no second RF
layer, handles split/coalesced USB reads, and clears stale input/uplink state on
disconnect. It requires matched HIL target manifests and the UVC Pi build.
Yamcs owns raw file delivery; the unchanged C3M app imports the downloaded FDP
with the producing Pi dictionary for image/temperature analysis.

## Remaining bench milestone

Identify hardware and populated-board/harness revisions, install the matching
runtime, then repeat capture → cache → Pi-off repair → Yamcs GET → unchanged C3M
import using the real Lepton, UART, PDU/power, sensors and RFM23BP. Measure target
heap/stack peaks and timing under load. Static reservations fit, but link/QEMU/
PTY evidence does not establish physical behavior or flight qualification.
