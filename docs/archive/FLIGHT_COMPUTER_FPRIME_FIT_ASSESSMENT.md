# F Prime Fit Assessment For ISIS iOBC-Class Flight Computer

## Inputs

Flight computer constraints provided for the final target:

- CPU: Atmel `AT91SAM9G20`
- CPU class: 32-bit ARM9
- Core speed: `400 MHz`
- Code storage: `1 MB` parallel NOR flash
- Boot model: code copied from flash into RAM at startup
- Critical data storage: `256 kB` SPI FRAM
- Mass storage: `2 x 2 GB` SD cards, FAT32

Measured comparison artifacts from this repository's Pi-style Docker cross-build study:

- F Prime `v3.6.4` minimal deployment raw binary: `994,528` bytes
- F Prime `v3.6.4` stripped binary: `625,036` bytes
- F Prime `v4.0.0` minimal deployment raw binary: `1,553,220` bytes
- F Prime `v4.0.0` stripped binary: `1,022,504` bytes

Reference measurement report:

- [PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md](PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md)

## Executive Finding

Based on the current Docker cross-build results alone:

- `F Prime v4.0.0` does not fit comfortably in a `1 MB` code store.
- `F Prime v3.6.4` only barely fits in raw form if `1 MB` means `1,048,576` bytes, leaving about `54,048` bytes of headroom.
- Neither result is strong enough to approve flight use yet, because these measurements are from a Linux `ARMv6` PIE executable, while the final CPU is an `ARM9 AT91SAM9G20`, which is a materially different target.

## Direct Fit Check Against 1 MB NOR Flash

Assuming `1 MB = 1,048,576 bytes`:

| Version | Measured binary | Size (bytes) | Fits in 1,048,576 bytes? | Margin |
|---|---|---:|---|---:|
| `v3.6.4` | Raw | 994,528 | Yes, barely | 54,048 bytes free |
| `v3.6.4` | Stripped | 625,036 | Yes | 423,540 bytes free |
| `v4.0.0` | Raw | 1,553,220 | No | 504,644 bytes over |
| `v4.0.0` | Stripped | 1,022,504 | Yes, barely | 26,072 bytes free |

## Why This Is Not Yet Flight-Credible

The current binaries are useful as an early framework-sizing signal, but not as a final acceptance result.

Reasons:

- The measured binaries target `ARMv6` hard-float Linux, verified as `Tag_CPU_arch: v6KZ`.
- The `AT91SAM9G20` is an `ARM9` part, commonly associated with `ARM926EJ-S`, not an `ARMv6` Pi Zero W class target.
- The measured executables are Linux PIE executables with dynamic loader dependency:
  - `/lib/ld-linux-armhf.so.3`
- Your hardware description emphasizes NOR flash boot into RAM, FRAM for critical state, and SD for bulk files. That is not enough by itself to assume a Linux runtime environment equivalent to the Pi-style build.

In short:

- The current study is good for relative framework overhead.
- It is not yet the right architecture/runtime combination for a final code-storage decision.

## Recommendations

### 1. Do not baseline on F Prime v4 for this OBC yet

`v4.0.0` is currently too close to or over the `1 MB` budget depending on whether the delivered image is stripped. A flight program should not plan around `26,072` bytes of remaining headroom on a code image this early.

Recommendation:

- Treat `v4.0.0` as `not acceptable yet` for this code-storage budget until a target-correct build proves otherwise.

### 2. Treat F Prime v3 as only a provisional candidate

`v3.6.4` is the only version that currently looks plausible, but only as a provisional candidate.

Recommendation:

- Use `v3.6.4` only if the eventual target-correct image remains well below `1 MB` after including:
  - final startup code
  - board support package
  - linker script layout
  - any required C/C++ runtime pieces
  - command/event/telemetry dictionaries or any packaged side artifacts, if they must live in NOR

### 3. Build for the actual CPU class next

The next technical gate should be a build for the actual flight CPU family and runtime model.

Recommendation:

- Produce a target-specific build for `AT91SAM9G20` / `ARM926EJ-S` class code generation.
- Decide explicitly whether the real system is:
  - Linux-based on the OBC
  - or bare-metal / RTOS-style

That choice will dominate code size much more than the current `v3` vs `v4` comparison.

### 4. Minimize framework services aggressively

If the goal is fitting inside `1 MB` NOR, the default starter deployment is already larger than is comfortable.

Recommendation:

- Remove anything not required for initial flight capability.
- Avoid default service-rich topologies.
- Minimize command/event/telemetry footprint.
- Avoid file-transfer and high-level service stacks in the boot image unless required at first flight.
- Consider a dedicated "safe mode / core avionics" deployment with only essential health, command, scheduling, and hardware-control paths.

### 5. Use FRAM and SD exactly for what they are good at

The provided memory architecture suggests a strong partitioning strategy.

Recommendation:

- Keep executable code and immutable boot assets in NOR flash.
- Store mission-critical mutable state in FRAM:
  - parameters
  - configuration/state snapshots
  - flight plan metadata
  - reboot reason / fault breadcrumbs
- Put bulky telemetry products, logs, payload data, and less-critical files on SD.

Do not spend NOR budget on data that belongs in FRAM or SD.

## Suggested Flight Software Strategy

For this hardware, the most defensible path is:

1. Start from the `v3.6.4` line, not `v4.0.0`.
2. Build a target-correct minimal deployment for the actual ARM9 environment.
3. Strip the binary and measure the exact final programmed image size.
4. Reserve explicit code-space margin for growth.

Recommended planning margin:

- target at least `20%` free space in NOR after the final linked image is measured
- for `1,048,576` bytes total NOR, that means trying to keep the programmed code image at or below about `838,860` bytes

By that standard:

- current `v3` stripped result at `625,036` bytes looks encouraging
- current `v3` raw result at `994,528` bytes does not
- current `v4` stripped result at `1,022,504` bytes is too tight to recommend

## Risks

### Architecture mismatch risk

The current ARMv6 Linux measurement may understate or overstate the true `ARM9` image size.

### Runtime model risk

If the real iOBC software stack is not Linux-based, then the current executable format is the wrong artifact to judge.

### Growth risk

Even if a minimal deployment fits today, mission-specific components, fault handling, storage support, and comms stacks can erase the remaining margin quickly.

### C++ runtime risk

F Prime and its selected service set may pull in more runtime support than is comfortable for a `1 MB` NOR budget unless aggressively constrained.

## Recommended Next Actions

1. Confirm the actual runtime model on the iOBC:
   - Linux
   - or bare-metal / RTOS
2. Build one target-correct minimal image for `AT91SAM9G20`-class code generation.
3. Measure the final flash-programmed artifact, not just the ELF on disk.
4. Create a "minimum flight subset" topology and compare it against the current starter deployment.
5. Only consider `v4` again if a target-correct stripped image lands comfortably below the NOR budget with margin.

## Bottom Line

If a decision had to be made from the currently available evidence:

- `v4.0.0`: reject for this `1 MB` NOR code budget
- `v3.6.4`: possible, but only as a starting point and only with a target-correct build plus margin validation

The right next move is not more Pi-style measurement. It is one architecture-correct build for the actual `AT91SAM9G20` flight environment.
