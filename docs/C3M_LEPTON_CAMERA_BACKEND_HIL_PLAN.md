# C3M Lepton Camera Backend HIL Plan

## Intent

Bring the real Lepton camera path back without undoing the `epscorc3m/demo`
architecture work.

The correct end state is one `PayloadDriver_Lepton` component with selectable
frame sources. Local emulation keeps using checked-in Lepton sample data. HIL
forces the real `libuvc` camera backend and fails if the camera is not actually
available.

This is not a plan to resurrect `PayloadAdapter_Lepton` as a parallel component.
Use the old branch only as source material for the camera backend and standalone
Pi harness.

## Current State

- Branch: `epscorc3m/demo`.
- Source branch for old hardware code: `EPSCOR_C3M_REFACTOR`.
- Current component: `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/`.
- Old hardware source:
  `EPSCOR_C3M_REFACTOR:ArtemisRpiTeensy_N2/Components/PayloadAdapter_Lepton/`.
- Local C3M demo is green using the real checked-in Lepton sample CSV from
  `ground-station/c3m-lepton-test-data/`.

## Implementation Status - 2026-07-08

Implemented on `epscorc3m/demo`:

- Ported the full old branch `libuvc` Lepton backend into current
  `PayloadDriver_Lepton/LeptonCamera.*`.
- Restored the clean compile-time split:
  - Linux with `libuvc` + `libusb-1.0`: `LEPTON_USE_LIBUVC` real camera path.
  - macOS/non-Linux/missing libs: sample/synthetic paths compile with no camera
    dependencies.
- Added explicit runtime backend selection:
  `LEPTON_CAMERA_BACKEND=uvc|sample|synthetic|auto`.
- Updated the C3M local demo to export `LEPTON_CAMERA_BACKEND=sample`.
- Added the F Prime-free `testLeptonCamera` harness under
  `PayloadDriver_Lepton/testLeptonCamera/`.
- Added `LeptonBackendSelected` event so GDS can prove whether the run used
  `uvc`, `sample`, or `synthetic`.
- Added real CRC16-CCITT source CRC emission from
  `PayloadDriver_Lepton::dpWrittenIn_handler()`.

Validated locally:

- `fprime-util generate -f`
- `fprime-util build`
- `./tools/validate_local.sh --demo c3m`
- `./tools/docker_cross_compile_pi_zero_w.sh --local-only`

Important cross-build caveat:

- The ARMv6 deployment cross-build passes, but the current local Pi sysroot did
  not expose `libuvc`, `libuvc/libuvc.h`, or `libusb-1.0`.
- That means the produced ARM artifact currently compiles the fail-hard
  non-`libuvc` path. It is architecture-valid, but not real-camera HIL-ready.
- Before HIL, install/sync the Pi camera libraries or build natively on the Pi
  until CMake reports `PayloadDriver_Lepton: libuvc enabled`.

Still pending hardware proof:

- Pi/Linux `libuvc` discovery and link in the actual HIL artifact.
- `LEPTON_CAMERA_BACKEND=uvc ./testLeptonCamera` on the Pi.
- F Prime `payloadDriverLepton.ENABLE` using `uvc`.
- Channel-1 RF downlink of a real-camera `.fdp`.

## Non-Goals

- Do not bring back `PayloadAdapter_Lepton` as a second topology component.
- Do not fork `PayloadManager`, `StorageManager`, `CommsApp`, or
  `PayloadDownlinkApp` for C3M.
- Do not make laptop local emulation require `libuvc`.
- Do not let HIL proof silently fall back to sample or synthetic data.
- Do not change the channel-1 payload downlink protocol as part of this camera
  backend work.

## Work Plan

### 1. Add Explicit Lepton Frame-Source Selection

Status: implemented.

Add a runtime selector such as:

```text
LEPTON_CAMERA_BACKEND=uvc|sample|synthetic|auto
```

Required policy:

- `uvc`: real Lepton camera only. If `LEPTON_USE_LIBUVC` is not compiled in, the
  camera is missing, stream negotiation fails, or no valid Y16 frame arrives,
  return an error. No fallback.
- `sample`: load the configured CSV sample only. If the sample cannot be read,
  return an error. No synthetic fallback.
- `synthetic`: deterministic generated frame only. Use for explicit fallback
  tests, not HIL proof.
- `auto`: developer convenience only. It may choose a reasonable available
  source, but runbooks must not use `auto` as proof of hardware capture.

Operational defaults:

- Local demo runner should explicitly set `LEPTON_CAMERA_BACKEND=sample` and
  `C3M_LEPTON_SAMPLE_CSV=.../ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv`.
- HIL runbook should explicitly set `LEPTON_CAMERA_BACKEND=uvc`.
- If a HIL run logs sample or synthetic backend selection, that run is not valid
  camera proof.

Implementation notes:

- Keep the public `LeptonCamera` API stable:
  `open()`, `getLatestFrame()`, `close()`, `isStreaming()`.
- Add a backend enum internally rather than spreading string checks through the
  driver.
- Log the selected backend through existing capture-ready/failure events, or add
  a small event if needed.
- Preserve the sample CSV behavior that local validation already depends on.

### 2. Restore Conservative `libuvc` Build Support

Status: implemented locally; Pi/HIL proof pending.

Port the proven real-camera implementation from
`EPSCOR_C3M_REFACTOR:.../PayloadAdapter_Lepton/LeptonCamera.*` into the current
`PayloadDriver_Lepton/LeptonCamera.*`.

Required hardware behavior to preserve:

- Persistent stream model: `ENABLE` opens once and starts streaming.
- Callback stores the latest valid frame.
- `CAPTURE_IMAGE` / scheduled capture copies the latest valid frame into the
  Data Product record.
- Y16 stream negotiation workaround for Lepton devices whose `libuvc` format
  table reports Y16 as unknown.
- FFC / blank-frame rejection before accepting a frame.
- Timeout while waiting for the first valid frame.

Required CMake behavior:

- Only define `LEPTON_USE_LIBUVC` when both `libuvc` and `libusb-1.0` are found.
- Gate real-camera discovery to Linux/Pi builds unless there is a deliberate
  future reason to support host-camera capture on macOS.
- Keep headers free of `libuvc` types. Use opaque handles so local/native builds
  do not need `libuvc` headers.
- On non-Linux or missing-library builds, compile the sample/synthetic paths
  without warnings or link failures.

Expected source edits:

- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/LeptonCamera.hpp`
- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/LeptonCamera.cpp`
- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/CMakeLists.txt`

Standalone harness:

- Bring over the old `testLeptonCamera` idea under the current driver directory
  or a clearly named tools path.
- Keep it F Prime-free so the Pi can prove `open()` plus `getLatestFrame()`
  before involving topology, GDS, RF, or Data Products.
- The harness must print enough stats to prove the frame is not blank:
  min, max, mean, and a few sample pixels.

### 3. Emit a Real `sourceCrc`

Status: implemented and validated in local emulation.

Make the C3M descriptor honest by computing CRC over the actual `.fdp` file
after `DpWriter` confirms the write.

Original issue:

- `PayloadDriver_Lepton::dpWrittenIn_handler()` emits the real file path and
  byte count, but uses `sourceCrc = 0`.
- `PayloadDownlinkApp` can already compare a nonzero expected source CRC against
  the source file before downlink.

Required behavior:

- Add `PayloadDriver_Lepton::computeFileCrc16(...)`.
- Use CRC16-CCITT with initial value `0xFFFF` and polynomial `0x1021`, matching
  `PayloadDownlinkApp` and `tools/payload_receiver.py`.
- Widen the U16 result into the existing U32 `sourceCrc` field.
- If CRC computation fails, treat the capture descriptor as failed rather than
  emitting `REAL_PAYLOAD` with a fake CRC.
- Preserve the final receiver CRC check; descriptor CRC is an earlier source
  integrity guard, not a replacement for ground reconstruction proof.

Expected source edits:

- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/PayloadDriver_Lepton.hpp`
- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/PayloadDriver_Lepton.cpp`
- Component unit tests if a test harness exists or is added.

## Verification Gates

Run these before HIL:

```bash
cd ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
fprime-util generate -f
fprime-util build
./tools/validate_local.sh --demo c3m
```

Also run:

```bash
python3 -m py_compile \
  ArtemisRpiTeensy_N2/tools/payload_receiver.py \
  ground-station/lepton-dp-viewer/lepton_dp_viewer.py
```

Cross-build gate, if the Docker/sysroot path is available:

```bash
cd ArtemisRpiTeensy_N2
./tools/docker_cross_compile_pi_zero_w.sh --local-only
```

Pi pre-HIL camera gate:

```bash
cd /home/pi/artemis/current
LEPTON_CAMERA_BACKEND=uvc ./testLeptonCamera
```

Minimum useful camera proof:

- `open()` succeeds.
- A valid frame arrives within timeout.
- Frame stats are not blank or all one value.
- Harness exits nonzero if any backend other than `uvc` is selected.

HIL proof gate:

- Start the Pi deployment with `LEPTON_CAMERA_BACKEND=uvc`.
- Send `payloadDriverLepton.ENABLE`.
- Run scheduled collection or `CAPTURE_IMAGE`.
- Verify a `.fdp` is written.
- Verify `ScienceProductDescriptor` contains nonzero path, size, and source CRC.
- Request channel-1 downlink.
- `payload_receiver.py` reconstructs the `.fdp` with final CRC OK.
- `ground-station/lepton-dp-viewer` decodes `160x120` / `19200` pixels and
  renders the PNG.

## Atomic Commit Shape

Original recommended commit sequence:

1. `add selectable lepton camera frame sources`
   - backend selector
   - local demo environment updated to force `sample`
   - local validation still green
2. `restore lepton libuvc backend for pi builds`
   - `libuvc` / `libusb` CMake detection
   - real Y16 streaming path
   - standalone Pi harness
   - native build still green
3. `emit real c3m lepton source crc`
   - file CRC helper
   - descriptor emits nonzero CRC
   - downlink path verifies expected CRC before transfer

Actual implementation may land as one HIL-prep commit if the branch owner wants
one atomic "restore real Lepton backend" change; the work is still split by
source responsibility in the diff.

## Stop Point Before HIL

Stop before claiming hardware readiness unless all local gates pass and the Pi
camera harness proves real `uvc` frame capture. The local sample path being
green is necessary for regression safety, but it is not evidence that the
Lepton hardware path works.
