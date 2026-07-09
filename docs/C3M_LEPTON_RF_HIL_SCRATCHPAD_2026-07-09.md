# C3M Lepton RF HIL Scratchpad — 2026-07-09

## Goal

Prove a real full-resolution Lepton camera capture can travel end to end over
the live RF HIL bench:

```text
Lepton -> Raspberry Pi F Prime -> satellite Teensy -> RFM23BP RF
-> ground Teensy -> payload_receiver.py -> .fdp -> Lepton PNG
```

Gate 1 is a nonzero, CRC-valid, openable `160x120` Lepton `.fdp` received on
the laptop. Gate 2 begins only after Gate 1 and aims for a reliable total
downlink time below one minute without reducing resolution or adding unrelated
features.

## Active Source Of Truth

- Branch: `epscorc3m/demo`
- Reference branch: `EPSCOR_C3M_REFACTOR` for proven Lepton backend behavior
  only; do not restore its old architecture.
- Required runbooks:
  - `docs/HIL_BENCH_HANDOFF_2026-07-09.md`
  - `docs/C3M_LEPTON_CAMERA_BACKEND_HIL_PLAN.md`
  - `docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`
- Architecture: `docs/SYSTEM_ARCHITECTURE.md`
- Transport constants: `config/transport_constants.json`

## Initial State From Handoff

- Ground Teensy firmware was flashed and its debug stream was proven.
- Satellite firmware compiled and was loaded in Teensy Loader, but the handoff
  did not prove that it was flashed; live debug evidence is required.
- A fresh ARMv6 deployment was copied to the C3M Pi at
  `/home/pi/artemis/cross/ArtemisRpiTeensyDeployment` and passed a `/dev/null`
  smoke, but the running service was deliberately not restarted.
- C3M Pi aliases: `artemis-pi-c3m` or `c3m-pi`.
- Handoff serial map:
  - GDS/channel 0: `/dev/cu.usbmodem115553301`
  - ground debug: `/dev/cu.usbmodem115553303`
  - payload/channel 1: `/dev/cu.usbmodem115553305`
  - satellite debug: `/dev/cu.usbmodem115565001`
- Handoff upload IDs: ground `usb:100000`, satellite `usb:2100000`.

## Hard Gates

### Camera gate

- Pi artifact must be built with `libuvc` enabled.
- `LEPTON_CAMERA_BACKEND=uvc ./testLeptonCamera` must return a nonblank frame.
- F Prime must emit `LeptonBackendSelected backend=uvc`; no sample, synthetic,
  or auto fallback counts as HIL proof.

### Gate 1 — received file

- GDS command/event path works over channel 0.
- A real `.fdp` is produced and staged on the Pi with nonzero size/source CRC.
- `payload_receiver.py` reports a complete transfer and final CRC success.
- Ground file exists, is nonzero, and matches the Pi source hash.
- `lepton_dp_viewer.py` reports `width=160`, `height=120`, `pixels=19200` and
  renders an openable PNG.

### Gate 2 — reliable speed

- Measure wall time from `PayloadDownlinkStarted` to receiver completion.
- Preserve full resolution and final CRC correctness.
- Keep channel 0 usable and both Teensy queue-drop counters at zero.
- Target less than 60 seconds using only conservative pacing/transport
  hardening and optimization.

## Live Run Log

### 2026-07-09 — initialization

- Read required runbooks, repo architecture, README, current agent notes, and
  HIL/Teensy/F Prime/testing skill instructions.
- Verified starting Git state: `epscorc3m/demo`, with the handoff document
  untracked and preserved.
- Next: enumerate live ports/process owners, prove both Teensy debug streams,
  inspect C3M Pi service/artifacts/libraries/camera, then start the smallest GDS
  command smoke.

### 2026-07-09 — live inventory and camera gate

- Live USB/upload map matches the handoff exactly. No process owned the four
  serial ports at inventory time.
- Both firmware roles are proven from live debug output:
  - ground: `[GDS_Teensy]` counters on `...3303`
  - satellite: `[ArtemisTeensy]` boot/counters on `...5001`
- Channel 0 is already moving Pi telemetry over RF. Sample counters showed no
  queue drops, no reassembly drops, and two accumulated ground message-ID gaps.
- C3M Pi confirmed at `192.168.0.234`, `armv6l`; Lepton enumerates as USB
  `1e4e:0100 Cubeternet WebCam` with `/dev/video*` nodes present.
- Pi has usable `libuvc` and `libusb-1.0` headers/libraries.
- Built the current branch's standalone harness natively at
  `/home/pi/artemis/camera-harness/testLeptonCamera` and ran it with explicit
  `LEPTON_CAMERA_BACKEND=uvc`.
- Camera gate PASS:
  `open OK, streaming`; frame `min=28845 max=30596 mean=29765`, nonblank.
- Running service is still the legacy mapped executable, project version
  `9eba742-dirty`, with old `*Manager/*Service/*Adapter` naming. It has libuvc
  but is not the current unified App-Man-Drv architecture.
- Preserved that mapped executable for rollback at
  `/home/pi/artemis/backup/ArtemisRpiTeensyDeployment-9eba742-uvc`, SHA-256
  `d52e9e1d60e3793363a241bb5a5832d475e9a6a4c4adbedb4cf336d142faf742`.
- The freshly staged current binary SHA-256
  `d45ac7a0b00e6aa2741f9a264ac02a6d18ec0a83ee40699ac0e77b99162c6690`
  has no libuvc dependency and correctly fails hard if `uvc` is requested.
- Root cause: the cached local ARMv6 sysroot/build predates the Pi's current
  `/usr/local` libuvc installation; CMake cached all Lepton UVC paths as
  `NOTFOUND`. Refresh the sysroot and force-regenerate before deployment.
- Receiver runbook defect found before live use: actual script is
  `ArtemisRpiTeensy_N2/tools/payload_receiver.py`; there is no `--continuous`
  flag; directory mode is already continuous and needs `--ext .fdp` for the
  Lepton viewer glob.
- Confirmed the old branch's `TEST-DATA-DOWNLINK` content is fully preserved
  byte-for-byte under `ground-station/c3m-lepton-test-data/`: `.fdp`, CSV,
  JSON, PNG, README, and original `dp_lepton_viewer.py`.
- The maintained `ground-station/lepton-dp-viewer/lepton_dp_viewer.py` decoded
  the reference as `160x120`, `19200` pixels and reproduced the reference CSV
  hash exactly; no legacy viewer/data feature is missing.
- Pi filesystem history explains the working old setup:
  - `/home/pi/libuvc`: upstream libuvc checkout, `v0.0.7-2-g047920b`
  - `/usr/local/include/libuvc` and `/usr/local/lib/libuvc.so.0.0.7`: built and
    installed 2026-02-23
  - `/home/pi/epscor-c3m-flirlepton`: HSFL camera tools/test frames
  - `/home/pi/testLeptonCamera`: older proven UVC harness from 2026-06-18
  - no full F Prime source tree remains on the Pi; current deployment artifacts
    are kept under `/home/pi/artemis`.
- Patched the RF runbook receiver invocation to the real path/API with
  `--output-dir`, `--ext .fdp`, `--idle-timeout 240`, and `--debug`.
- Current throughput model: a full `.fdp` is `38480` bytes, `1100` data packets
  and `1104` total channel-1 messages. At the existing `32` messages per 1 Hz
  F Prime run, nominal handoff is about `34-35 s`; healthy current settings
  should already meet the sub-minute target without a speed feature change.

### 2026-07-09 — current deployment and first RF isolation

- Refreshed the ARMv6 sysroot, fixed CMake's cross-sysroot libuvc discovery,
  and built a current deployment with libuvc enabled. The binary is ARMv6KZ,
  links `libuvc`, `libusb-1.0`, and `libudev`, and passed a target smoke.
- Staged the release under `/home/pi/artemis/releases/c3m-hil-20260709`, pointed
  `/home/pi/artemis/current` at it, and migrated the service to the repo-owned
  unit with explicit `LEPTON_CAMERA_BACKEND=uvc`. Current `*App` instance names
  and the running executable hash were verified.
- GDS channel-0 command smoke passed with `missionApp.PING 7091` and a matching
  Pi `pong` event.
- Current F Prime UVC capture passed end to end on the Pi: backend `uvc`,
  `ImageCaptureQueued` with 38,415 data bytes, `ImageCaptureSuccess` with a
  38,480-byte `.fdp`, `ScienceProductReady`, and `ScienceStored`.
- First 38,480-byte F Prime downlink attempt completed its software loop in
  about 35 seconds, but the receiver saw only five payload packets. Teensy
  counters proved all five accepted packets crossed RF with zero queue drops.
- Flashed the just-built current satellite firmware to pinned physical ID
  `usb:2100000`; reset counters and current diagnostic fields proved the flash.
  A repeated F Prime transfer still delivered only four accepted payload
  frames, ruling out stale satellite firmware as the sole cause.
- With F Prime stopped, a paced direct Pi UART transfer passed perfectly:
  14/14 channel-1 frames crossed RF and the receiver wrote an exact 350-byte
  output file.
- A direct 104-frame test in unpaced 32-frame bursts reproduced the failure.
  A direct 104-frame test using 5 ms inter-packet pacing and 24-frame runs
  passed completely; the receiver wrote an exact 3,500-byte output and all
  Teensy payload counters agreed.
- Root cause is now isolated: Linux accepts each UART write before its bytes
  leave the wire, and unpaced F Prime rate-group bursts overrun the effective
  Pi-to-Teensy receive path even though every `write()` reports success.
- Implemented conservative manifest-driven hardening: 24 packets/run and a
  5 ms channel-1 UART inter-packet gap. This predicts about 46 seconds for the
  1,100-packet full-resolution file while retaining margin below one minute.

## Current Status

Both gates pass on the live bench. The ground Teensy was recovered and flashed
through pinned ID `usb:100000`; the satellite remains pinned to `usb:2100000`.
The final full-resolution UVC transfer completed in `58.557 s`, byte-identical,
with a responsive mid-transfer command and no retry packets or transport drops.

### 2026-07-09 — Gate 1 PASS and Gate 2 first-principles hardening

- Gate 1 PASS on a real Lepton UVC product. `payload_receiver.py` completed
  1,100/1,100 packets, verified final CRC `0x45b2`, and wrote:
  `/tmp/neutron_hil/c3m_lepton_ack_20260709_105921/Dp_20260709_110252.fdp`.
- The Pi source and ground file are byte-identical, SHA-256:
  `63ed1ee864e81f3857d78ada60dc44a8131575ecb3f435c6d475ecff0b261320`.
- The maintained viewer decoded and opened the downlinked product as full
  `160x120`, `19,200` pixels, temperature range `15.30..22.73 C`, mean
  `18.67 C`; PNG:
  `/tmp/neutron_hil/c3m_lepton_ack_20260709_105921/viewer/Dp_20260709_110252.png`.
- Per-payload-packet ACK was useful diagnostically but took about 161 seconds
  (`11:00:11` request to `11:02:52` saved file), so it is not the final design.
- First-principles audit confirms the proven EPSCOR architecture: unacknowledged
  bulk packets, hardware CRC, indexed application packet CRC, and selective
  bitmap retries after END. Pure 125 kbps RF airtime is only about five seconds;
  the 1 Hz scheduler and Pi UART flow control dominate.
- Replaced a fixed UART gap with encoded-frame wire-time pacing at 115200 8N1
  plus 1 ms. This is channel-global, so a maximum channel-0 frame waits about
  20.7 ms while a normal payload frame waits about 5.4 ms; Linux cannot build
  an unchecked serial backlog between virtual channels.
- Kept payload RF ACK disabled and recurring idempotent headers enabled. At 24
  total messages/run, the 1,100-packet first pass is nominally about 48 seconds,
  leaving selective-retry margin inside the one-minute goal.
- Corrected the RadioHead 125 kbps PHY setup symmetrically: RFM23BP datasheet
  section 3.5.7 requires register `0x58=0xC0` above 100 kbps; RadioHead's preset
  leaves `0x80`, causing non-optimal modulation and increased eye closure.
- Hardened `check_transport_constants.py` so it now verifies generated headers
  against `config/transport_constants.json`, not only against one another.
- Final F Prime native build/tests (6/6), ARMv6/libuvc cross-build, and both
  Teensy builds pass. Satellite final firmware flashed successfully. Ground
  final firmware upload reported success but the board then disappeared from
  USB entirely; software-side reboot/upload recovery is exhausted.

### 2026-07-09 — ground reconnect check

- Operator unplugged/replugged the ground station and requested continuation.
- Live macOS checks still show only satellite serial `11556500` at USB location
  `2-1`; `arduino-cli board list`, `ioreg`, and `system_profiler` do not show the
  ground Teensy or upload ID `usb:100000` in either sketch or bootloader mode.
- No upload was attempted because the pinned physical target is absent and an
  auto-targeted upload could overwrite the satellite board. Next action remains
  one PROGRAM-button press on the connected ground/GDS Teensy, followed by one
  pinned upload to `usb:100000` when that ID appears.

### 2026-07-09 — final UART/RF flow-control root cause and Gate 2 PASS

- RF geometry for this result: ground-station and satellite antennas were about
  30 inches apart on the tabletop, line-of-sight.
- The Pi did not change UART devices during this work. Both this branch and
  `EPSCOR_C3M_REFACTOR` use `/dev/serial0` at `115200 8N1`; on this Pi the
  stable logical device resolves to `/dev/ttyS0`. A ten-second clock sample
  stayed fixed at 400 MHz. No boot configuration was changed.
- The rejected `57600` diagnostic did not cure the failure. It produced many
  CRC errors during a bulk run, so the temporary release was not retained.
- The real regression was missing producer/consumer flow control. With a 1 ms
  Pi frame margin, the application attempted `1992` data sends for a
  `1100`-packet product, while the satellite accepted only `1150` valid payload
  messages. The receiver required `892` retries and took `245.539 s`.
- Exposing the existing satellite parser counters proved CRC drops rose only
  while the Pi launched frames concurrently with RF service. Queue counters
  stayed zero; per-payload RF ACK was not the answer.
- A 25 ms shared margin reduced the tail to `80` retries and `103.852 s`.
  A 40 ms shared margin reduced it to `21` retries and `64.491 s`; the remaining
  one-tick hole coincided with a three-segment channel-0 ping response.
- Final pacing is channel-aware and manifest-driven:
  - Pi/satellite UART remains `115200 8N1`.
  - payload/base inter-frame margin: `37,000 us`.
  - additional channel-0 drain margin: `40,000 us`.
  - payload and retry messages per 1 Hz run: `22`.
  - payload RF inter-packet gap: `15 ms`.
  - ground-to-satellite CCSDS ACKs remain enabled; satellite telemetry and
    payload bulk remain unacknowledged.
- Final Pi release:
  `/home/pi/artemis/releases/c3m-hil-uartflow37-channel`, SHA-256
  `7b2959e0e1957a6fa14b9a9ed2c2873234f0dccd4edf997e06a50189e9e47372`.
- Final fresh UVC source:
  `/home/pi/artemis/current/DpCat/Dp_268595200_1783638724_00314549.fdp`.
- Final ground file:
  `/tmp/neutron_hil/c3m_uartflow40_20260709_130422/Dp_20260709_131330.fdp`.
- Command-to-file time: `58.557 s`.
- Source and ground SHA-256:
  `87b61b387647a4e732918b93a071fe51bf64b9b1a55ede6ff30e99289465ac26`.
- Final GDS status: `sent=1100 total=1100 error=0`; receiver reached
  `1100/1100` without emitting a retry request.
- Mid-transfer `missionApp.PING 37002` returned in the same second.
- Satellite final counters remained `crc_drops=0`, `framing_drops=0`,
  `uart_timeouts=0`, `rf_tx_drops=0`, `up_q_drops=0`, and `down_q_drops=0`.
- The maintained viewer decoded `160x120`, `19,200` pixels, `15.11..23.40 C`,
  mean `18.84 C`; PNG:
  `/tmp/neutron_hil/c3m_uartflow40_20260709_130422/viewer/Dp_20260709_131330.png`.
