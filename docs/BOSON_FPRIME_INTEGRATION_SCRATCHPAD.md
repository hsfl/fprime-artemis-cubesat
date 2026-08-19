# Boson F Prime Integration Scratchpad

## Goal

Implement and locally validate a separate native F Prime Boson payload driver
that supports the existing scheduled-capture, data-product, channel-1 downlink,
and ground-viewer demo flow without regressing the proven Lepton path.

## Working Branch

- Parent repo: `codex/boson-fprime-integration`
- Boson reference submodule: `external/epscorc3m`
- Boson reference branch/commit: `boson_dev` at
  `dcb1050285708d256ef06742531a9a60db5a687f`

## Scope

- Initial implementation and verification were local-only.
- Dennis explicitly approved Boson HIL, Raspberry Pi service interruption,
  deployment, and direct SSH retrieval on 2026-07-30.
- Firmware upload and Pi deployment were completed for HIL. Dennis approved
  organizing the finished integration into commits and pushing this branch on
  2026-07-30.
- Build and test the native macOS path, project unit tests, local emulation,
  both Teensy firmware workspaces, and the ARMv6 Pi sysroot path.
- Preserve `PayloadDriver_Lepton` and its 160x120 centikelvin data-product
  schema.

## Architecture Decisions

1. `PayloadDriver_Boson` is a sibling of `PayloadDriver_Lepton`.
2. Boson capture uses a dedicated native Linux V4L2 helper. It does not import
   the Lepton `libuvc` implementation and does not require Python or OpenCV in
   the flight process.
3. `PayloadManager` keeps its existing generic
   `PayloadCaptureRequest`/`ScienceProductDescriptor` interface.
4. A small payload-driver selector owns the Lepton/Boson request and status
   routing. Lepton remains the default. The camera drivers expose no direct
   capture commands, so every capture goes through this selector.
5. The project overrides `FwSizeStoreType` to `U32`, allowing both cameras to
   use one standard F Prime `.fdp` per capture.
6. Lepton and Boson keep separate FPP record schemas and separate ground
   decoders because their shapes and units differ. Both flow through the same
   FDP writer, channel-1 transport, receiver, and `.fdp` handling.
7. The one existing satellite payload cache is enlarged to 192 KiB and placed
   in Teensy RAM2. At runtime it contains at most the one selected camera's
   current product.
8. The existing channel-1 packet format remains unchanged unless local tests
   prove a concrete blocker.
9. Initial full-resolution target is Boson 320: 320x256 Y16. Boson 640 is out
   of scope for this slice.
10. Selection changes synchronously deactivate the prior driver before the new
    driver becomes active. Only one request may be in flight.

## Known Constraints

- Legacy U16 Lepton `.fdp`: 38,480 bytes; project U32 Lepton `.fdp`: 38,482
  bytes.
- Boson 320 raw pixels: 163,840 bytes.
- Project U32 Boson `.fdp`: 163,922 bytes.
- Project F Prime data-product buffer: 196,608 bytes.
- Satellite Teensy payload cache: 196,608 bytes.
- Channel-1 packet data: 35 bytes; Boson 320 is 4,684 packets.
- Boson `boson_dev` reference uses USB vendor `09cb`, V4L2 Y16, and strips two
  telemetry rows.
- Boson reference data is raw U16 counts. Do not label it as temperature
  without a validated radiometric conversion.

## Planned Work

- [x] Independent architecture/topology review.
- [x] Independent Boson V4L2/backend review.
- [x] Independent Data Products/Teensy memory and protocol review.
- [x] Independent ground decoder/UI review.
- [x] Add deterministic Boson synthetic and generated sample fixtures.
- [x] Implement native Boson camera helper and standalone tests.
- [x] Implement `PayloadDriver_Boson`.
- [x] Implement safe payload-driver selection.
- [x] Override `FwSizeStoreType` to U32 and add one full-frame standard Boson
      FDP.
- [x] Enlarge the one existing payload cache and preserve one-product state.
- [x] Add Boson decoder/viewer and receiver dispatch.
- [x] Add Boson local-demo path and tests.
- [x] Model the one target-3 Teensy cache in local emulation, including begin,
      chunk, commit, abort, completion status, and retry bitmap handling.
- [x] Run native F Prime build and component unit tests.
- [x] Run Python/ground tests and end-to-end local demo.
- [x] Build both Teensy firmware workspaces and inspect memory.
- [x] Run ARMv6 Pi sysroot build and inspect linked dependencies.
- [x] Re-run complete Lepton regression.
- [x] Complete independent final architecture and regression review.

## Acceptance Criteria

1. Existing Lepton local demo and tests remain green.
2. Boson sample/synthetic local demo completes:
   scheduled capture -> one `.fdp` file -> channel-1 reconstruction -> CRC
   -> PNG.
3. Boson record decodes as 320x256 raw U16 counts.
4. Boson and Lepton routing is deterministic, defaults to Lepton, and rejects
   unsafe selection while a request is active.
5. Native F Prime generation/build and project component unit tests pass.
6. Both Teensy firmware workspaces compile with acceptable reported memory.
7. ARMv6 sysroot build succeeds and reports the expected architecture,
   interpreter, and Boson backend availability.
8. Remaining limitations are explicitly HIL-only.

## Evidence Log

| Check | Result | Evidence |
| --- | --- | --- |
| Branch created | PASS | `codex/boson-fprime-integration` |
| Boson reference pinned | PASS | `dcb1050285708d256ef06742531a9a60db5a687f` |
| Untouched local baseline | PASS | `./tools/validate_local.sh --skip-demo`; 95 Python tests and 7/7 F Prime component test executables passed |
| Native U32 FDP build | PASS | `fprime-util generate -f && fprime-util build`; dictionary reports `FwSizeStoreType = U32` and Boson record size 81,920 U16 pixels |
| Complete local validation | PASS | `./tools/validate_local.sh --skip-demo`; generated transport check, 102 Python tests, native generation/build, and 8/8 F Prime component test executables passed |
| Boson camera helper | PASS | `testBosonCamera`; deterministic synthetic, 256-row sample, 258-row telemetry stripping, malformed sample, invalid backend, and V4L2 failure paths passed |
| Boson end-to-end demo | PASS | `tools/logs/c3m_local_demo_20260730_125642`; one 163,922-byte standard FDP traversed the one target-3 cache and 4,684 channel-1 packets, decoded as 320x256 raw U16 counts, and produced CSV/JSON/PNG |
| Lepton regression demo | PASS | `tools/logs/c3m_local_demo_20260730_125714`; one 38,482-byte standard FDP traversed the same cache and 1,100 packets, decoded as 160x120 centikelvin data, and produced CSV/JSON/PNG |
| Cache boundary/retry tests | PASS | Exact 163,922-byte Boson reconstruction, 17-byte final data chunk, oversize rejection, completion response, and retry selection passed using one 196,608-byte cache |
| Satellite Teensy compile | PASS | Code 62,768 bytes; RAM2 variables 209,664 bytes with 314,624 bytes free; `g_payloadCache` is in `.bss.dma` at `0x20200000` |
| Ground Teensy compile | PASS | Code 62,320 bytes; RAM2 variables 37,088 bytes with 487,200 bytes free |
| ARMv6 Pi sysroot build | PASS | Fresh `--local-only` build produced ELF32 ARM EABI5, ARMv6KZ/VFPv2 hard-float with `/lib/ld-linux-armhf.so.3`; detected Lepton `/usr/local/lib/libuvc.so` and direct Boson V4L2 |
| ARM dynamic dependencies | PASS | `libuvc.so.0`, `libstdc++.so.6`, `libgcc_s.so.1`, `libc.so.6`, and `ld-linux-armhf.so.3`; no OpenCV or libv4l2 runtime dependency |
| Selector ownership | PASS | Generated dictionary exposes only `payloadDriverSelector.SELECT_PAYLOAD_DRIVER`; no direct Lepton/Boson capture commands |
| Independent final review | PASS | No remaining actionable findings after cache routing, retry/completion, selector ownership, synchronous deactivation, and duplicate-request review |
| Boson stale-frame fix | PASS | Native V4L2 capture drains the completed MMAP queue before waiting for a new frame and rejects the observed 99% `0x8080` startup placeholder without rejecting uniform non-placeholder thermal counts |
| Updated native regression | PASS | `testBosonCamera` covers flat and sparse `0x8080` placeholder rejection plus valid uniform and varying raw-count frames |
| Updated ARMv6 build | PASS | Incremental Docker cross-build produced ARMv6KZ/VFPv2 hard-float with direct V4L2 enabled |
| Pi HIL deployment | PASS | Release `boson-fresh-frame-20260731T012000Z` activated with automatic rollback protection; `artemis-fprime.service` remained active |
| First real Boson capture | PASS | `ImageCaptureSuccess`; 163,922-byte FDP; SHA-256 `4517855042632c5194dba83291df31a325d059dd333fac0bdf17842678cb9939`; 320x256 counts min 14,024 max 15,373 |
| Idle-queue Boson recapture | PASS | Second capture after idle produced a distinct 163,922-byte FDP; SHA-256 `ba79fa838ce214321be274d84b9f28e4aa139d0e9705fb16589a5a7a32b59182`; counts min 13,152 max 14,539 |
| HIL image review | PASS | Both decoded PNGs show a structured thermal scene instead of the prior nearly all-white `0x8080` test pattern |
| Final review hardening | PASS | Exact 99-percent placeholder boundary now rejects; repeated ready/error frames cannot extend the absolute capture deadline |
| Final Pi deployment | PASS | Release `boson-fresh-frame-final-20260731T014500Z` active; final binary SHA-256 `6ff18ad60b2c371a30ea665b9c5aacfc4f7e66c7b0ed11b0ec819ba98e4c6538` |
| Final confirmed capture | PASS | 163,922-byte FDP; matching Pi/local SHA-256 `0754027c7c05e991077b28998c3a18efcf6c014e2fd50116ccd70e4d5a465097`; counts min 13,752 max 14,933; structured thermal PNG |
| Dual-camera HIL demo | PASS | Dennis confirmed viewable pictures from both the real Lepton and real Boson through the shared capture/downlink/viewer flow |
| Full Boson RF downlink | PASS | One 163,922-byte / 4,684-packet Boson product completed in about 3m13s with retries under current lab conditions; treat this as an observed bench result, not a guaranteed timing bound |
| Physical camera swap investigation | CAUTION | During a Boson-to-Lepton physical USB swap, the Pi abruptly rebooted before GDS sent the Lepton selector command. The new boot found an unclean filesystem; the selector teardown path was not implicated by the surviving command/event timeline |

## Remaining HIL Follow-Up

- Do not treat physical USB replacement as a powered hot-swap requirement.
  Power down the Pi before changing cameras, or use an independently powered
  USB hub and qualify that setup separately. After boot, select the connected
  driver in GDS. Logical driver selection remains safe and separate from the
  physical USB/power event.
- Repeat full Boson transfers in additional RF environments before using the
  observed 3m13s lab result as a demo timing budget.
