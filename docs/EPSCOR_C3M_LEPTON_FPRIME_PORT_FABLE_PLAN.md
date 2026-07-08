# EPSCoR C3M Lepton → Neutron 2 Port + Payload Downlink Speed-Up

## Context

**Intent:** One reusable Artemis CubeSat F´ bus supporting multiple missions by swapping payload-specific pieces. First concrete work: transplant the working EPSCoR C3M FLIR Lepton payload (branch `EPSCOR_C3M_REFACTOR`, pre-rename architecture, ~29 files vs merge base) onto the current Neutron 2 architecture (`epscorc3m/demo` == `neutron2-develop` @ 33aef0a), and fix the too-slow full-image downlink. FSR v2.0 deliverables are due July 31 on `neutron2-develop` (freeze-first: tag `demo-fsr-rc1` before invasive change), so all work lands on `epscorc3m/demo` and bus-level improvements merge back after bench proof.

Quick note on sequencing whenever you're ready to start: Phase 0 (baseline measurement) needs the HIL bench, but Phases 1–2 (Teensy hardening cherry-picks + the pacing fix, which is the ~30× win) are laptop-verifiable via validate_local.sh — so I can start on those anytime, even without hardware plugged in. Just say the word.

**Key investigation findings (verified with file:line):**
- **The downlink bottleneck is NOT primarily the RF ACK.** `PayloadDownlinkApp.cpp:13` `PACKETS_PER_RUN=1` on the 1 Hz rateGroup1 ⇒ 1 pkt/s ⇒ 1,100 packets (38,480 B `.fdp`) ≈ **18+ min** with the RF link ~99% idle. RF airtime floor at 125 kbps ≈ 4–8 s. Fix pacing first.
- The per-segment stop-and-wait RF ACK (`relay_uart_rf.cpp:413` `sendRfPacketWithAck`, 80 ms timeout × 5 tries) is **global for both channels**; it ~halves clean-link throughput and multiplies wall-clock under loss.
- **Channel-1 UNACK is already safe by design:** full selective-repeat app-level ARQ exists end-to-end (`payload_receiver.py` bitmap NAKs ↔ `PayloadDownlinkApp.handleRetryRequest`) — the frozen decision in `docs/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md`. Channel 0 (CCSDS) has no app ARQ ⇒ keeps RF ACK.
- The N2 driver seam is exact (`Components/Types/Types.fpp`): `requestIn: Components.PayloadCaptureRequest`, `statusOut: Components.ScienceProductDescriptor{productId, productBytes, sourceKind, sourcePath, sourceCrc}`, `pingIn/pingOut`, `timeCaller`. Swap = new instance + 2 `ManagerDriverBindings` lines. Descriptor passes through PayloadManager→ScienceApp→StorageManager→CommsApp→PayloadDownlinkApp **unmodified**; `prepareSource()` honors a non-empty `sourcePath` (`PayloadDownlinkApp.cpp:309-324`) and compares CRC as U16 truncation (`:279`).
- **C3M contract violation to fix in the port:** `PayloadAdapter_Lepton` emits `statusOut` with empty `sourcePath`/crc 0 and compensated with a payload-specific `./DpCat` newest-`.fdp` scan hacked into the downlink app. Fix: `Svc.DpWriter` has `dpWrittenOut: DpWritten(fileName, priority, size)` carrying the real written path — but it's a single port already consumed by `dpCat.addToCat` (`lib/fprime/Svc/Subtopologies/DataProducts/DataProducts.fpp:75`), so the subtopology must be **vendored** with a small fan-out router.
- **DP config landmine:** `dpBufferStoreSize=10000` in the fprime submodule must be ≥48,000 for the 38,480 B product; C3M's fix was an uncommitted submodule edit (reverted by every `git submodule update`). Vendoring the subtopology makes this project-owned.
- Satellite Teensy uplink queue depth = 32, Pi-UART RX ring = 4096 B (`satellite_teensy.ino:22-26`) ⇒ safe burst = **32 packets/tick**.
- Constants are single-sourced: `config/transport_constants.json` → `tools/generate_transport_constants.py` → `LinkCfg.hpp` + both Teensy `link_protocol.hpp`; `check_transport_constants.py` + `validate_local.sh` drift-guard them.
- N2's `PayloadDownlinkApp` lacks C3M's `HEADER_RETRANSMIT_COUNT=3` improvement — port it. C3M's Teensy boot fixes (radio-before-Pi-power, bounded chip-ready probe) are bus-generic — cherry-pick early.
- `fprime-dp` ships with pip-installable `fprime-gds` ⇒ the `.fdp` viewer works on **native Windows** (`pip install fprime-gds numpy matplotlib`), no WSL for ground viewing.

**Locked decisions (user):** keep F´ Data Products (`.fdp`) output; component named `PayloadDriver_Lepton`; work on `epscorc3m/demo` first, merge back after proof; transport first; generic logic stays in `PayloadDownlinkApp`; Pi-as-brain / Teensy-as-relay preserved.

## Principles applied (why the plan has this shape)

- **End-to-end argument (Saltzer/Reed/Clark):** reliability lives at the application endpoints (Pi ↔ ground PC selective-repeat ARQ, whole-file CRC), not duplicated per hop — justifies removing the redundant per-segment RF ACK on channel 1 while keeping it on channel 0, which has no endpoint ARQ.
- **Measure before optimizing:** the assumed culprit (RF ACK) was second-order; the 1 Hz feed was first-order. Phase 0 locks a baseline + measurement protocol reused at every phase.
- **Software product line / variation point:** the bus is the platform; missions differ only at the documented driver seam + 2 topology wires. Both missions' components exist on both branches; the 2-line wiring diff *is* the mission identity.
- **Policy/mechanism separation + single source of truth:** ACK mechanics stay in Teensy firmware; per-channel ACK *policy* lives in `transport_constants.json` and is generated + drift-checked, making sender/receiver disagreement unrepresentable.
- **Honest interfaces:** the driver fills its `ScienceProductDescriptor` truthfully (real path + CRC) so no payload knowledge leaks into the generic downlink layer.
- **Test pyramid + demo-first increments:** generator/receiver unit tests → component UTs (stub camera) → `validate_local.sh` local demo → HIL timed acceptance; each phase independently landable and demoable.

## Phases

### Phase 0 — Measure (no code changes)
Bench-characterize the current link before trusting anything. New `docs/TRANSPORT_CHARACTERIZATION_2026-07.md` recording: full-image wall clock (expect ~1,100 s), RF segment drain rate with ACK, `rf_retries`/`rf_ack_timeouts`/`rf_msg_id_gaps`/queue drops (Teensy 1 Hz debug counter streams; satellite `Serial`, ground `SerialUSB1`), receiver retry rounds, RSSI. Drive via `payloadDownlinkApp.START_PAYLOAD_DOWNLINK` + `tools/payload_receiver.py --debug`.

### Phase 1 — Bus-generic hardening cherry-picks (from C3M)
- `satellite_teensy.ino`: radio `begin()` **before** Pi power-enable (pin 36) — manual re-apply around the new `wdt_guard` arming lines (not a clean cherry-pick).
- `artemis_rf23bp.hpp`: bounded chip-ready probe (degrade to UART-only bridge instead of hanging) — apply **byte-identically to both** Teensy trees (shared-by-copy drift check in `validate_local.sh`).
- `rf23_driver.cpp` log-sink line; `cross/pi-zero-w/...armv6hf.cmake` crt fix (clean pick); `docker_cross_compile_pi_zero_w.sh --copy-only`; `run_gds_uart.sh` cross-dict default.
- Reconcile systemd units: keep only `deploy/pi/artemis-fprime.service`; set `WorkingDirectory` so `./DpCat` lands in a known dir.
- Verify: `validate_local.sh`, arduino-cli builds, HIL cold-boot smoke (`demo_rf_mvp_smoke.sh`), radio-absent boot degrades gracefully.

### Phase 2 — Pacing fix (~30× speed-up, zero protocol change)
- `config/transport_constants.json`: add `payload.packets_per_run: 32`, `retry_packets_per_run: 32` (= uplink queue depth); render into `LinkCfg.hpp` via `generate_transport_constants.py`.
- `PayloadDownlinkApp.cpp/.hpp`: use LinkCfg constants; port C3M's `HEADER_RETRANSMIT_COUNT=3` (`m_headerSends`). Do **not** port C3M's `./DpCat` auto-detect (superseded by Phase 5's honest descriptor). Extend UTs (burst count, header resends).
- Keep the 1 Hz tick — a faster rate group would require an `RateGroupDriverRateGroupPorts` AcConstants override; disproportionate risk pre-freeze. Bench-sweep N ∈ {8,16,32}, require `up_q_drops=0`.
- Target with ACKs still on: **full-res ≤ 120 s** (expect 35–60 s).

### Phase 3 — Per-channel RF ACK policy (ch0 ACKed, ch1 UNACK)
- `transport_constants.json`: `rf.ack_channels: {ccsds: true, payload: false}` → generator renders `ackRequiredForChannel()` into **both** Teensy `link_protocol.hpp` + parity booleans in `LinkCfg.hpp`; extend `check_transport_constants.py` to assert cross-tree agreement (the safety interlock against retry storms).
- Guard three sites per Teensy (files intentionally diverge — parallel edits, not copies): skip `waitForAck` in `sendRfPacketWithAck` (sat `relay_uart_rf.cpp:413/:440`; gds `:391/:418`) and skip `sendAck()` at receive/dup sites (sat `:602/:570`; gds `:580/:548`) when channel doesn't require ACK. Keep `RF_INTER_SEGMENT_GAP_MS=8`.
- Note: `local_emulation_loop.py` never modeled ACKs — coverage comes from generator parity tests + HIL A/B.
- Verify: ch1 drain >150 seg/s, `rf_ack_timeouts` flat during payload transfer, ch0 commanding responsive mid-transfer (GDS NO-OPs), ch0 `rf_ack_rx` still increments.

### Phase 4 — Vendored DataProducts subtopology + DpWritten fan-out
- New `Components/DpWrittenRouter/` (passive, ~40 lines): `dpWrittenIn: Svc.DpWritten` → `catOut` + `notifyOut` (bus-generic asset for any future DP payload).
- Vendor `lib/fprime/Svc/Subtopologies/DataProducts/` → `ArtemisRpiTeensy_N2/Subtopologies/DataProducts/` renamed `ArtemisDataProducts`/`ArtemisDataProductsConfig` (avoids duplicate FPP symbols; lib config module registers unconditionally). Inside: `dpWriter.dpWrittenOut -> dpWrittenRouter.dpWrittenIn`, `router.catOut -> dpCat.addToCat`, export `notifyOut`; set `dpBufferStoreSize=49152`, `dpBufferStoreCount=10` (480 KB — fine on Pi Zero W). Keep BASE_ID 0x04000000 and `./DpCat` paths.
- Update `project.cmake`, `Top/topology.fpp:18` import + 3 schedIn wires (`:159-161`), `TopologyDefs.hpp` PingEntries include, doc references.
- (Rejected: `CONFIGURATION_OVERRIDES`-only fix — works for the constant but can't add the dpWritten wire; one vendoring beats two half-mechanisms.)
- Verify: build; confirm 49152 won in build cache; `validate_local.sh` (NeutronSim untouched — no DPs).

### Phase 5 — `PayloadDriver_Lepton` component
New `Components/PayloadDriver_Lepton/` ported from C3M `PayloadAdapter_Lepton/` (+`LeptonCamera.{hpp,cpp}`, `testLeptonCamera/` harness, UTs):
- Seam ports (`requestIn`/`statusOut`/`pingIn`/`pingOut`/`timeCaller`) + DP ports + **new `dpWrittenIn: Svc.DpWritten`**; active component, 256 KB stack (libuvc thread, C3M-proven).
- **Honest descriptor:** capture → `dpSend` → hold pending state → on `dpWrittenIn` compute CRC16-CCITT over the written file (mirror `PayloadDriver_NeutronSim.cpp` `computeFileCrc16`) → `statusOut(id, size, REAL_PAYLOAD, fileName, crc)`. Failure ⇒ immediate `statusOut(0,0,UNKNOWN,"",0)`; stale-pending ⇒ `DpWrittenMissing` event.
- **Restore the stub/libuvc `#ifdef` split** (currently commented out in `LeptonCamera.cpp:138,370-396`) so native/UT builds get a deterministic ramp stub; keep libuvc Y16-GUID workaround + FFC rejection for the Linux build; re-enable the Linux gate in `CMakeLists.txt`.
- **Preview mode (demo pacing):** `enum LeptonCaptureMode {FULL_RES, PREVIEW}`; 2×2 box-average → 80×60 `ThermalPreviewRecord` (~9,680 B ⇒ 277 pkts ⇒ ~9 s); `CAPTURE_IMAGE(mode)` command + `CAPTURE_MODE` param for the scheduled path.
- UTs with stub camera: DP interactions, dpWrittenIn→statusOut path/CRC, preview averaging, failure paths.
- Verify: `fprime-util check --all` native (proves stub build), `testLeptonCamera` on Pi with hardware, ARMv6 cross-compile via docker script.

### Phase 6 — Topology integration + two-missions story
- `Top/instances.fpp`: `payloadDriverLepton` @ base id `0x10027000` (verified free); `topology.fpp`: 2-line `ManagerDriverBindings` swap (NeutronSim instance stays defined for revert) + DP wires + `dpWrittenNotifyOut → payloadDriverLepton.dpWrittenIn`; PingEntries in `TopologyDefs.hpp`.
- Teach `tools/run_neutron2_local_demo.sh` the Lepton-stub story (detect wired driver from dictionary; assert `.fdp` flow) so `validate_local.sh` stays a real gate on both branches.
- **Two missions, one bus:** one deployment, per-mission wiring swap; all code identical on both branches except the 2 wiring lines + demo default. Second deployment variant deferred. Document the swap recipe in `docs/SYSTEM_ARCHITECTURE.md`.

### Phase 7 — Ground tooling (Windows-friendly)
- Port receiver directory/continuous mode into `tools/payload_receiver.py` (applies cleanly; stays pyserial-only ⇒ native Windows) + directory-mode unit tests.
- Port viewer to `ground-station/lepton-dp-viewer/dp_lepton_viewer.py`, extended to accept 19,200-px **and** 4,800-px arrays; README runbook with N2 command names + Windows section (`pip install fprime-gds numpy matplotlib`).
- Port July 7 sample `.fdp`/`.png`/`.csv` + README into `TEST-DATA-DOWNLINK/`; drop the 77k-line decoded `.json` (regenerable).

### Phase 8 — HIL acceptance gate
≥5 full-res + ≥5 preview transfers, counters before/after. Targets: **full-res ≤ 60 s** (expect 35–45 s), **preview ≤ 15 s**, 5/5 CRC-complete + plausible thermal decode, zero queue drops, ch0 responsive mid-transfer, retry rounds ≤ 2, `validate_local.sh` PASS on both mission stories, radio-absent cold boot degrades gracefully.

### Phase 9 — Merge back to `neutron2-develop`
Confirm `demo-fsr-rc1` tag exists first. Merge in phase order (1–3 bus, 4 inert-for-NeutronSim, 5+7 additive/unwired), each gated on `validate_local.sh` on `neutron2-develop`. **Exclude** the Phase 6 wiring-swap lines (mission identity). Known conflict: `deploy/pi/artemis-fprime.service` — keep the Phase 1 reconciled version.

## Critical files
- `ArtemisRpiTeensy_N2/Components/PayloadDownlinkApp/PayloadDownlinkApp.{cpp,hpp}` (pacing, header retransmit)
- `config/transport_constants.json` + `tools/generate_transport_constants.py` + `tools/check_transport_constants.py` (pacing + ACK policy source of truth)
- `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.cpp` + `GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.cpp` (ACK guards)
- `ArtemisRpiTeensy_N2/Components/PayloadDriver_Lepton/` (new, from C3M `Components/PayloadAdapter_Lepton/`)
- `ArtemisRpiTeensy_N2/Subtopologies/DataProducts/` (new, vendored) + `Components/DpWrittenRouter/` (new)
- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/{instances.fpp,topology.fpp,ArtemisRpiTeensyDeploymentTopologyDefs.hpp}`
- `ArtemisRpiTeensy_N2/tools/{payload_receiver.py,run_neutron2_local_demo.sh,validate_local.sh}`

## Verification
- Per phase: `tools/validate_local.sh` (drift + constants + pytest + F´ build + UTs + local demo); `python3 tools/generate_transport_constants.py --check`; arduino-cli builds for both Teensys; ARMv6 cross-compile via `tools/docker_cross_compile_pi_zero_w.sh`.
- Transport phases: HIL A/B timed transfers against Phase 0 baseline using Teensy debug counters + `payload_receiver.py` exit codes (0 = complete + CRC match).
- End-to-end: full operator sequence (ENABLE → SCHEDULE_COLLECTION → REQUEST_SCIENCE_DOWNLINK → receiver `--output-dir` → viewer decode) on bench; Phase 8 acceptance table is the gate for merge-back.
