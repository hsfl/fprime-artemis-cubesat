# Worker G - Operator Docs

Date: 2026-07-06
Branch observed: `neutron2-develop`

## BLUF

Updated operator-facing docs for the hardening sprint's command-surface changes.
Docs-only; no code or scripts touched.

## Files Changed

- `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`
  - Added pending-collection cancel coverage to the validated/proven surface.
  - Documented `missionManager.CANCEL_COLLECTION`.
  - Documented that `missionManager.ENTER_BASE_MODE` cancels pending
    collections.
  - Documented command validation bounds:
    - `SCHEDULE_COLLECTION delaySeconds`: `1..300`.
    - capture duration: `1..120`.
    - default capture duration: `30`.
  - Documented invalid-value behavior: `VALIDATION_ERROR` plus rejection
    warning event.
  - Documented rejection warning event throttling after 5 emissions while GDS
    command responses still show `VALIDATION_ERROR`.
  - Documented that `scienceManager.SCIENCE_CAPTURE(durationSeconds)` is
    one-shot and does not change the default duration.
  - Documented persisted parameter flow:
    `scienceManager.CAPTURE_DURATION_SECONDS` via `PRM_SET`, then `PRM_SAVE`.
  - Documented `PrmDb.dat` as relative to the deployment runtime working
    directory.
  - Documented Teensy watchdog boot log meaning for normal arming and
    WDT-caused reset detection.
- `docs/MISSION_OPS_QUICK_RUN.md`
  - Added compact ground-rules bullets for validation bounds, cancel behavior,
    one-shot `SCIENCE_CAPTURE`, persistent parameter flow, `PrmDb.dat`, and
    watchdog boot logs.
- `deploy/pi/README.md`
  - Added a `PrmDb.dat` note for the systemd
    `WorkingDirectory=/home/pi/artemis/current` release-symlink layout.
  - Documented that swapping the `current` symlink can reset saved parameters
    unless operators re-run `PRM_SET`/`PRM_SAVE` or copy `PrmDb.dat` forward.
- `.gitignore`
  - Added root `PrmDb.dat` so local repo demo runs do not litter the worktree
    after `PRM_SAVE`.

## Validation

- Read sprint scratch doc and Worker A/E/F reports before editing.
- Scoped edits to docs and root ignore only.
- Did not edit code, scripts, or the sprint scratch doc.
