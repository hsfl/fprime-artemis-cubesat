# Demo-Reliability Gap Audit — Follow-up to the Fable Flight-Readiness Review

> **🤖 Produced by Claude Fable 5** (`claude-fable-5`) on **2026-07-06**, branch `neutron2-develop`,
> as a status re-audit of
> [`archive/FABLE_FLIGHT_READINESS_REVIEW_2026-07-01.md`](archive/FABLE_FLIGHT_READINESS_REVIEW_2026-07-01.md).
> Every "still open" claim below was re-verified against the working tree on 2026-07-06 (code read,
> `rg` sweeps, `gh release view`). Investigation only — no fixes were applied in this session.
> Status: **active planning input**; archive after the Week-1 goal below is met.

## TL;DR

The freeze happened and it was done well: tag `v1.0.0-mvp-demo` + GitHub Release with
`SHA256SUMS` (2026-07-02), and work moved to a fresh `neutron2-develop` branch. **Everything else
in the review's "freeze week" list is still open**, and all four top-ranked reliability risks
(§1.1–§1.4) remain live in the code. With FSR v2.0 due **2026-07-31 (3.5 weeks)**, the plan's
Week-1–2 items have not started. The good news: the highest-value remaining items are
**laptop-only** — no bench time needed.

Post-sprint status (2026-07-06): the Week-1 goal was executed the same day via
delegated workers A-H. Current sprint source of truth is
[`HARDENING_SPRINT_2026-07-06_SCRATCH.md`](HARDENING_SPRINT_2026-07-06_SCRATCH.md);
all sprint changes are still uncommitted pending review.

## Scorecard vs. the 2026-07-01 review (verified 2026-07-06)

| Review item | Status | Evidence |
| --- | --- | --- |
| §1.6 Freeze: tag + release + checksums | ✅ **Done** | `v1.0.0-mvp-demo`, release asset `neutron2-v1.0.0-mvp-demo-artifacts.zip`, validation note in tag message |
| Freeze-first branch discipline | ✅ Done | v2.0 work on `neutron2-develop`, tag untouched |
| §1.2 Scheduling landmines | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/A-scheduling.md`](hardening-reports/A-scheduling.md) adds delay/duration validation, sane defaults, one-shot capture semantics, and cancel path; [`hardening-reports/H-race-throttle-fix.md`](hardening-reports/H-race-throttle-fix.md) fixes the async cancel race. Was: `ScienceManager.cpp:46-51` still stores `delaySeconds` raw (0 = stuck forever); no cancel path anywhere (`rg -i cancel` = 0 hits); no clamps (`CONFIGURE_CAPTURE_DURATION` accepts any U32). |
| §1.2 Command-bypass of mode validation | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/A-scheduling.md`](hardening-reports/A-scheduling.md) routes command-driven mode changes through validation and adds cancel/base-mode behavior. Was: `MissionManager.cpp:42-47,56-66`: `ENTER_BASE_MODE` and `SCHEDULE_COLLECTION` set `m_currentMode` directly; only the port path (`modeUpdateIn_handler:33`) checks `isAllowedTransition`. |
| `ScienceManager`/`CommsManager` UTs | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/A-scheduling.md`](hardening-reports/A-scheduling.md) adds focused `ScienceManager` and `CommsManager` UT dirs; final sprint validation reports 6/6 UT executables pass in [`HARDENING_SPRINT_2026-07-06_SCRATCH.md`](HARDENING_SPRINT_2026-07-06_SCRATCH.md). Was: no `test/` dir in either component. |
| §1.3 CI job | ⏸ SKIPPED by decision (Dennis, 2026-07-06) | Evidence: [`HARDENING_SPRINT_2026-07-06_SCRATCH.md`](HARDENING_SPRINT_2026-07-06_SCRATCH.md) lists "SKIP CI jobs" as a hard sprint constraint. Was: no `.github/workflows/` exists. |
| §1.1 Topology de-fork + HIL rehearsal gate | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/E-topology.md`](hardening-reports/E-topology.md) removes the former demo-only topology fork and profile selector; `run_neutron2_local_demo.sh` now runs the unified topology. |
| §1.4 Teensy watchdogs | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/B-watchdog.md`](hardening-reports/B-watchdog.md) adds byte-identical `wdt_guard.hpp`, 12 s watchdog arming, feed points, and reset-reason boot logs; both Arduino builds passed per sprint scratch. Was: `rg -i watchdog` = 0 hits in both firmware trees. |
| §1.5 Pi provisioning in repo | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/D-pi-provisioning.md`](hardening-reports/D-pi-provisioning.md) adds `deploy/pi/artemis-fprime.service` with `Restart=always` and a provisioning/migration README. Was: no `deploy/` dir; systemd unit (service name `ln`!) exists only as prose in docs. |
| §1.7 Firmware drift check | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/C-drift-docs.md`](hardening-reports/C-drift-docs.md) adds explicit shared Teensy drift checks to `tools/validate_local.sh`; sprint scratch notes the watchdog guard pair was added too. Was: `tools/validate_local.sh` has no must-be-identical file check. |
| Params persistence (`PrmDb`) | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/F-params-fppops.md`](hardening-reports/F-params-fppops.md) adds `ScienceManager.CAPTURE_DURATION_SECONDS` with PrmDb-backed boot default and parameter update handling. Was: 0 `param` declarations in `Components/*/*.fpp`. |
| FPP ops pass (throttle / update-on-change / limits) | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/F-params-fppops.md`](hardening-reports/F-params-fppops.md) adds throttles, update-on-change channels, and RSSI low limits; [`hardening-reports/H-race-throttle-fix.md`](hardening-reports/H-race-throttle-fix.md) splits operator rejections from storm-capable warnings. Was: 0 uses of each (only a prose mention of hand-rolled throttling in `PayloadDownlinkManager.fpp:79`). |
| Stage-0 judge dashboard XML | ❌ Open | No dashboard XML in the deployment tree |
| SatNOGS/SDR GO/NOGO decision record | ⚠️ Partial | New `archive/HACKRF_SDR_GROUND_STATION_INVESTIGATION_2026-06-30.md` sets a sound posture (RFM23BP stays demo-safe path; SDR is receive-only research first) but has **no dated gates, no owner, no expiry** — the plan-vs-investigation contradiction the review flagged is still unresolved on paper |
| Doc hygiene: stale renamed-runbook refs | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/C-drift-docs.md`](hardening-reports/C-drift-docs.md) fixes stale RF MVP runbook refs in live docs. Was: still plain-text-stale in `GET_STARTED_TESTING.md` (x2), `SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`, `GLOSSARY.md`, `agents_notes.md`. |
| Move downlink-research doc out of `archive/` | ✅ Done (2026-07-06 hardening sprint, uncommitted) | Evidence: [`hardening-reports/C-drift-docs.md`](hardening-reports/C-drift-docs.md) moves active downlink research to [`FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md`](FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md). Was: still at `archive/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md` despite active growth. |

## New findings from this audit (not in the 2026-07-01 review)

1. **Default capture duration is a landmine of its own.** `ScienceManager` constructs with
   `m_captureDurationSeconds(600)` — a bare `START_COLLECTION` with no prior configure commits the
   demo to a **10-minute** collection window. The runbook demo uses 30 s. A defaulted-or-fat-fingered
   command mid-demo produces exactly the "dead air" failure mode the review warned about. Clamp
   *and* pick a demo-sane default (e.g. 30 s).
2. **`SCIENCE_CAPTURE` silently persists its duration.** It overwrites
   `m_captureDurationSeconds` (line 92), so a one-off long capture changes every *subsequent*
   `START_COLLECTION`/`SCHEDULE_COLLECTION` too. Surprising cross-command state; worth an explicit
   decision (and a UT) either way.
3. **The freeze release is one zip.** Fine for sharing, but demo-day flashing instructions should
   name the exact files inside it; the review imagined per-artifact assets. Cheap fix: list the
   zip's contents + per-file SHA256 in the release notes next release.
4. **The post-freeze GDS-downlink research ends in open team questions.** Its "Questions for the
   Team" (judge-facing stock-GDS vs. file-arrives-reliably; expected file sizes; stock-GDS
   constraint for judging) are **decision blockers** for any Stage-1/2/3 GDS work — answer them
   before spending more research effort there. The research itself already converged: keep the
   channel-1 sidecar; prototype the repair helper post-MVP.
5. **The Fable review was archived early.** Its own header says move to `archive/` only after v2.0
   lands. Harmless, but the active checklist (Part 8) is now buried where nobody tracks it — which
   may be exactly why freeze-week items were missed. This audit exists to fix that.
6. **The Pi systemd service is named `ln`.** Verified in three docs. A service indistinguishable
   from the coreutils binary is a debugging trap for students (`systemctl status ln`). Rename when
   the unit file gets versioned into `deploy/pi/` (§1.5).

## Goal (set this session)

**By 2026-07-10 (end of Week 1), close every laptop-only reliability gap so bench time from
Week 2 onward is spent exclusively on PDU/payload HIL — not on re-fixing operational landmines.**

Concretely, in priority order:

1. **Scheduling-path hardening + UTs** (demo-critical, ~1 day):
   reject `delaySeconds == 0` and `durationSeconds == 0` with `VALIDATION_ERROR`; clamp upper
   bounds (suggest ≤ 300 s delay, ≤ 120 s duration for demo builds); add a `CANCEL_COLLECTION`
   command and make `ENTER_BASE_MODE` clear `ScienceManager`'s countdown; route command-driven
   mode changes through `isAllowedTransition`; fix the 600 s default; lock all of it with
   `ScienceManager` + `MissionManager` UTs (extend the existing mode-validation UT pattern).
2. **CI** (~half day): `.github/workflows/` job running the cheap tier — transport drift check +
   Python unittests — on every PR. Free (public repo).
3. **Firmware drift guard** (~1 hour): must-be-identical check for the shared
   satellite/ground Teensy modules in `validate_local.sh` (and thus in CI).
4. **Topology de-fork** (~half day): shared `.fppi` connection graphs; only the rate-group block
   stays per-profile. Then add a written **hil-profile rehearsal gate** to the runbook.
5. **Doc hygiene sweep** (~1 hour): fix the five stale runbook refs; move
   `FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md` out of `archive/`; write the SDR GO/NOGO as a
   dated decision record with owner + gates (the investigation doc is 90 % of it already).

Deliberately **deferred past Week 1** (need bench or team decisions): params persistence → Teensy
WDT → `Restart=always` chain; FPP ops pass (throttle/update-on-change/limits); Stage-0 dashboard;
`deploy/pi/` provisioning; answers to the GDS-downlink team questions.

## Suggested check-back

Re-run this audit's scorecard after the Week-1 goal (target ~2026-07-13) and again before the
first full-stack rehearsal. Pass criterion: every "❌ Open" row in the laptop-only set flips to ✅
with evidence (commit, CI run, or runbook section).
