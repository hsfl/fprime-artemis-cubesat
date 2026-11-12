# Artemis local twin

Owner: Artemis flight software. Started 2026-11-11 (HST).

Current handoff status: producer IDs repaired; four fresh Yamcs-downloaded FDPs
passed the unchanged existing C3M app with its F′ 4.2.1 environment and the
producing Pi dictionary. See the final compatibility milestone and walkthrough.

## Objective and boundaries

Run the real Teensy flight-controller F′ application with Zephyr `native_sim`,
the real Linux Pi payload application with recorded Lepton frames, simulated
peripherals/radio, and Yamcs on this MacBook. The flight controller owns mission
admission, scheduling, sensors, power, the full product cache, and radio access.
Pi owns camera acquisition, processing, persistent products, and staging bytes.
Ground science must traverse the simulated link; shared files cannot substitute
for downlink. No physical hardware, RF, flashing, deployment, CI/CD, or FreeRTOS
work is part of this implementation.

The required path is BASE → fresh SOH/readiness → scheduled capture → matching
Pi DpWriter completion → full cache commit → downlink/repair → verified Yamcs
archive → exact file download for the existing mission ops software. Sender
completion and verified ground receipt differ. Yamcs handles commanding,
telemetry and file delivery; the mission ops software owns image/temperature
analysis. This twin does not add another analysis UI.

## Branch and preserved state

Implementation branch: `codex/artemis-local-twin`, based on `zephyr-dev`
`d35be8174be8e5300f939dd5667c5b4c19060f7c`. This uses the existing checkout,
not a worktree. The original dirty files were backed up under local Git
metadata and retained in stash `742fef4ade801093fa6da5195797e419fd97f4b3`;
they were successfully reapplied without conflicts. Unrelated pre-existing
changes must not be committed as twin changes.

## Dependency and memory targets

Target F′ v4.4.2, fprime-zephyr `f97eaaf28e22fa2db3ec1909351365c1b90d1538`,
and Zephyr v4.3.0. Treat compatibility as a build/runtime gate, not an existing
test result. Both split deployments use the same F′ version.

The full product cache is 196,608 bytes. One aligned internal OCRAM buffer must
serve staging, transmission, and repair without a second full-size allocation.
The selected partition is 128 KiB ITCM, 128 KiB DTCM, 256 KiB OCRAM, plus
dedicated 512 KiB OCRAM2 for the main application. Configure bank partitioning,
MPU access, NOLOAD state initialization, region bounds, and malloc arena bounds.

Fresh pre-upgrade RFM23 build: 243,088 static RAM bytes (237.39 KiB), 46.37%
of main OCRAM2 and 23.18% of physical RAM. Adding the cache projects 439,696
bytes (41.93% physical RAM), before runtime heap allocations and new components.
This is a baseline, not acceptance of the complete upgraded application.

## Milestones and evidence

| Milestone | Acceptance | State |
|---|---|---|
| M0 Preserve and branch | Dedicated branch, existing work restored, scope recorded | Complete |
| M1 Platform/dependencies | Pinned dependencies, FC native_sim + target builds, Pi build, explicit cache memory layout | Complete |
| M2 Split science | Correlated capture/commit/staging, full-cache validation and recovery tests | Complete |
| M3 Local link/devices | Production radio channel 0/1 logic, PDU/sensor models, isolated processes and repeatable launcher | Complete |
| M4 Yamcs | FC dictionary/XTCE, commanding/tlm/events, verified ground science archive and raw-file handoff | Complete |
| M5 Acceptance/handoff | Three cycles, Pi-disconnected repair, faults, target budgets, clean-start runbook | Complete |

Each milestone must append its implemented behavior, commands/checks, observed
results, and remaining limitations here before progressing. Component-specific
contracts and tests are documented beside their owning source.

### M0 — branch established

- Preserved tracked/untracked local work and submodule state.
- Created the dedicated branch directly from the upstream Zephyr baseline.
- Reapplied local work successfully; no reset, cleanup, or remote push.
- Reuse existing `RadioComms`; extend its channel-0 link for science channel 1.

### M1.1 — dependency and Docker foundation

- F′ is now pinned to released v4.4.2 (`f22ce8f`); fprime-zephyr is pinned
  to `f97eaaf`, with Zephyr retained at v4.3.0. Legacy Pi framework is untouched.
- ARM64 Debian/Python 3.11 Docker image built successfully; dependency checks
  passed using the framework's pinned requirements.
- Added a native_sim profile, UART aliases, packet-radio peripheral substitute,
  ADC/I2C models, and virtual Pi power observation beneath production components.
- Added an explicit target FlexRAM partition and OCRAM MPU memory attributes.
  Full cache storage and linker assertions are implemented but target-link and
  native runtime validation are still pending.
- Native build exposed the upstream ARM-only `zephyr_final` assumption; the
  project now connects that target to native_sim's final runner. Build testing
  continues; this is not yet a complete M1 pass.

### M2.1 — portable full-cache and channel-1 engines

- Implemented the single-copy full cache with exact size/bounds, session and
  generation validation, idempotent chunks, separate CRC commit, abort/reset,
  retained transmission and repair state.
- Extended the existing radio link for independent A5/A6 channels and bounded
  science servicing in its one hardware-owning worker.
- Parent independently ran `cmake --build /tmp/artemis-twin-cache-verify` and
  `ctest --test-dir /tmp/artemis-twin-cache-verify --output-on-failure`: **2/2
  pass**, including loss/repair through the unchanged Python ground receiver.
- Worker checks additionally passed both engines' ASan/UBSan tests and a
  Cortex-M7 cache-engine compile. See the owning Science README and radio
  SCIENCE_INTEGRATION document for exact contracts and commands.
- FC coordinator and Pi service wiring remain integration work; these checks
  do not yet establish the complete Lepton/Yamcs scenario.

## Final acceptance checklist

- Three distinct scheduled Lepton products, downloadable unchanged from Yamcs.
- Exact source/ground/archive bytes, CRC, and SHA-256 agreement.
- Stage fully, disconnect Pi, then downlink and repair lost packets from FC RAM.
- Oversize, gap, duplicate, bad CRC, stale session, abort, and busy replacement checks.
- Pi/capture/write/link/ground/Yamcs failures followed by a clean successful product.
- SOH, sensors, and commands remain responsive during staging and repair.
- Fresh USB/RFM23 Teensy and Pi builds; complete memory budget, bounded allocations.
- Runtime hardware heap/stack/timing evidence remains a later bench gate.

## Review and merge

Keep dependency, platform, science, radio, and ground changes reviewable.
Integrate into `zephyr-dev` before the separately reviewed merge into `main`.
The user subsequently authorized organizing commits and publishing the feature
branch. Merge into `zephyr-dev` or `main` remains a separate action.

### M2.2 — real F′ science and direct PDU components

- Added the flight coordinator and Pi payload link service, connected through
  GenericHub index 6. Capture completion waits for the matching DpWriter result;
  200-byte acknowledged chunks stage the entire file and require a final CRC
  commit before the RAM cache becomes valid. The radio reads that same buffer.
- The coordinator owns admission, schedule/capture/staging deadlines, explicit
  restaging and retained-cache downlink. It ignores late unrelated messages,
  retires old repair leases, and bounds consecutive local transmission failures.
- The Pi application compiled and linked on Linux ARM64. Recorded-camera startup
  stayed alive for five seconds and shut down cleanly; missing fixture/UART
  configuration was rejected visibly.
- Registered F′ tests passed: **11/11 coordinator**, **3/3 Pi service**; the
  recorded capture-window helper passed **1/1**. Tests cover a complete 196,608-byte
  cache, CRC failures, admission/freshness, cancellation, duplicate commit during
  transmission, old errors, timeouts, failed staging, restage metadata and bounded
  send failure. Logs: `/tmp/artemis-twin-component-tests.log` and component READMEs.
- Added the direct UART-v2 EpsManager using the owning PDU protocol header.
  Portable client plus actual production PDU C tests passed **2/2**, including
  corruption/recovery; ASan/UBSan passed **2/2**. Registered F′ component tests
  passed **3/3**, including observed state versus requested state and complete
  power-cycle restoration. These are host tests, not physical-PDU evidence.
- Independent review is checking recovery and duplicate-message behavior before
  full end-to-end acceptance. The complete FC/native and Teensy links remain M1
  gates; the component passes alone do not establish the complete twin.

### M1.2 — linked native FC and physical Teensy RFM23 target

- The complete FC `native_sim/native/64` deployment built and linked inside the
  pinned ARM64 Linux container. Normalized binary/dictionary artifacts were
  produced successfully by `python tools/twin/build.py fc`.
- Fresh Teensy41/RFM23 target configuration and link passed outside the source
  tree using Zephyr SDK 0.17.4 and the new framework tool environment. No board
  was flashed. USB target configuration/link is being checked separately.
- The ELF places exactly one `artemis_product_cache` at `0x20280000`, size
  196,608 bytes, in a NOLOAD OCRAM section. Linker assertions enforce its address,
  exact size, 256 KiB bound and the main malloc arena bound.
- Main RAM high-water static address `_end=0x20241890` uses **268,432 / 524,288
  bytes (51.20%)**. The separate OCRAM bank uses **196,608 / 262,144 bytes
  (75.00%)**. Combined mapped static reservations are **465,040 bytes (44.35%
  of 1 MiB physical RAM)**. The main address space remaining after `_end` is
  255,856 bytes; this is a prospective malloc arena, not measured free heap.
  ITCM/DTCM reserves and aliased OCRAM2 entries are not added twice.
- Added project-owned compatibility for the removed F′ assert-hook selection,
  native glibc time header, new CCSDS aggregator size, and selected hardware/native
  entropy drivers for session IDs. Dependencies remain unmodified checkouts.
- Runtime heap/stack maxima and physical timing remain a later hardware gate.

### M1.3 — USB target and inspectable memory evidence

- A separate fresh Teensy41/USB build and link also passed with the full cache.
  Its main static high-water reservation is **272,720 bytes (52.02% of main
  RAM)**; combined with the cache this is **469,328 bytes (44.76% physical)**.
- `fprime-artemis-core/Platform/report_memory.py <zephyr.elf>` checks the actual
  ELF cache symbol, NOLOAD section, size and address, and emits the main and
  cache bank percentages without alias double-counting. Results and ELF hashes
  are retained in `Platform/evidence/teensy-{rfm23,usb}-memory.json`.
- Native FC boot observed the real mission application, ON IMU, nominal thermal
  readings, an eight-satellite GPS fix, virtual power/UARTs and a successful
  response from the actual PDU C application. This is software-device evidence.
- Target runtime allocation/stack peaks remain unmeasured; a successful link
  and software-model boot do not qualify the populated board.

### M3.1 / M4.1 — device boot and real Yamcs startup

- The isolated FC/devices stack booted the native kernel and production flight
  components with connected virtual UARTs, real radio link logic and actual PDU
  C host firmware. The ground path remains independent of Pi product storage.
- The parent reran `python3 tools/twin/twin.py test`: **14/14 pass**. These checks
  include packet wire contracts, actual PDU parser interoperability, archive
  admission/readback and artifact provenance checks.
- A real dependency probe generated XTCE from the FC dictionary and started
  fprime-yamcs 0.2.4; the Yamcs instance and realtime processor reached RUNNING.
  No fabricated telemetry or science products were supplied to that probe.
- Integrated commanding, three products, exact downloaded file equality and Pi-off
  packet repair remain the next acceptance gates.

## Fidelity and remaining physical gates

| Area | Executed locally | Evidence still needed on hardware |
|---|---|---|
| FC | Real F′ components, Zephyr kernel/tasks, bus-driver APIs on Linux ARM64 native_sim | Cortex-M7 execution, ISR/MPU/cache behavior, peak heap/stack and timing |
| Pi | Real Linux F′ application, recorded Lepton backend, DpWriter/catalog and persistent files | Actual Pi CPU/ABI build and camera/bus capture |
| Sensors | Existing GPS, LSM6DSOX and seven TMP36 paths against software inputs | Exact-board pin order/scaling and physical sensor faults |
| EPS | Real PDU application/parser C with mocked GPIO/UART/ticks | Electrical rails, load behavior, watchdog and reset timing |
| Radio | Existing channel 0/1 framing, fragmentation, ACK/retry and ground receiver with paced packet I/O | Physical RF/IRQ performance and throughput |
| Ground | Real Yamcs, FC XTCE, received products and exact object upload/GET | Bench link and mission-operations qualification |

INA219 current sensing and LIS3MDL acquisition remain existing sensor-inventory
follow-up work; the local twin does not make their unimplemented paths complete.
Docker Desktop uses one Linux VM with separate containers, not a CPU-accurate
Teensy or Raspberry Pi VM. Target-link memory evidence is kept separately from
native simulation results. See the existing sensor audit for exact-board gaps.

### M2.3 — persistent product recovery and replay safety

- Pi now stores bounded numeric-ID metadata records for committed catalog-owned
  files and a persistent ID high-water mark. Older products can be restaged after
  a newer or failed capture and after Pi restart; IDs cannot silently collide
  when the deployment restarts. Requested files remain confined to the configured
  catalog root and must pass regular-file, full-size and CRC checks.
- A repeated completed STAGE_REQUEST replays CACHE_READY; stale stage keys are
  rejected. It does not reopen the file and leave the Pi busy.
- Updated real generated F′ service tests passed **6/6** with ASan/UBSan; portable
  helpers passed **1/1**. A second component instance exercises actual catalog
  reload. Active ABORT now requests driver cancellation and keeps correlation
  until a terminal callback; driver/manager cancellation wiring is the next gate.

### M2.4 — active capture cancellation and clean reuse

- Connected service → asynchronous manager cancel → asynchronous Lepton cancel.
  Cancelling a running collection interval reports a terminal failure descriptor
  and fresh READY immediately on the driver's serialized task. If DpWriter owns
  a pending write, the service holds transaction ownership until acknowledgment
  or a ten-tick timeout and ignores its cancelled result.
- Unique paths within the driver lifetime and exact pending-path matching prevent
  a late timed-out/cancelled write callback from completing a fresh collection.
- **4/4 actual generated F′ Lepton driver tests passed**, exit 0, with address,
  undefined-behavior and leak sanitizers. They cover a 3,600-second interval
  cancelled into a successful fresh serialization, pending-write draining,
  timeout/late callbacks and completion racing a queued cancellation.
- The existing camera read itself retains its five-second timeout. Physical
  camera behavior and sudden-power-loss file durability remain bench gates.
- Independent re-review closed the persistent-product and duplicate-stage
  findings. Both Teensy target builds were repeated after the final production
  source freeze; both links and memory audits passed. Native application artifacts
  are being normalized for integration.

### M2.5 — explicit cancellation completion barrier

- Final review found that a normal product callback could race with queued
  cancellation and release ownership too early. A dedicated local Fw.Signal
  barrier now runs driver → manager → service; ordinary completion/failure
  descriptors cannot acknowledge cancellation. No wire protocol change was
  needed. The service discards pending/late product descriptors while cancelling
  and retains ownership until cancelDone after the cancellation was dispatched.
- Updated service tests pass **7/7** under sanitizers, including both pending
  positive and failed results, further old callbacks, premature Done, BUSY before
  the actual barrier and successful fresh capture afterward. Driver tests remain
  **4/4**, with address/undefined/leak sanitizer checks and explicit ordering.
- The first diagnostic end-to-end product was received over 1,100 science packets,
  38,486 bytes, archived in Yamcs and independently downloaded with matching
  file bytes. The final multi-cycle run follows rebuilding the cancellation
  barrier and uses file delivery as its ground acceptance boundary.
- Native FC now initializes its software REALTIME clock from the supervisor's
  launch UTC epoch, avoiding 1970 timestamps in Yamcs archive windows. Monotonic
  scheduling and the physical target are unaffected.

### Ground handoff boundary — user steering, 2026-11-11 HST

Yamcs delivers the raw science file to the existing mission ops software.
Removed the added custom viewer, automatic FDP decoding, PNG generation, pixel
exports and temperature analysis from the twin runtime and acceptance scope.
The operator downloads the original `.fdp` object from Yamcs storage, with its
verified byte count/CRC/SHA receipt. The existing mission ops software performs
image and temperature analysis. The producing Pi dictionary is retained with
the run artifacts for that software when needed. Existing mission software and
legacy viewers remain independent of the twin runtime.

### M3.2 / M4.2 — raw file delivery through Yamcs

- Fresh raw-only run `20261112T052132Z-f1628f` starts the four separate Compose
  roles. Only Yamcs port 8090 and local device-fault port 8092 are exposed; the
  custom viewer directory and port 8091 are removed.
- The parent opened Yamcs Storage in a browser, selected `artemis-science` and
  downloaded the current-run `.fdp` object using its normal file link. The UI
  had no console errors. No image analysis is part of this handoff.
- Parent independently downloaded product 7 again via its exact Yamcs REST path:
  **38,486 bytes**, CRC16 and SHA-256 equal the receipt, and bytes equal the
  retained ground GET file. The receipt contains no image/temperature fields.
- The producing Pi dictionary and hash remain available for the existing mission
  ops software. The ground service cannot mount Pi product storage.
- Independent review confirmed opaque file delivery, exact readback and the RF
  boundary; two acceptance-script cleanup/gating edges are being corrected
  before final multi-cycle/fault acceptance.

### M5.1 — three real raw-file cycles

- Final raw-only acceptance passed **3/3 scheduled products (7, 8, 9)** in run
  `20261112T052132Z-f1628f`. Each file is 38,486 bytes; all commands used Yamcs
  and received actual F′ dispatcher completion events.
- Each product committed on Pi, staged fully in FC RAM, crossed virtual RF, and
  was independently downloaded from Yamcs. Parent independently repeated every
  GET and checked byte count, CRC16 and SHA against its receipt, retained ground
  file and the post-delivery Pi source-hash oracle: **all pass**.
- `acceptance.json` retains the command/event, cache, product and receipt evidence.
  Pi-off packet repair and failure/recovery scenarios remain the final gates.

### M5.2 — Pi off and packet repair from retained FC RAM

- Pi-off acceptance passed for product **10**, **38,486 bytes**. FC first staged
  the whole product, then MissionApp STANDBY stopped the Pi payload process.
  Injected loss of DATA packet 0 required **one repair round** from the FC cache.
- Parent independently repeated the raw Yamcs GET and checked byte count, CRC16
  and SHA against the receipt and Pi source-hash oracle; **all pass**. The
  evidence records virtual power OFF before downlink.
- Native telemetry remained live: IMU ON, GPS READY/eight satellites, thermal
  NOMINAL, PDU RESPONDING and zero 100 Hz rate-group cycle slips. Physical
  heap/stack/timing qualification remains separate.
- Evidence: `acceptance-pi-off-repair.json` in the same final run.

### M5.3 — final fault recovery, evidence and handoff

- All **five live fault cases passed**, exit 0: invalid U32 rejection, active
  capture cancellation followed by a new file, interrupted UART staging followed
  by restart/recovery, RF loss with retained-cache resend, and an actual paused
  Yamcs process with a pending science outbox followed by resume and file GET.
- Final run contains **eight verified raw files**: products 7/8/9 (normal cycles),
  10 (Pi-off packet repair), 11 (cancel recovery), 13 (stage recovery), 14 (RF
  recovery), 15 (Yamcs recovery). Each is 38,486 bytes. The parent independently
  downloaded all eight and verified exact retained-ground bytes, CRC16 and SHA.
  Latest Pi source SHA also matches the final outage-recovery file.
- Final checks: **15/15 runtime tests**, Python syntax, Compose configuration and
  diff checks pass. Registered F′ checks remain 11 coordinator, 7 service, 4
  Lepton driver, 3 EPS manager, plus the documented portable engine/helper tests.
  Protocol/write-result failures use component tests; the five named scenarios
  above were exercised against the running split applications.
- [Compact acceptance evidence](evidence/ARTEMIS_LOCAL_TWIN_ACCEPTANCE_2026-11-11.json)
  retains pins, matching build/dictionary hashes, exact products, target memory
  audits and hashes of the complete run reports. Full reports/logs remain in
  `.twin-runs/20261112T052132Z-f1628f/`. An initial harness expectation mismatch
  is retained as a separate attempt report; final fault acceptance passed.
- Yamcs is RUNNING at `http://127.0.0.1:8090`; use Storage → `artemis-science` to
  download the raw FDP and receipt for the existing mission ops software. The
  producing dictionary is under the run's `ground/dictionaries/pi.json`.
- All virtual fault flags are reset: UART/RF connected, drop indices -1, delay 0.
  Containers remain running. `python3 tools/twin/twin.py down` stops the twin
  while retaining Pi/Yamcs volumes. No physical hardware or remote push/merge
  was performed. Runtime target heap/stack and physical timing remain bench gates.
- Implementation commits are local on `codex/artemis-local-twin`; original dirty
  build-script/sensor-audit/context changes remain preserved and uncommitted.

### Pre-fix mission ops compatibility diagnosis — 2026-11-11 HST

The existing C3M analysis app is **not yet drop-in compatible** with the twin
FDP: app/decoder recognize legacy Lepton container 0x10027000; the new Pi uses
0x10031000. Actual downloaded-file test reproduces signature rejection before
dictionary processing. F Prime 4.4.2 decoding with the producing Pi dictionary
succeeds with 19,200 samples. Raw-file acceptance above remains valid; it did
not test the complete mission ops importer. The new
[operator walkthrough](../operations/walkthroughs/C3M_YAMCS_LOCAL_TWIN.md) records
commands, wait conditions, download/handoff and this open compatibility gate.

### Compatibility root cause and preferred repair

Confirmed the differing Lepton base IDs predate this twin implementation in
the split-core baseline. Both drivers retain local container/record ID 0 and
the same record fields. Preserve the established C3M producer identity in the
twin, moving its colliding fcLinkBufferManager base as needed, rather than
requiring a working C3M application to adopt a new product ID. The first
rejection is not caused by the F′ upgrade; downstream version compatibility
remains an importer-test gate after identity repair. See the walkthrough.

### C3M producer identity repair — build milestone, 2026-11-11 HST

Restored the split Pi Lepton component base to 0x10027000, preserving both
local-zero container and record identities. Moved only the Pi link buffer
manager to unused 0x10034000. Independent ID/routing review found no other
required producer changes. Existing C3M application and decoder are unchanged.
Both native deployments rebuilt successfully with matching working-source
digest 5fa3d4d3239ce5ed4df9e9af4cd499e8e4b6f47b9b3ec333f8e0d5b410ad8c47.
New-run Yamcs capture/cache/downlink and actual unchanged-app import are the
remaining verification gates for this repair.

### C3M repair — nominal flow and unchanged-app milestone

Both regenerated dictionaries use Lepton container and record 0x10027000.
Fresh run `20261112T055046Z-d8611b` passed three full Yamcs scheduled captures,
products 16/17/18, with whole-cache commit and exact raw-file delivery. An actual
Yamcs GET for product 16 passed the unchanged canonical C3M ReceiverController:
classifier → channel-1 local-file replay → actual default decoder → History.
Its existing macOS Python 3.11 / F′ tools 4.2.1 environment produced a complete
Lepton History entry, 120×160 CSV, PNG, and all 19,200 fixture temperatures.
Source hashes before/after prove the app and decoder were not modified. This is
an explicit post-download compatibility test, not runtime analysis in Yamcs.
The retained-cache/Pi-off repair and remaining downloaded-file checks follow.

### C3M repair — full unchanged-app acceptance complete

- Fresh run `20261112T055046Z-d8611b`: **3/3 nominal cycles passed**, products
  16/17/18, and **Pi-off cached packet-loss repair passed**, product 19 with one
  repair round. Every raw FDP is 38,486 bytes with container/record 0x10027000.
- Parent independently downloaded all four from Yamcs and checked header, size,
  CRC16 and SHA against the receipt and Pi source oracle. All passed the actual
  unchanged canonical C3M app's classifier → local-file replay → default decoder
  → History pipeline, using its existing F′ 4.2.1 Python environment.
- All four History entries are complete Lepton products; CSVs are 120×160 with
  all 19,200 recorded-fixture temperatures matching, and PNGs were produced by
  the existing app. Code hashes and the resolved fprime-dp launcher are recorded.
- **15/15 runtime tests**, Python syntax and Compose checks pass. No analysis
  app/decoder code changed and no automatic analysis was added to the runtime.
- [Compact compatibility evidence](evidence/ARTEMIS_C3M_ID_COMPATIBILITY_2026-11-11.json)
  records exact builds/dictionaries, source/app/file hashes and per-product
  results. The walkthrough now documents successful import with the producing
  Pi dictionary. Retained pre-fix files keep their old incompatible identity.
- Independent review confirmed producer namespace/routing safety and actual
  unchanged-app verification. At this milestone physical hardware and browser interaction with
  the analysis UI had not been exercised; the later GUI proof below closes the
  browser check. Services remain running.

### Browser proof and publication preparation — 2026-11-11 HST

Opened the unchanged canonical C3M app at localhost:8064 with a fresh Yamcs GET
of product 19 (`af6acc97bee0d0cb67642d352e3f93022768d8377f755cfda464d8377f137027`).
The browser showed Payload complete, CRC passed, 38,486 bytes/1,100 packets, the
thermal image and 14.55–31.05 °C statistics. Its History tab showed Complete,
archived image and FDP/JSON/CSV/PNG links. User confirmed it works. The app's
local replay displays product 1/transfer 42; that is replay transport metadata,
not the source product handle 19. Launch/data provenance is retained under the
run's `gui-proof/`; no app code or Yamcs runtime analysis was added.

Added the current [architecture overview](../architecture/ARTEMIS_LOCAL_TWIN.md)
and documentation map. The user authorized organizing and pushing this feature
branch. Final committed contents will be preserved while grouping local history
into flight/payload core, Docker/Yamcs runtime, and documentation/evidence commits.
Original dirty files stay outside these commits. Dependency pins are verified
on their upstream refs; final remote ref verification follows publication.

### Named profiles — implementation and first build milestones, 2026-11-12 HST

Added `yamcs-twin` and `yamcs-rfm23-hil` definitions, FC CMake presets and the
`tools/deployments.py` command entry point. HIL build forces local-only cross
compilation, emits matched hardware artifacts and Pi startup/service inputs,
and never flashes/SSH-deploys. Real camera support is required for the HIL Pi.

The Teensy RFM23 target linked with its full 192 KiB cache and the Pi Zero W
ARMv6 target linked with libuvc enabled. Cross validation resolved the ARMv6
interpreter and all nine shared dependencies from the prepared read-only sysroot.
The physical ground serial adapter follows CDC0 CCSDS / CDC2 science and avoids
double RF framing. Targeted PTY tests and an actual native Yamcs PTY probe
passed, including real command bytes, recorded FDP packet loss/RETRY and exact
Yamcs GET. The final Linux-only Pi binary also passed QEMU ARM1176 startup with
recorded input, virtual UART, camera READY and clean SIGTERM exit.
See [named-profile commands](YAMCS_DEPLOYMENT_PROFILES.md).


### Named-profile acceptance and publication — 2026-11-12 HST

- Both complete named build commands passed. `yamcs-rfm23-hil` generated the
  Teensy41 RFM23 ELF/HEX, required-UVC Pi Zero W ARMv6 ELF, producing dictionaries,
  matched manifests and Pi startup/service bundle. The final Pi binary excludes
  the Zephyr library, passed ELF/dependency inspection, and ran on QEMU ARM1176
  with explicit recorded input, READY and clean shutdown.
- Final native Yamcs + two-PTY test passed against those exact HIL artifacts:
  actual PING bytes reached the command port; missing science DATA0 generated
  RETRY; the 38,486-byte Yamcs GET matched the replay source and receipt. This
  exercised the host adapter, not flight code or physical RF.
- Fresh twin run `20261112T191012Z-99de17` passed three full cycles, products
  20/21/22. Product 20 downlinked and repaired one lost packet with Pi OFF;
  all three independent Yamcs GETs matched the persisted Pi source oracle.
- A fresh Yamcs download of product 20 passed the unchanged canonical C3M app
  with its existing F′ 4.2.1 tools: complete History, CRC/SHA match, 160×120 image,
  CSV/PNG and all 19,200 recorded temperatures equal. No analysis software changed.
- All five full fault cases passed: invalid argument, active cancellation,
  PC-link interruption during staging, radio OFF/cache retention and Yamcs
  unavailable/outbox recovery. Cleanup restored device faults and resumed Yamcs.
- **45/45 local tests passed**: 28 runtime/profile/serial, four cross-wrapper,
  and 13 Ground Teensy host tests. The actual TripleSerial ground firmware also
  compiled. Two old snapshot assertions were corrected to allow the intentional
  `7c6de54` demo-only 424 MHz default while preserving every other byte check;
  product/radio settings were not changed. Independent review passed, including
  the saturated uplink/reconnect race regression and those snapshot corrections.
- RFM23 static reservations remain 268,432 bytes in main RAM (51.20% of its
  512 KiB bank) plus the full 192 KiB cache (75% of its 256 KiB bank), together
  44.35% of the Teensy's 1 MiB physical RAM. Runtime peaks remain unmeasured.
- [Compact final evidence](evidence/YAMCS_DEPLOYMENT_PROFILES_2026-11-12.json)
  records exact artifact/source/library hashes, emulator/PTY scope, product and
  app checks. [Profile commands](YAMCS_DEPLOYMENT_PROFILES.md) and
  [hardware handoff](ARTEMIS_HARDWARE_HANDOFF.md) replace the prior missing-build/
  missing-adapter status. Builds and tests performed no physical operation,
  flashing, Pi deployment or real camera capture. Original unrelated dirty work
  stays outside the feature commits. The local twin remains running for review.
