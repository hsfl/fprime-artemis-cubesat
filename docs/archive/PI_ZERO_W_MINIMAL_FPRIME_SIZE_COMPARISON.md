# Pi Zero W Minimal F Prime Size Comparison

## Scope

This compares the smallest fresh F Prime starter deployment I could build for the latest `v3` and `v4` major lines while staying aligned with the repository's Raspberry Pi Zero W cross-build approach.

- Base repository: `<repo-root>`
- Worktree for F Prime `v3.6.4`: `<fprime-artemis-cubesat-v3.6.4-worktree>`
- Worktree for F Prime `v4.0.0`: `<fprime-artemis-cubesat-v4.0.0-worktree>`
- Cross-build style: repo-local Pi Zero W Dockerfile + repo-local Pi Zero W sysroot/toolchain wiring
- Target class: ARMv6 hard-float Linux, Raspberry Pi Zero W style
- Deployment topology: fresh minimal starter deployment, not `ArtemisRpiTeensyDeployment` and not the Neutron 2 UART/Teensy topology

## Projects Built

### F Prime v3

- Release tag: `v3.6.4`
- Branch: `codex/pi-minimal-fprime-v3.6.4`
- Project root: `<fprime-artemis-cubesat-v3.6.4-worktree>/sizing/pi_minimal/fprime-minimal-v3`
- Deployment: `PiMinimalV3Deployment`
- Generated comm driver choice: `TcpClient`

### F Prime v4

- Release tag: `v4.0.0`
- Branch: `codex/pi-minimal-fprime-v4.0.0`
- Project root: `<fprime-artemis-cubesat-v4.0.0-worktree>/sizing/pi_minimal/fprime-minimal-v4`
- Deployment: `PiMinimalV4Deployment`
- Generated comm driver choice: `TcpClient`

## Build Method

Each project contains a local helper script:

- `<fprime-artemis-cubesat-v3.6.4-worktree>/sizing/pi_minimal/fprime-minimal-v3/tools/docker_cross_compile_pi_zero_w_size.sh`
- `<fprime-artemis-cubesat-v4.0.0-worktree>/sizing/pi_minimal/fprime-minimal-v4/tools/docker_cross_compile_pi_zero_w_size.sh`

Each helper:

1. Builds a project-local Docker image from `cross/pi-zero-w/docker/Dockerfile`.
2. Mounts the project-local Pi Zero W sysroot at `/workspace/cross/pi-zero-w/sysroot`.
3. Creates a container-local Python venv at `.cross-venv-linux`.
4. Installs the exact Python dependencies from `lib/fprime/requirements.txt`.
5. Runs:
   - `fprime-util generate pi-zero-w-armv6hf -f -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSROOT=/workspace/cross/pi-zero-w/sysroot`
   - `fprime-util build pi-zero-w-armv6hf`
6. Verifies the resulting ELF with `file`, `readelf`, and `arm-linux-gnueabihf-size`.
7. Measures:
   - raw deployment executable bytes
   - stripped-copy executable bytes

## Target Verification

Both outputs were verified as ARMv6-class Linux binaries.

### `v3.6.4`

- `file`: `ELF 32-bit LSB pie executable, ARM, EABI5, interpreter /lib/ld-linux-armhf.so.3, not stripped`
- `readelf -A`:
  - `Tag_CPU_name: "6KZ"`
  - `Tag_CPU_arch: v6KZ`
  - `Tag_FP_arch: VFPv2`
  - `Tag_ABI_VFP_args: VFP registers`

### `v4.0.0`

- `file`: `ELF 32-bit LSB pie executable, ARM, EABI5, interpreter /lib/ld-linux-armhf.so.3, not stripped`
- `readelf -A`:
  - `Tag_CPU_name: "6KZ"`
  - `Tag_CPU_arch: v6KZ`
  - `Tag_FP_arch: VFPv2`
  - `Tag_ABI_VFP_args: VFP registers`

## Results

| Version | Deployment | Raw bytes | Stripped bytes | `size -A` total |
|---|---|---:|---:|---:|
| `v3.6.4` | `PiMinimalV3Deployment` | 994,528 | 625,036 | 1,269,726 |
| `v4.0.0` | `PiMinimalV4Deployment` | 1,553,220 | 1,022,504 | 1,775,389 |

## Delta

Comparing `v4.0.0` against `v3.6.4`:

- Raw executable delta: `+558,692` bytes
- Raw executable percent change: `+56.18%`
- Stripped executable delta: `+397,468` bytes
- Stripped executable percent change: `+63.59%`
- `size -A` total delta: `+505,663` bytes

## Interpretation

For this specific minimal Pi-targeted starter deployment, F Prime `v4.0.0` produced a noticeably larger ARMv6 executable than `v3.6.4`.

The most conservative number for non-volatile storage planning is usually the on-disk raw or stripped executable size, depending on how the flight image is packaged:

- Use `raw bytes` if you store the binary exactly as built.
- Use `stripped bytes` if your release process strips symbols before packaging.

This report does not declare fit or no-fit for the final flight computer because the available non-volatile memory budget was not provided.

## Reproduction

### F Prime v3.6.4

```bash
cd <fprime-artemis-cubesat-v3.6.4-worktree>/sizing/pi_minimal/fprime-minimal-v3
./tools/docker_cross_compile_pi_zero_w_size.sh
```

Verification outputs:

- `cross/pi-zero-w/verify/file.txt`
- `cross/pi-zero-w/verify/readelf-A.txt`
- `cross/pi-zero-w/verify/readelf-l.txt`
- `cross/pi-zero-w/verify/size-A.txt`
- `cross/pi-zero-w/verify/raw-bytes.txt`
- `cross/pi-zero-w/verify/stripped-bytes.txt`

### F Prime v4.0.0

```bash
cd <fprime-artemis-cubesat-v4.0.0-worktree>/sizing/pi_minimal/fprime-minimal-v4
./tools/docker_cross_compile_pi_zero_w_size.sh
```

Verification outputs:

- `cross/pi-zero-w/verify/file.txt`
- `cross/pi-zero-w/verify/readelf-A.txt`
- `cross/pi-zero-w/verify/readelf-l.txt`
- `cross/pi-zero-w/verify/size-A.txt`
- `cross/pi-zero-w/verify/raw-bytes.txt`
- `cross/pi-zero-w/verify/stripped-bytes.txt`

## Notes

- This is not a native Pi build. It is a Docker-based cross-build using the repository's Pi Zero W style sysroot/toolchain flow, per the requested change in execution method.
- The generated minimal projects still contain bootstrap-generated nested Git metadata because `lib/fprime/.git` is wired to the project root's `.git/modules`. Removing that metadata cleanly would require re-vendoring the generated framework tree and was not necessary for the binary size comparison.
