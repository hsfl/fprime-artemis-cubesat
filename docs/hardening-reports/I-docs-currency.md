# Worker I - Docs Currency Sweep

Date: 2026-07-06
Branch observed: `neutron2-develop`

## Scope

Updated active docs to match the completed hardening sprint state. Docs only;
no code, scripts, deploy files, archive docs, sprint scratch, or existing
worker reports were modified.

## Files Changed

- `docs/DEMO_RELIABILITY_GAP_AUDIT_2026-07-06.md`
  - Added a post-sprint note under the TL;DR pointing to
    `docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md`.
  - Flipped sprint-closed scorecard rows to
    `Done (2026-07-06 hardening sprint, uncommitted)` with evidence links to
    the relevant worker reports.
  - Marked the CI row as
    `SKIPPED by decision (Dennis, 2026-07-06)`.
  - Kept the original audit evidence visible as `Was:` text for flipped rows.
  - Left Stage-0 dashboard and SatNOGS/SDR GO/NOGO rows unchanged.
- `docs/agents_notes.md`
  - Removed the stale open TODO for `ScienceManager`/`CommsManager` UTs.
  - Appended `## Demo Hardening Sprint (2026-07-06)` with the landed changes,
    validation evidence, and still-open bench/target items.
- `README.md`
  - Replaced stale default-profile wording with unified-topology wording.
  - Updated the post-v1 work summary so completed hardening items are no
    longer described as future work.
  - Added the current `validate_local.sh` coverage summary.
  - Replaced stale demo-orchestration TODO wording with the remaining
    bench/target proof items.

## Verification

- Read `docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md` fully before editing.
- Checked worker reports A-H for evidence links:
  - `docs/hardening-reports/A-scheduling.md`
  - `docs/hardening-reports/B-watchdog.md`
  - `docs/hardening-reports/C-drift-docs.md`
  - `docs/hardening-reports/D-pi-provisioning.md`
  - `docs/hardening-reports/E-topology.md`
  - `docs/hardening-reports/F-params-fppops.md`
  - `docs/hardening-reports/G-operator-docs.md`
  - `docs/hardening-reports/H-race-throttle-fix.md`
- Required retired-topology sweep returned no live-doc hits:
  - `rg -n "topology\\.local-demo|NEUTRON2_TOPOLOGY_PROFILE|local-demo profile" README.md docs -g '!docs/archive/**' -g '!docs/hardening-reports/**' -g '!docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md'`
- Verified every relative file/link touched exists.
- `git diff --check -- docs/DEMO_RELIABILITY_GAP_AUDIT_2026-07-06.md docs/agents_notes.md README.md` passed.

## Not Changed

- Did not edit `docs/archive/*`.
- Did not edit `docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md`.
- Did not edit existing `docs/hardening-reports/A-H` files.
- Did not edit code, scripts, or deploy files.
