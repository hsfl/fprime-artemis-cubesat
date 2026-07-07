# J - ARMv6 Cross-Compile Verification

Date: 2026-07-06 HST

## Result

PASS. The hardened working tree cross-compiles locally for Pi Zero W ARMv6, and the output binary has ARMv6-compatible attributes. No SSH, Pi access, deploy, or sysroot refresh was performed.

## Command Log Summary

Run from `ArtemisRpiTeensy_N2/`:

```sh
./tools/docker_cross_compile_pi_zero_w.sh --local-only --skip-sync 2>&1 | tee cross/pi-zero-w/verify/J-cross-build.log
```

Summary:

```text
Cross-build target configuration
  ssh host: pi@raspberrypi-zero-w
  sysroot: /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/cross/pi-zero-w/sysroot
  remote dir: /home/pi/artemis/cross
  docker image: artemis-pi-zero-w-cross:local
  clean: false
  local only: true

Reusing existing Docker image: artemis-pi-zero-w-cross:local
Reusing existing F Prime build cache: /repo/ArtemisRpiTeensy_N2/build-fprime-automatic-pi-zero-w-armv6hf
-- Installing: /repo/ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment
Local-only cross compile + verification completed successfully
```

The only notable build warning was:

```text
-- [INFO] Failed to find 'syft' on PATH, please install to generate software bill-of-materials
```

This is SBOM tooling only; it did not affect compile/link/readelf verification.

## Artifacts

Fresh binary:

```text
ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment
size: 2,303,820 bytes
sha256: 009297d518423d3e4405517983cad40a59ae5d96c7e29d13080b09c36cb5ef80
```

Dictionary:

```text
ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
size: 341,511 bytes
sha256: c51dddcdff9d70722caa2e55d2b8da37a5bef3984fa72b0a278f682b26d746fd
```

Verification logs:

```text
ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/J-cross-build.log
ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/file.txt
ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/readelf-A.txt
ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/readelf-l.txt
ArtemisRpiTeensy_N2/cross/pi-zero-w/verify/J-qemu-smoke.log
```

## Readelf Evidence

`file`:

```text
/repo/ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment: ELF 32-bit LSB pie executable, ARM, EABI5 version 1 (GNU/Linux), dynamically linked, interpreter /lib/ld-linux-armhf.so.3, BuildID[sha1]=9e566998fa96949220dfb80c741e71169a258878, for GNU/Linux 3.2.0, not stripped
```

`readelf -l`:

```text
[Requesting program interpreter: /lib/ld-linux-armhf.so.3]
```

`readelf -A`:

```text
Tag_CPU_name: "6KZ"
Tag_CPU_arch: v6KZ
Tag_FP_arch: VFPv2
Tag_ABI_VFP_args: VFP registers
Tag_CPU_unaligned_access: v6
```

## Qemu Smoke

Best-effort qemu smoke was run in a throwaway `artemis-pi-zero-w-cross:local` container. The container did not already include `qemu-arm`, so `qemu-user` was installed inside that throwaway container only.

Command shape:

```sh
timeout 6s qemu-arm \
  -L /repo/ArtemisRpiTeensy_N2/cross/pi-zero-w/sysroot \
  /repo/ArtemisRpiTeensy_N2/build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment \
  -d /dev/serial0
```

Transcript excerpt:

```text
Hit Ctrl-C to quit
EVENT: (16789504) ... ACTIVITY_LO: (version) FrameworkVersion : Framework Version: [v4.2.1-dirty]
EVENT: (16789505) ... ACTIVITY_LO: (version) ProjectVersion : Project Version: [v1.0.0-mvp-demo-dirty]
EVENT: (16789506) ... ACTIVITY_LO: (version) LibraryVersions : Library Versions: [pi-zero-w@v1.0.0-mvp-demo-dirty]
EVENT: (268517376) ... WARNING_HI: (comDriver) OpenError : Error opening UART device /dev/serial0: -1 No such file or directory
Assert: "/repo/ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/ArtemisRpiTeensyDeploymentTopology.cpp:79"
FATAL 16797696 handled.
Exiting with abort signal and core dump file.
qemu: uncaught target signal 6 (Aborted) - core dumped
Aborted
```

Interpretation: PASS for sanity. The binary starts under ARM emulation, registers commands, emits version events, and reaches the expected no-UART failure path. It did not fail with an immediate segfault or illegal instruction.

## Frozen vs Fresh Size

The frozen/pre-run binary under `build-artifacts/pi-zero-w-armv6hf/` was recorded before this run at:

```text
2,277,028 bytes
```

The fresh hardened binary is:

```text
2,303,820 bytes
```

Delta:

```text
+26,792 bytes
```

This is a meaningful size change and is consistent with the hardening changes being reflected in the ARMv6 artifact.

## Bench-Attention Warnings

- No bench/Pi deployment was performed in this task.
- Qemu smoke confirms startup and ARM ISA sanity only; real HIL still needs `/dev/serial0`, RF channel 0/1, and payload receiver proof.
- Runtime without `/dev/serial0` aborts through the deployment assert path after logging the UART open failure. That is expected for this no-UART smoke, but bench validation should confirm normal startup with the actual Pi UART device.
- SBOM generation is unavailable in the cross container because `syft` is not installed. This is non-blocking for demo hardening, but worth tracking if release provenance becomes a requirement.
