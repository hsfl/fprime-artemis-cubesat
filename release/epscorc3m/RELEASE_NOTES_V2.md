# EPSCoR C3M v2.0.0 Demo — SmallSat Conference 2026

This release advances the live-validated v1 Lepton RF demonstration into the
SmallSat Conference 2026 demo baseline. It keeps F Prime GDS on mission channel
0 and the C3M receiver on payload channel 1 while improving recovery, downlink
speed, operator visibility, and payload flexibility.

## Changes since v1.0.0

### Reliable demo operations

- Added bounded RF transmit recovery, one retry after a recovered timeout, and
  duplicate suppression without requiring a GDS reset.
- Preserved ground USB traffic under backpressure and separated RF message IDs
  by channel.
- Hardened shared-radio transmit-to-receive turnaround and added focused RF
  recovery, retry, buffer-transition, and thermal-soak tests.

### Resilient and faster science downlink

- Added resumable receiver transfers, retained state across local faults, and
  bounded repair rounds and frame bursts.
- Added on-demand Pi-to-satellite-Teensy payload caching so the Teensy can serve
  science packets and repair requests locally.
- Preserved completed transfers across receiver restart and saved useful
  operator-cancelled partial products.

### Live science visibility

- Added best-effort 80 by 60 Lepton preview transport and a live thermal viewer.
- Kept preview frames current while reliable full-resolution science products
  remain the authoritative data path.
- Added a thermal-hotspot visibility toggle to the ground display.

### Broader payload support

- Added a selectable F Prime payload-driver layer.
- Added Boson capture, full-size product support, and a ground Boson viewer.
- Retained the Lepton workflow as the established demo path.

### Ground operations and documentation

- Added the one-command `tools/c3m` RFM23BP operator launcher.
- Preserved the HackRF RF22 adapter and documentation for research, teaching,
  capture, and receive diagnosis; it is not the normal mission fallback.
- Expanded the operator runbook, radio architecture notes, live QA record, and
  reliability/repair documentation.

## Release verification

- Shared transport/config drift checks: pass.
- Python regression suite: 112 passed.
- Native F Prime build: 881 of 881 build steps passed.
- F Prime component tests: 9 of 9 suites passed.
- Automated local C3M demo: three consecutive capture/downlink/decode cycles
  passed.
- Satellite Teensy 4.1 single-serial firmware: built successfully.
- Ground Teensy 4.1 triple-serial firmware: built successfully.
- Raspberry Pi Zero W cross-build: verified ARMv6KZ, VFPv2, hard-float, with
  interpreter `/lib/ld-linux-armhf.so.3`.
- Deployment to the Raspberry Pi was intentionally not performed during this
  release build.
- Physical HIL acceptance is user-confirmed prior evidence; these build results
  do not independently reproduce that hardware test.

## Git comparison

- Previous tag: `v1.0.0-epscorc3m-demo`
- This tag: `v2.0.0-epscorc3m-demo`
- Compare: `v1.0.0-epscorc3m-demo...v2.0.0-epscorc3m-demo`
