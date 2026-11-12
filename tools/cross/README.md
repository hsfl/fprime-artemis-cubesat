# Pi Zero W cross-build and local ARMv6 check

Run these commands from the repository root. Docker Desktop must be running,
and `PI_SYSROOT_DIR` must name an already prepared **Pi Zero W ARMv6 hard-float**
sysroot containing its loader, glibc, GCC runtime objects and camera libraries.
An arbitrary Debian `armhf` sysroot can contain ARMv7 code and is unsuitable.

The `yamcs-rfm23-hil` profile uses this wrapper with `--local-only --skip-sync
--require-uvc`. These flags build the hardware Pi executable without contacting
the Pi. The wrapper's historical default still includes SSH deployment; keep
the explicit local flags for development and profile builds.

```bash
export PI_SYSROOT_DIR=/absolute/path/to/prepared/pi-zero-w/sysroot
./tools/cross/cross_compile.sh \
  --local-only --skip-sync --require-uvc \
  --project fprime-artemis-core \
  --deployment PayloadComputerDeployment \
  --sysroot "$PI_SYSROOT_DIR"
```

The external sysroot is mounted read-only at `/sysroot`. Local-only mode never
syncs, deploys or runs SSH; combining it with `--resync-sysroot` fails. The core
Linux build explicitly omits `fprime-zephyr` and disables the twin build flag.
It uses direct CMake configuration/build with the pinned F′ Python environment:
`fprime-util generate` merges the Zephyr defaults from `settings.ini` into its
library list. Direct CMake supplies the Linux list without editing that shared
settings file. Other projects retain the existing `fprime-util` build path.
`--require-uvc` makes camera-library discovery mandatory rather than producing
a hardware artifact with the UVC backend disabled.

Python tools are cached under `tools/cross/venv/<requirements-hash>` using the
selected project's pinned F′ requirements. This separates F′ 4.4.2 tooling from
older deployments. CMake configuration is regenerated when the requirements,
Linux library selection, UVC requirement or sysroot location changes.

## Artifacts and verification

For the core payload deployment the files are:

```text
fprime-artemis-core/build-artifacts/pi-zero-w-armv6hf/
  FprimeArtemisCore_Deployments_PayloadComputerDeployment/
    bin/FprimeArtemisCore_Deployments_PayloadComputerDeployment
    dict/PayloadComputerDeploymentTopologyDictionary.json
tools/cross/verify/fprime-artemis-core/PayloadComputerDeployment/
  manifest.json
  binary-path.txt
  dictionary-path.txt
  file.txt
  readelf-A.txt
  readelf-l.txt
```

The manifest records binary/dictionary SHA-256, framework/source revision,
dirty-source status, ARM attributes, interpreter and recursive shared-library
paths/hashes. Its `binary_path` and `dictionary_path` use the container's
`/repo/` prefix; host callers replace that prefix with this repository's path.
The profile builder normalizes these paths in its own artifact manifest.

Verification rejects a non-ARM ELF, CPU attributes newer than ARMv6, the wrong
hard-float ABI/interpreter, missing shared dependencies or missing required
UVC linkage. Absolute target symlinks are resolved inside the sysroot.
Library search order is explicit in `library_search_paths`, with
`/usr/local/lib` first to match the prepared camera library. A physical Pi must
have the matching runtime libraries installed and discoverable; the sysroot
and executable alone do not establish that.

## Execute the hardware binary locally with QEMU

Rebuild the cross image once after adding QEMU, or omit this command if the
current image already contains `qemu-arm`:

```bash
docker build -t artemis-pi-cross:local \
  -f tools/cross/docker/Dockerfile tools/cross/docker
docker run --rm \
  -v "$PWD:/repo:ro" \
  -v "$PI_SYSROOT_DIR:/sysroot:ro" \
  -v "$PWD/tools/cross/verify:/verify" \
  -w /repo artemis-pi-cross:local \
  python tools/cross/smoke_qemu.py \
  --manifest /repo/tools/cross/verify/fprime-artemis-core/PayloadComputerDeployment/manifest.json \
  --sysroot /sysroot \
  --fixture /repo/ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv \
  --output /verify/fprime-artemis-core/PayloadComputerDeployment/qemu-smoke.json
```

This runs the exact manifest-identified executable on QEMU's `arm1176` CPU with
the target loader/libraries, a newly created PTY UART, and an explicitly selected
recorded Lepton fixture. It checks five seconds of application liveness,
recorded-camera readiness and clean SIGTERM exit. JSON and application logs are
written beside the manifest. This is a CPU/loader/application smoke check;
physical UART, USB camera, power, RF and full HIL remain bench checks.

## Local contract tests

```bash
python3 -m unittest discover -s tools/cross/tests -v
bash -n tools/cross/cross_compile.sh
```

These tests cover local-only rejection of target sync, read-only external
sysroot mounting, mandatory UVC propagation, and target symlink resolution.

## Verified milestone, 2026-11-12

The complete F′ 4.4.2 `PayloadComputerDeployment` cross-build passed with real
UVC linkage required, using the retained C3M Pi Zero W Raspbian Bookworm
sysroot. ELF inspection reported ARMv6KZ, VFPv2, hard-float VFP arguments and
`/lib/ld-linux-armhf.so.3`; recursive runtime dependencies were ARMv6 compatible.
The source checkout was dirty during the build; generated manifests identify
the exact resulting artifacts. No hardware was contacted.

The same executable then passed the five-second QEMU `arm1176` check with the
recorded Lepton backend and virtual UART. It reported camera ready and exited
with status 0 after SIGTERM. Binary SHA-256:
`cf4462147632808c77c8508b1e255aac13ad6fa52758530593b41b3e7d1de09d`.
The generated `qemu-smoke.json` and `.log` retain the emulator version, fixture
hash and runtime result. All four local contract tests passed.
