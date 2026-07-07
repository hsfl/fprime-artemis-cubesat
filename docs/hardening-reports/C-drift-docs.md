# Worker C Report - Firmware Drift Guard + Docs Hygiene

Date: 2026-07-06

## Scope

- Added a shared-by-copy Teensy firmware drift guard to `tools/validate_local.sh`.
- Fixed stale runbook references to `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`.
- Moved active downlink reliability research out of `docs/archive/`.

## Firmware Drift Guard

Added `check_shared_teensy_drift` to `tools/validate_local.sh`, called before generated-header checks and before any F Prime build/cache work.

Explicit checked pair list:

| Satellite path | Ground path | Precheck evidence |
| --- | --- | --- |
| `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/artemis_rf23bp.hpp` | `GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp` | `cmp -s ...; echo $?` returned `0` |
| `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_counters.hpp` | `GDS_Teensy/firmware/gds_teensy/src/link_counters.hpp` | `cmp -s ...; echo $?` returned `0` |

Excluded:

- `link_protocol.hpp`: verified with `cmp -s`; exit code was `1`, so it is not byte-identical today and was not included.
- `rf23_driver.*` and `relay_uart_rf.cpp`: intentionally divergent; the validator includes a script comment documenting this.
- Worker B's watchdog file, if present later, was not added.

## Self-Test Evidence

Standalone clean run, extracted from `tools/validate_local.sh` without running the full validator:

```text
[validate-local] checking shared Teensy firmware drift
+ cmp -s ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/artemis_rf23bp.hpp GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp
+ cmp -s ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_counters.hpp GDS_Teensy/firmware/gds_teensy/src/link_counters.hpp
```

Negative test:

- Temporarily added a marker comment to `GDS_Teensy/firmware/gds_teensy/src/link_counters.hpp`.
- Ran the extracted check.
- It failed as intended:

```text
[validate-local] ERROR: Firmware drift: ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_counters.hpp != GDS_Teensy/firmware/gds_teensy/src/link_counters.hpp; shared-by-copy modules must be byte-identical; fix BOTH sides
```

Restoration:

- Removed the temporary marker.
- `git diff -- ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_counters.hpp GDS_Teensy/firmware/gds_teensy/src/link_counters.hpp` returned no diff.
- `cmp -s ...link_counters.hpp ...link_counters.hpp; echo $?` returned `0`.
- Final standalone drift check passed.
- `bash -n tools/validate_local.sh` passed.

Full `tools/validate_local.sh` was not run, per sprint constraint.

## Docs Hygiene

Runbook reference fixes:

- `docs/GET_STARTED_TESTING.md`
  - replaced the old RF MVP runbook path with `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md` in the guide list.
  - replaced the maintained-procedure reference with `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`.
- `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`
  - replaced the old RF MVP runbook path with `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`.
- `docs/agents_notes.md`
  - replaced both stale old RF MVP runbook path references with `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`.
- `docs/GLOSSARY.md`
  - checked; already referenced `NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`, so no edit was needed.
- `docs/DEMO_RELIABILITY_GAP_AUDIT_2026-07-06.md`
  - removed the stale old runbook filename from the open-item row text.

Downlink research doc move:

- Requested move: archive copy of `FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md` -> `docs/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md`.
- `git mv` could not write `.git/index.lock` under the sandbox, so the file was moved with filesystem `mv`; Git should detect this as a delete/add or rename at review time.
- Updated links in `docs/archive/FABLE_FLIGHT_READINESS_REVIEW_2026-07-01.md`:
  - old archive/downlink research target -> `../FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md`
- Updated `docs/DEMO_RELIABILITY_GAP_AUDIT_2026-07-06.md` text to reference `FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md` outside `archive/`.

Verification scans:

- Searching for the old archive/downlink research prefix returned no hits.
- Searching for the old RF MVP runbook path, excluding the protected sprint scratch file, returned hits only under `docs/archive/`.
- The unexcluded old RF MVP runbook path search still reports `docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md:60`; that file was explicitly protected by the task instructions and was not edited.

## Result

PASS: drift-check self-test passed cleanly and failed on a corrupted copy with the intended error.
