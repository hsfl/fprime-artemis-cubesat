# Hardening Sprint Scratch Doc — 2026-07-06 (LIVING DOCUMENT)

> Orchestrator: Claude Fable 5. Workers: Codex (GPT-5.5) via `codex exec`.
> **Every agent working this sprint: read this file first.** Workers write reports to
> `docs/hardening-reports/<task-id>.md`; the orchestrator folds results back into this file.
> Branch: `neutron2-develop` (HEAD = freeze commit `abc8bcd`, tag `v1.0.0-mvp-demo`, validated green 2026-07-02).

## Mission

Harden the frozen Neutron 2 MVP demo — reliability + simplicity only, **no new features**
(no PDU work, no GDS dashboard). Source plan: `docs/DEMO_RELIABILITY_GAP_AUDIT_2026-07-06.md`
and `docs/archive/FABLE_FLIGHT_READINESS_REVIEW_2026-07-01.md` (§ numbers below refer to it).

**Acceptance test:** `tools/validate_local.sh` passes end-to-end (local emulation incl. automated
demo). HIL RF testbench verification happens later on real hardware — don't claim it.

## Hard constraints (from Dennis, 2026-07-06)

- **SKIP CI jobs** — explicitly out of scope this sprint.
- §1.1: **one topology for everything** — unify onto the `hil` topology; local emulation and the
  HIL RF demo must build/run the SAME topology (test what you fly). Kill the `local-demo` fork.
- No new features; hardening + demo simplicity/understandability only.
- Student platform policy: macOS first, Windows/WSL second (see CLAUDE.md).
- Do not commit; leave changes in working tree for orchestrator review.

## Task board

| ID | Scope | Status | Worker report |
| --- | --- | --- | --- |
| A | §1.2 scheduling hardening (validate/clamp inputs, cancel path, command-path mode validation, sane defaults) + **new UTs for ScienceManager and CommsManager** | ✅ DONE (builds + 6/6 UT executables pass; orchestrator code-reviewed) | `docs/hardening-reports/A-scheduling.md` |
| — | Phase 1 complete: A/B/C/D all landed and reviewed | — | — |
| B | §1.4 Teensy hardware watchdogs (satellite + ground firmware) | ✅ DONE (both real Arduino builds pass) | `docs/hardening-reports/B-watchdog.md` |
| C | §1.7 firmware drift check in `tools/validate_local.sh` + docs hygiene (stale `RF_MVP_DEMO_RUNBOOK` refs; move downlink research out of archive) | ✅ DONE (orchestrator extended drift list) | `docs/hardening-reports/C-drift-docs.md` |
| D | §1.5 Pi provisioning: `deploy/pi/` with proper systemd unit (`Restart=always`), provisioning checklist, migration note from the `ln` service | ✅ DONE (orchestrator-reviewed) | `docs/hardening-reports/D-pi-provisioning.md` |
| E | §1.1 topology de-fork: single topology (hil), delete `topology.local-demo.fpp`, update CMake/tools/runbook | ✅ DONE — unified onto hil wiring, NO periodic loops enabled (demo is command-driven); orchestrator ran full `validate_local.sh` un-sandboxed: **PASS incl. automated demo CSV** | `docs/hardening-reports/E-topology.md` |
| F | Params persistence (`PrmDb` `param` declarations for demo config, e.g. capture duration) + FPP ops pass (event `throttle`, `update on change`, channel `low/high` limits) | ✅ DONE (param + 13 throttles + 20 update-on-change + RSSI limits; guessed limits correctly skipped) | `docs/hardening-reports/F-params-fppops.md` |
| V | Final verification: full `validate_local.sh` + hil generate/build + both Arduino builds | ✅ **PASS** — orchestrator ran full gate un-sandboxed 2026-07-06: drift check, transport checks, Python tests, unified-topology build, 6/6 UT suites, automated demo produced + parsed science CSV. Both Teensy builds green (verified earlier, untouched since). | — |

## Key repo facts (verified 2026-07-06, save workers the recon)

- Venv: `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`; build from `ArtemisRpiTeensy_N2/` with
  `fprime-util generate -f && fprime-util build` (always `-f`, stale caches bite).
- Topology profile switch: `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/CMakeLists.txt:13-24`
  (`NEUTRON2_TOPOLOGY_PROFILE` cache var, default `hil`); `tools/run_neutron2_local_demo.sh` selects
  `local-demo`. The two topology files differ mainly in rate-group wiring: hil comments out
  `sohManager.run`, `payloadService.run`, `storageService.run` (RF-budget tradeoff).
- UT pattern to copy: `ArtemisRpiTeensy_N2/Components/MissionManager/test/` (mode-transition UT).
  `ScienceManager` and `CommsManager` have NO test dirs yet.
- §1.2 landmine details: `ScienceManager.cpp:46` stores raw delaySeconds (0 = never fires);
  default `m_captureDurationSeconds` is **600 s** (constructor) — 10-min dead air on bare
  `START_COLLECTION`; `SCIENCE_CAPTURE` persists its duration into subsequent collections;
  `MissionManager.cpp:42,56` command handlers bypass `isAllowedTransition` (port path enforces it).
- House style for rejected commands: `VALIDATION_ERROR` response + warning event
  (see `EpsService` confirm-gating and `EpsCommandRejected`).
- Teensy workspaces build with `./tools/arduino-cli/build.sh` run FROM the workspace root
  (`ArtemisTeensy_N2_Baremetal`, `GDS_Teensy`). Sketches:
  `firmware/satellite_teensy/satellite_teensy.ino`, `firmware/gds_teensy/gds_teensy.ino`.
- Shared-but-duplicated Teensy modules (drift-check targets): `artemis_rf23bp.hpp`,
  `link_counters.hpp` (must be identical); `rf23_driver.*` and `relay_uart_rf.cpp` intentionally
  diverge — check-file list must be explicit, not a blanket diff.
- Stale doc refs to fix (plain-text `docs/RF_MVP_DEMO_RUNBOOK.md` → `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`):
  `docs/GET_STARTED_TESTING.md:10,53`, `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md:12`,
  `docs/GLOSSARY.md`, `docs/agents_notes.md:504,904`.
- Pi service today: systemd unit named `ln` (!), prose-only in docs
  (`docs/HARDWARE_PORT_MAP_AND_POWER.md`, `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`,
  `docs/agents_notes.md`). Runs `ArtemisRpiTeensyDeployment -d /dev/serial0`.

## Decisions log

- 2026-07-06: Skip redundant baseline validate run — HEAD is the freeze commit already validated
  green on 7/2 (tag message records PASS). Build cache reserved for worker A.
- 2026-07-06: Phase 1 = A/B/C/D in parallel (disjoint paths, only A uses the F´ build cache).
  Phase 2 = E then F sequential (shared F´ build cache + overlapping files). Final = V.
- 2026-07-06: Unified topology policy for E: start from hil wiring. If the local emulation demo
  genuinely needs a disabled rate-group connection, enable it in the ONE topology (accepting the
  RF-budget cost) and document that in the runbook — never re-fork.
- 2026-07-06: `SCIENCE_CAPTURE` duration persistence: make it NOT persist (one-off capture),
  document in the FPP comment. `CONFIGURE_CAPTURE_DURATION` remains the way to change the default.

## Post-sprint verification round (2026-07-06, same day)

| ID | Scope | Status | Report |
| --- | --- | --- | --- |
| R | Adversarial second-opinion review of the full uncommitted diff (codex read-only, high) | ✅ DONE — 2 findings: BLOCKER cross-thread race on active managers' sync inputs (cancel can race final tick → surprise collection); MAJOR over-throttled operator-facing rejection events | `scratchpad R-review.txt` (findings folded into H) |
| G | Operator docs for the new surface (CANCEL_COLLECTION, bounds, param flow, WDT log line) + Pi PrmDb.dat symlink-swap note + gitignore | ✅ DONE (orchestrator-verified) | `docs/hardening-reports/G-operator-docs.md` |
| H | Fix R's findings: convert state-mutating sync inputs to async on ScienceManager/MissionManager/CommsManager, tick-boundary cancel UT, throttle split (command rejections always visible; storm-capable warnings stay throttled), doc touch-up | ✅ DONE — 12 ports converted to async; orchestrator re-ran full `validate_local.sh` un-sandboxed: **PASS incl. automated demo** | `docs/hardening-reports/H-race-throttle-fix.md` |

| I | Docs currency sweep: gap-audit scorecard flipped with evidence, agents_notes dated section, README stale bits, straggler sweep | ✅ DONE (orchestrator-verified) | `docs/hardening-reports/I-docs-currency.md` |
| J | ARMv6 cross-compile verify (`docker_cross_compile_pi_zero_w.sh --local-only --skip-sync`, cached sysroot, NO deploy) | ✅ **PASS** — build OK; readelf `Tag_CPU_arch: v6KZ` + VFPv2 + armhf interpreter; qemu smoke ran F´ startup/events then expected no-`/dev/serial0` abort, **no segfault**; fresh binary 2,303,820 B (+26,792 vs frozen) | `docs/hardening-reports/J-armv6-cross.md` |

Additional investigation findings (2026-07-06):
- ~~ARMv6 cross-build of hardened code NOT yet run~~ **RESOLVED same day by worker J (above).**
  Bench still needs the real-Pi run, but compile/link/startup sanity for ARMv6 is proven.
- PrmDb.dat: relative path in runtime cwd → on Pi lives inside the per-release dir behind the
  `current` symlink → params silently reset on release swap (documented by G; stable-data-dir
  path change is a candidate future improvement, deliberately not coded).
- Verified non-issues: first boot without PrmDb.dat is non-fatal (both demo runs prove it);
  new event strings fit com buffers; no leftover local-demo/profile refs anywhere.

## Post-freeze follow-on: native App-Man-Drv rename (2026-07-06, worker K)

- Sprint work committed (5 atomic commits `76ec9ca..2008b18`) and pushed to
  `origin/neutron2-develop` BEFORE the rename, so the refactor is separable history.
- Worker K renamed all 19 HAL components to native F´ nomenclature (old Manager→`*App`,
  Service→`*Manager`, Adapter→`*Driver_<Hw>`), instances, topology, GDS command strings in
  tools, and the active docs; `docs/hardening-reports/K-appmandriver-rename.md` has the table.
- **Incident:** K's blanket Adapter→Driver sed leaked into the two GENERATED Python venvs
  (`fprime-venv`, `.cross-venv-linux`), corrupting pip's vendored `requests`
  (`HTTPAdapter`→`HTTPDriver`). Git-tracked code, submodules, and `external/` verified
  untouched. Orchestrator rebuilt `fprime-venv` from the exact dist-info pin set
  (fprime-gds==4.2.1, fprime-tools==4.2.1, fprime-fpp==3.2.0, 70 pkgs) and deleted
  `.cross-venv-linux` for the cross script to regenerate. Lesson for future rename tasks:
  exclude `*venv*` and all generated trees from sed sweeps EXPLICITLY.
- Post-rename, clean-venv verification: full `validate_local.sh` PASS (incl. automated demo)
  AND ARMv6 cross-compile PASS (Tag_CPU_arch v6 attributes verified). One straggler fixed by
  orchestrator: old names in `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md`.
- Rendered diagrams (`docs/*.png`/`.svg` from `.mmd`) still show old instance names —
  regenerate when convenient (K updated the `.mmd` sources).

## SPRINT COMPLETE — 2026-07-06

All tasks landed (A–H incl. post-sprint adversarial review + fixes); final `tools/validate_local.sh`
gate PASSED end-to-end on the unified topology, race-fixed managers, and split throttles.
Everything is UNCOMMITTED in the working tree awaiting Dennis's review/commit decision.
Remaining for next bench session (NOT laptop-verifiable): HIL RF smoke on the unified topology,
deliberate WDT trip test (procedure in B's report), `ln` → `artemis-fprime.service` migration +
D's six bench-confirmation questions, and PRM_SAVE round-trip on the real Pi filesystem.

## Status updates (orchestrator appends)

- 2026-07-06: Sprint started. Phase 1 dispatched.
- 2026-07-06: D landed + reviewed. `deploy/pi/{artemis-fprime.service,README.md}`. Unit uses the
  deployed-release layout `/home/pi/artemis/current` (symlink pattern), NOT the repo-checkout path.
  Migration off the `ln` service is documented but must be executed at the next bench session.
  D's report lists 6 bench-confirmation questions (service name, symlink targets, account) — fold
  into the next HIL checklist.
- 2026-07-06: B landed + reviewed. `wdt_guard.hpp` (byte-identical both trees): direct RT1062
  WDOG1 registers (no library dep), 12 s timeout, feeds in main loops + relay poll + UART drain +
  RF segment delay + ACK retry paths; WDT-reset reason logged at boot. Worker's sandbox blocked
  `arduino-cli` network access, so orchestrator re-ran BOTH real `build.sh` runs: PASS/PASS.
  Bench-test procedure for deliberately tripping the WDT is in B's report — add to next HIL run.
- 2026-07-06: C landed + reviewed. Drift guard in `tools/validate_local.sh` (self-tested:
  fails on corrupted copy). Orchestrator added `wdt_guard.hpp` as third must-be-identical pair
  and a comment documenting `link_protocol.hpp` as INTENTIONALLY divergent (satellite carries
  channel 2 local-RPC constants the ground Teensy doesn't). Stale runbook refs fixed; downlink
  research doc moved to `docs/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md` with links updated.
