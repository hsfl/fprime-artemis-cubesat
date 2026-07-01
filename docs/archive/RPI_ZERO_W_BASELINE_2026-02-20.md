# Raspberry Pi Baseline (Pi Zero W)

Captured on **2026-02-20** from target host `raspberrypi` for deployment triage.

## System

- `uname -a`:
  - `Linux raspberrypi 6.12.47+rpt-rpi-v6 #1 Raspbian 1:6.12.47-1+rpt1 (2025-09-16) armv6l GNU/Linux`
- Model:
  - `Raspberry Pi Zero W Rev 1.1`
- CPU:
  - `ARMv6-compatible processor rev 7 (v6l)`

## Memory Snapshot

- `free -h`:
  - `Mem total: 427 MiB`
  - `Mem used: 140 MiB`
  - `Mem free: 135 MiB`
  - `Mem available: 286 MiB`
  - `Swap total: 426 MiB`
  - `Swap used: 3.8 MiB`
- `/proc/meminfo` (top fields):
  - `MemTotal: 437772 kB`
  - `MemFree: 138480 kB`
  - `MemAvailable: 293632 kB`
  - `Cached: 186328 kB`
  - `SwapTotal: 437244 kB`
  - `SwapFree: 433404 kB`

## GPU/ARM Split

- `vcgencmd get_mem arm`:
  - `arm=448M`
- `vcgencmd get_mem gpu`:
  - `gpu=64M`

## Note For Deployment Debug

- Target is a 32-bit `armv6l` device with constrained memory budget.
- Use this baseline when evaluating startup crashes, stack sizing, and component footprint.
