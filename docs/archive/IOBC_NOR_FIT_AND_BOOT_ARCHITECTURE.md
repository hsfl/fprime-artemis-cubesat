# iOBC 1 MB NOR Fit: Boot Architecture Reframe

**Date:** 2026-06-29
**Status:** Analysis / recommendation. Reframes (does not contradict the measurements in) [FLIGHT_COMPUTER_FPRIME_FIT_ASSESSMENT.md](FLIGHT_COMPUTER_FPRIME_FIT_ASSESSMENT.md) and pairs with [V4_IOBC_DECISION_MEMO.md](V4_IOBC_DECISION_MEMO.md).

## Bottom Line

The "does F´ fit in 1 MB NOR?" question is sizing the framework against the **wrong budget**. On the ISIS iOBC (`AT91SAM9G20`), F´ does **not** execute from NOR flash. The application is loaded from the **SD card into SDRAM (~32 MB)** by a staged bootloader and runs there. The 1 MB NOR only has to hold a **first-stage bootloader (~40 KB)** plus, optionally, a small recovery image.

- **Do not** cull the full flexible F´ topology down to <1 MB to run from NOR — that is not realistic for the real deployment and is unnecessary.
- **Do** use the iOBC's native bootloader → SD → SDRAM path. Your real binary (~1.5–2.3 MB) fits in SDRAM with ~15–30× headroom, and the manager→service→adapter flexibility from [SYSTEM_ARCHITECTURE.md](../SYSTEM_ARCHITECTURE.md) survives intact.

The binding constraint moves from **1 MB NOR** to **~32 MB SDRAM**.

## Why The 1 MB Number Was Misleading

The fit assessment assumed the worst-case bare-metal boot model — "code copied from flash into RAM at startup," i.e. the entire program lives in the 1 MB NOR. That is a valid assumption *only* for a pure bare-metal, run-from-NOR design. It is **not** how the iOBC is normally flown.

## Measured Sizes (for reference)

ARMv6 Linux cross-build proxies (final target is ARM926EJ-S / ARMv5TEJ — re-measure on a target-correct build before any flight call):

| Artifact | Raw ELF | Stripped / loadable |
|---|---:|---:|
| Minimal fresh starter, F´ v3.6.4 | 994,528 (0.95 MB) | 625,036 (0.61 MB) |
| Minimal fresh starter, F´ v4.0.0 | 1,553,220 (1.48 MB) | 1,022,504 (0.98 MB) |
| **Real `ArtemisRpiTeensyDeployment`, pi-zero-w-armv6hf** | 2,277,028 (2.17 MB) | ~1,486,588 loadable (1.42 MB) |
| **Real `ArtemisRpiTeensyDeployment`, arm-hf-linux** | 1,856,332 (1.77 MB) | ~1,243,892 loadable (1.19 MB) |

"Loadable" = sum of `PT_LOAD` `filesz` (code + data actually loaded, excluding debug sections). The real deployment exceeds 1 MB even stripped — confirming that *run-from-NOR is not viable for the full topology*, and that the SD→SDRAM path is the right answer. See [PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md](PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md) for the minimal-starter study.

## The Actual iOBC Boot Chain

`AT91SAM9G20`, 400 MHz ARM9, ~32 MB SDRAM, 1 MB NOR flash, 256 KB FRAM, 2× 2 GB SD (redundant):

1. **NOR `0x0 → 0xA000` (~40 KB):** ISIS first-stage bootloader. This is *all* that must live at the bottom of NOR.
2. ISIS bootloader copies a second-stage loader (**U-Boot**) into SDRAM and runs it. Programs stored from `0xA000` onward are prepared to be loaded into SDRAM and started.
3. **U-Boot loads the application/kernel image from the SD card into SDRAM**, then jumps to it.
4. The application runs **from SDRAM (~32 MB)** — orders of magnitude larger than the F´ binary.

This is exactly the "point NOR at SD, load into RAM" model. (KubOS documents this chain precisely and is a good reference, but it is end-of-life — the U-Boot/bootloader integration would be a maintenance burden you own, same as the F´ platform port.)

## Two Boot Models Compared

| | Bare-metal, run-from-NOR | Loader → SD → SDRAM (iOBC native) |
|---|---|---|
| Budget | 1,048,576 bytes | ~32 MB SDRAM |
| Real deployment (~1.5 MB) | **Does not fit**, even after culling | Fits with ~20× headroom |
| Linking | Needs **static** linking (inflates image) | Dynamic OK — libs live on SD rootfs |
| Effort to fit | Heavy topology culling + size flags + static link | Essentially none |
| Verdict | Only viable as a stripped "safe-mode core avionics" subset | **Recommended** |

## Flight Heritage For Limited-Memory Boot

The recommended approach maps onto standard CubeSat boot-redundancy practice:

- **Golden image + slots.** First slot holds an immutable Golden Image installed pre-launch and never overwritten; additional slots hold uploaded FSW images as redundancy.
- **Fallback chain.** Boot the newest uplinked image first; on failure fall back to last-known-good, then other slots, finally the golden image.
- **Boot-count / watchdog failover.** Hardware watchdog plus a crash counter incremented each boot; after N failures switch to an alternate image. On the iOBC this is U-Boot `bootcount`/`bootlimit` → `altbootcmd`, with the count and environment persisted in the **256 KB FRAM** (non-volatile, radiation-tolerant).
- **Separate-SD failover.** The iOBC's **2× redundant SD cards** let a watchdog force boot from the backup card so a single-event-upset-corrupted image cannot brick the OBC.

## Recommended Memory Map

This is the [V4_IOBC_DECISION_MEMO.md](V4_IOBC_DECISION_MEMO.md) "Scenario E" staged-boot architecture, aligned with iOBC heritage:

- **NOR (1 MB):** ISIS bootloader (~40 KB) + a tiny immutable safe-mode/recovery image + image-verify/rollback entry logic. *Not* the full F´ app.
- **FRAM (256 KB):** active-slot pointer, boot-attempt counter, rollback flags, last-fault reason, critical params.
- **SD ×2 (2 GB):** primary + backup F´ application images as **fixed A/B slots** (not a bare FAT32 filepath — fixed slots are the more robust boot contract), loaded into SDRAM at boot.
- **SDRAM (~32 MB):** where F´ actually runs; the full flexible topology fits easily.

## Caveats

1. **Requires the loader-to-RAM runtime model** (ISIS/KubOS Linux, or your own U-Boot/AT91Bootstrap-from-SD setup). Pure bare-metal-from-NOR re-imposes the 1 MB wall.
2. **Static vs dynamic linking.** The current ELF is dynamically linked (`/lib/ld-linux-armhf.so.3`). Under an SD Linux rootfs the libs live on SD (fine); bare-metal would need static linking, which *grows* the image. Decide the runtime model first — it dominates code size more than v3-vs-v4.
3. **Architecture-correct build still needed.** All numbers above are ARMv6 Linux proxies; the iOBC is ARM926EJ-S (ARMv5TEJ). Re-measure on a real `-mcpu=arm926ej-s` build before any flight decision. For the SD→SDRAM path, fit is no longer the question — boot robustness and correctness are.
4. **KubOS is EOL.** Excellent boot-chain reference, but unmaintained; the integration burden is yours.

## Sources

- ISIS-OBC Quick Start Guide v2.2: <https://usermanual.wiki/Document/ISISOBC20QuickStart20Guide20v22.552359569/help>
- KubOS Linux on the ISIS iOBC: <https://docs.kubos.com/1.0.0/kubos-linux-on-iobc.html>
- CubeSat Flight Software: Insights and a Case Study (AIAA JSR): <https://arc.aiaa.org/doi/10.2514/1.A35882>
- On-Board Computer for CubeSats: State-of-the-Art and Future Trends: <https://www.researchgate.net/publication/382285921_On-Board_Computer_for_CubeSats_State-of-the-Art_and_Future_Trends>
- Microchip AT91SAM9G20 summary datasheet: <https://ww1.microchip.com/downloads/en/DeviceDoc/6384s.pdf>
- Companion repo docs: [FLIGHT_COMPUTER_FPRIME_FIT_ASSESSMENT.md](FLIGHT_COMPUTER_FPRIME_FIT_ASSESSMENT.md), [V4_IOBC_DECISION_MEMO.md](V4_IOBC_DECISION_MEMO.md), [PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md](PI_ZERO_W_MINIMAL_FPRIME_SIZE_COMPARISON.md)
