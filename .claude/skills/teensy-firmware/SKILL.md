---
name: teensy-firmware
description: "Use when building, uploading, or modifying Teensy 4.1 bridge firmware in this repo: the satellite Teensy workspace (ArtemisTeensy_N2_Baremetal), the ground/GDS Teensy workspace (GDS_Teensy), arduino-cli build/upload scripts, upload-ID targeting with multiple boards, the UART/RF link protocol, transport constants regeneration, and debug counter interpretation."
---

# Teensy Bridge Firmware

## Two Workspaces — Do Not Mix

| | Satellite | Ground/GDS |
|---|-----------|------------|
| Workspace | `ArtemisTeensy_N2_Baremetal/` | `GDS_Teensy/` |
| Sketch | `firmware/satellite_teensy/satellite_teensy.ino` | `firmware/gds_teensy/gds_teensy.ino` |
| FQBN | `teensy:avr:teensy41` (single serial) | `teensy:avr:teensy41:usb=serial3` (**triple serial**) |
| HIL bench upload ID | `usb:2100000` | `usb:100000` |
| Debug marker | `[ArtemisTeensy]` counters | `[GDS_Teensy]` counters |

Shared Arduino libraries live in `ArtemisTeensy_N2_Baremetal/firmware/libs/`
— the GDS build script pulls from there too, so library changes affect BOTH
boards.

Ground triple-serial port roles: first = GDS channel 0 data, second = ground
debug counters, third = payload channel 1 receiver.

## Platform Notes (macOS first, then Windows/Linux)

- **macOS** (primary driver platform): serial ports appear as
  `/dev/cu.usbmodem*`. First upload may fail while `teensy.app` starts — retry
  once it is open. BSD userland has no `timeout`; use `gtimeout` from
  `brew install coreutils`, or run `cat <port>` and Ctrl-C after a few seconds.
- **Windows**: all repo commands run inside **WSL2**, never PowerShell/CMD.
  Attach the Teensy to WSL first from an **administrator PowerShell** window:
  `usbipd bind --busid <BUSID>` (once), then `usbipd attach --wsl --busid <BUSID>`
  each session. Ports then appear as `/dev/ttyACM*`. Do not use `COM3`-style
  names in repo scripts. Full procedure: `docs/STUDENT_WINDOWS_LAPTOP_SETUP.md`.
- **Linux / WSL2 serial permissions (`sudo`)**: if a serial command fails with
  `Permission denied`, the durable fix is
  `sudo usermod -aG dialout "$USER"` then restart WSL (or log out/in on Linux).
  For a one-off demo emergency, `sudo` on the specific serial command works,
  but keep the repo venv active and the same port. Do not `sudo` builds,
  `fprime-util`, or `arduino-cli compile` — only hardware-facing serial access
  ever needs it.

## Build and Upload

Always run from the workspace root (scripts set `ARDUINO_CONFIG_FILE` to the
workspace-local `tools/arduino-cli/arduino-cli.yaml`; running elsewhere picks
up the wrong config):

```bash
cd GDS_Teensy                      # or ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000    # usb:2100000 for satellite
```

### Upload targeting — mandatory rule

**Never upload by `/dev/*` serial path when more than one Teensy is
connected.** arduino-cli auto-search can flash the wrong physical board and
report success — the ground board then enumerates as triple serial while
running wrong/stale firmware, which looks exactly like a dead RF/GDS link.

- List physical upload IDs first: `arduino-cli board list` (rows starting `usb:`).
- `upload.sh` auto-targets when exactly one Teensy is present, and **refuses**
  an ambiguous serial-path upload when multiple are present. That refusal is
  correct — do not bypass it by retrying other `/dev/*` paths; use the `usb:*` ID.
- If upload stalls needing a physical PROGRAM press, ask the operator to press
  it, retry once, then stop. On macOS the first upload can fail while
  `teensy.app` starts; retry once it is open.

### Post-flash proof (enumeration is not proof)

```bash
python3 -m serial.tools.list_ports -v
timeout 5 cat <ground-debug-port>   # expect [GDS_Teensy] counters
timeout 5 cat <sat-debug-port>      # expect [ArtemisTeensy] counters
```

(macOS: `gtimeout` or plain `cat` + Ctrl-C; see Platform Notes.)

## Link Protocol — Generated Constants, Do Not Hand-Edit

`src/link_protocol.hpp` (both workspaces) is **generated** from
`config/transport_constants.json` by `tools/generate_transport_constants.py`
(repo root). To change framing/timing/channel constants:

1. Edit `config/transport_constants.json`.
2. Rerun `python3 tools/generate_transport_constants.py`.
3. Rebuild BOTH Teensy workspaces and the F Prime side — the constants are a
   shared contract; `./tools/validate_local.sh` verifies generated headers
   match the manifest.

Hand-editing `link_protocol.hpp` desyncs satellite, ground, and F Prime and
fails validation.

Protocol shape (for reading code/counters, not for re-derivation):

- UART frames: magic `0xD4 0xC3`, opaque payload tagged by virtual channel.
- Channels: `0` = CCSDS/GDS, `1` = payload downlink, `2` = satellite-local RPC
  (not sent over RF).
- RF path segments frames into ≤49-byte RFM23 packets with per-segment
  ACK/retry (`RF_ACK_RETRIES`, `RF_ACK_TIMEOUT_MS`) and reassembly timeout.
- Debug-serial text commands: `#PING` → `#PONG`, `#LINK_STATUS`,
  `#RESET_COUNTERS`.

## Source Map

| Concern | Files (per workspace, under `firmware/*/src/`) |
|---------|------------------------------------------------|
| UART↔RF relay loop | `relay_uart_rf.{hpp,cpp}` |
| RFM23BP radio driver | `rf23_driver.{hpp,cpp}`, `artemis_rf23bp.hpp` |
| Link framing contract | `link_protocol.hpp` (generated) |
| Debug counters | `link_counters.hpp` |
| Satellite-only: local RPC router, PDU proxy | `local_teensy_router.*`, `pdu_proxy.*` |

## Counter-Based RF Triage

When commands reach the ground Teensy but not the Pi:

```text
uart_rx rises, rf_tx_pkt stays 0            → ground relay not transmitting (firmware/config)
rf_tx_pkt rises, sat rf_rx_pkt stays 0      → RF path (antenna, power, frequency)
rf_tx_drops / rf_retries / rf_ack_timeouts rise → uplink/ACK quality problem
ground streams silent but sat rf_tx_pkt rises   → suspect wrong/stale GROUND upload first
```

## Common Pitfalls

- Running `arduino-cli` outside the workspace root (wrong config path).
- Editing generated build cache under `build/arduino-cli/` instead of source.
- Changing UART/link settings in firmware without regenerating from the
  manifest and updating the F Prime side (see Link Protocol above).
- `upload.sh` "Compiled sketch not found" → run `build.sh` first.
- Stale `build/arduino-cli` cache causing link errors → remove the dir or
  `arduino-cli compile --clean`.
- Treating triple-serial enumeration as proof the right firmware runs — always
  check debug counters.

For staged bring-up with hardware attached (which board to plug first, serial
enumeration shapes, GDS/receiver startup), use the `fprime-hil-testing` skill.
