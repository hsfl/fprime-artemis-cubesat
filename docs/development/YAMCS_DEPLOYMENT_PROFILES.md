# Named Yamcs deployment profiles

Current configuration and hardware-free validation, 2026-11-12 HST.

Both profiles use the same FC mission/cache/radio components, Pi payload service
and unchanged C3M file contract. They select different execution targets and
ground transports. Profile definitions are in `config/deployment_profiles.json`;
`tools/deployments.py` is the complete build/runtime entry point.

| Profile | FC | Pi | Ground |
|---|---|---|---|
| `yamcs-twin` | Zephyr native_sim, modeled peripherals/RFM23 | Linux host application, explicit recorded Lepton | Virtual packet link and Yamcs in Docker |
| `yamcs-rfm23-hil` | Teensy41 firmware, real RFM23BP driver | Pi Zero W ARMv6 hard-float, required UVC | Explicit Ground Teensy CDC0/CDC2 serial ports and native-host Yamcs |

The named CMake configure/build presets in `fprime-artemis-core/CMakePresets.json`
cover the FC portion only. `yamcs-twin`'s native_sim CMake preset runs inside
Linux; it is intentionally hidden on macOS. Use the Python entry point from
macOS to run that build in Docker. The HIL preset explicitly selects RFM23,
not the generic legacy preset's USB default.

## Local digital twin

From the repository root on macOS with Docker Desktop running:

```sh
python3 tools/deployments.py yamcs-twin show
python3 tools/deployments.py yamcs-twin build
python3 tools/deployments.py yamcs-twin up
python3 tools/deployments.py yamcs-twin status
python3 tools/deployments.py yamcs-twin demo --captures 1
python3 tools/deployments.py yamcs-twin demo --captures 1 --pi-off-repair
python3 tools/deployments.py yamcs-twin test
python3 tools/deployments.py yamcs-twin down
```

These delegate to the existing twin launcher. Existing `tools/twin/twin.py`
commands remain supported. Yamcs is at localhost:8090 and device faults at
localhost:8092. Persistent Pi/Yamcs volumes survive `down`.

## Build the HIL release without hardware

The HIL build always uses local-only cross compilation and a previously prepared
Pi sysroot. It performs no SSH, copying to Pi, firmware flashing or service
installation. Supply an installed Zephyr SDK, a Python tools environment matching
the pinned framework requirements, and a Pi Zero W sysroot with GCC runtime,
libuvc/libusb headers and ARM libraries:

```sh
python3 tools/deployments.py yamcs-rfm23-hil show
python3 tools/deployments.py yamcs-rfm23-hil build \
  --sdk /path/to/zephyr-sdk-0.17.4 \
  --tools-bin /path/to/target-venv/bin \
  --sysroot /path/to/prepared-pi-zero-w-sysroot
```

Use `--role fc` or `--role pi` to rebuild one role, then rebuild the other if
source/dependency identity changed. Optional `--output PATH` changes the release
root; the default is `.profiles/yamcs-rfm23-hil` (ignored generated state).
The external sysroot is mounted read-only, including when its host path contains
spaces. `--require-uvc` is mandatory in this profile: absence of the physical
camera backend fails configuration instead of producing a synthetic-only release.

Artifacts:

```text
.profiles/yamcs-rfm23-hil/artifacts/fc/firmware/zephyr.hex
.profiles/yamcs-rfm23-hil/artifacts/fc/bin/flight-controller   # ARM target ELF
.profiles/yamcs-rfm23-hil/artifacts/fc/dict/topology.json
.profiles/yamcs-rfm23-hil/artifacts/pi/bin/payload-computer   # ARMv6 Linux ELF
.profiles/yamcs-rfm23-hil/artifacts/pi/dict/topology.json
.profiles/yamcs-rfm23-hil/artifacts/{fc,pi}/manifest.json
```

The build rejects native ARM64/x86 binaries for HIL artifacts. The cross
validator checks ARMv6/VFP/hard-float attributes, the dynamic interpreter,
recursive target dependencies and explicit UVC linkage. Preserve that manifest
with the release. The tested sysroot includes `/usr/local/lib/libuvc.so.0`; the
actual Pi must provide the matching ABI/libraries. See the cross report rather
than assuming any library with the same filename is equivalent.

## Pi startup package

The HIL build also generates:

```text
artifacts/pi/run-payload.sh
artifacts/pi/config/payload.env
artifacts/pi/config/artemis-payload.service
```

Defaults are user `pi`, release root `/home/pi/artemis/payload-core`, and FC UART
`/dev/serial0`. Override during build with `--pi-user`, `--pi-root`, `--pi-uart`.
The unit expects a writable release directory/product storage and appropriate
`dialout`/`video` permissions. The wrapper unsets ARTEMIS_TWIN (including a value
of `0`, which still counts as present), clears the sample fixture setting, and
forces `LEPTON_CAMERA_BACKEND=uvc` before launching `-d UART -b 115200`.

These are generated installation inputs. No service is installed or enabled by
building the profile. Review actual UART console configuration, camera USB
permissions/libraries, release ownership, existing services and rollback before
an authorized bench deployment. The old Pi-primary service must not concurrently
own the same UART.

## Physical Ground Teensy to Yamcs

The adapter reuses the existing Ground Teensy TripleSerial firmware. Build it
locally without uploading:

```sh
cd GDS_Teensy
./tools/arduino-cli/build.sh
cd ..
```

The sketch HEX is under `GDS_Teensy/build/arduino-cli/`; the existing
`GDS_Teensy/tools/arduino-cli/upload.sh` and bench runbook handle separately
authorized installation on the identified ground board. This build passed on
the Mac; the ground board was not flashed or run.

Run the ground service on the host which owns USB serial, using a Python
environment with the pinned `fprime-artemis-core/lib/fprime/requirements.txt`
dependencies (includes fprime-yamcs, bundled Java, pyserial and PyYAML). On macOS,
USB serial is not passed through Docker Desktop by this launcher.

```sh
python3 tools/deployments.py yamcs-rfm23-hil ground \
  --ground-python /path/to/ground-venv/bin/python \
  --ccsds-port /dev/cu.GROUND_CDC0 \
  --science-port /dev/cu.GROUND_CDC2
```

`up` is an alias for HIL `ground`. It runs in the foreground; Ctrl-C closes its
owned serial ports and Yamcs process. Optional `--yamcs-port`, `--tcp-port`,
`--data-dir` and `--output` select isolated instances. Defaults are HTTP 8090 and
loopback adapter TCP 5100. Use free distinct ports; stop the twin ground service
first if sharing those defaults. Ground opens the explicitly named devices:
only run this command with physical ports during an authorized bench operation.

The Ground Teensy TripleSerial contract is:

- CDC0 / `Serial`: raw CCSDS commands and telemetry, bidirectional.
- CDC1 / `SerialUSB1`: debug console, not a data transport.
- CDC2 / `SerialUSB2`: raw science packets and RETRY requests, bidirectional.

Firmware already handles RF segmentation and ACKs. The serial adapter bypasses
the virtual GroundLink packet codec and never adds a second RF envelope. It
reassembles the science byte stream across USB reads before feeding the production
receiver, and retains the same CRC-valid-file → Yamcs upload → exact GET path.
Ports are explicit, distinct and exclusively opened. Disconnect clears partial
stream state and stale queued uplinks; reconnect requires the configured path to
return. No automatic enumeration, radio setup or physical power commands occur.

The serial profile requires matching physical FC/Pi manifests and dictionaries;
renaming twin artifacts does not make them a HIL release. The Pi must report its
required UVC build. Keep science reception independent of the C3M analysis app:
Yamcs delivers the raw file, and the app opens that downloaded file with the
producing Pi dictionary as documented in the operator walkthrough.

## Hardware-free tests versus bench acceptance

Hardware-free checks include real Teensy cross-link, real ARMv6/UVC cross-link
with dependency inspection, ARMv6 QEMU startup when available, profile guards,
serial PTY fragmentation/reconnect/CRC/repair checks, and actual native Yamcs
command/file publication through PTYs. A recorded FDP used in a PTY test is
identified as replay, not a new hardware camera capture. See the [acceptance evidence](evidence/YAMCS_DEPLOYMENT_PROFILES_2026-11-12.json)
for the exact observed artifacts and results.

Actual flashing, Pi service installation/boot, real camera acquisition, physical
RF/USB/PDU wiring and loaded target timing/heap/stack remain bench acceptance.
This profile implementation does not claim those operations have been exercised.
Windows developers use WSL2 for F′ builds; a physical ground host must have its
serial devices attached to that environment or use a native Python ground setup.


The PTY acceptance helper creates both local serial endpoints itself, launches
real Yamcs and the serial adapter, submits PING, drops one replayed science
packet, verifies RETRY and independently GETs the exact object. It closes only
its own processes; no physical device is opened:

```sh
/path/to/ground-venv/bin/python tools/twin/serial_probe.py \
  --fc-artifacts .profiles/yamcs-rfm23-hil/artifacts/fc \
  --pi-artifacts .profiles/yamcs-rfm23-hil/artifacts/pi \
  --fdp /path/to/previously-verified-c3m.fdp \
  --output .profiles/yamcs-rfm23-hil/new-pty-proof
```

Use a fresh output directory. Default PTY test ports are HTTP 18090/TCP 15100,
so it can run beside the twin. For all 28 Python runtime/profile/serial tests,
use the ground environment's Python (PyYAML and pyserial installed); minimal
Python environments may skip serial-dependent cases. The four cross-wrapper
contract tests use `python3 -m unittest discover -s tools/cross/tests -v`.


## Acceptance recorded 2026-11-12

Both named builds passed. The hardware Pi binary passed ARMv6 ELF/runtime-library
inspection and QEMU ARM1176 startup with recorded camera input. The real native
Yamcs/PTY probe recovered the exact 38,486-byte replay after packet loss. A fresh
twin passed three complete captures, Pi-off cached repair and five fault cases.
Its downloaded file passed the unchanged C3M app, including all 19,200 temperature
samples. All 45 local/host tests and the Ground Teensy firmware build passed.
The two stale ground snapshot tests now allow only the documented `7c6de54`
demo frequency override; no product frequency was changed. Exact hashes and
scope are retained in the linked acceptance evidence. No physical bench
acceptance is claimed.
