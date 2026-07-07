# Worker E - Topology De-Fork

## Summary

Unified the Neutron 2 deployment back to one topology: `Top/topology.fpp`.
Deleted the former `Top/topology.local-demo.fpp` fork and removed the
`NEUTRON2_TOPOLOGY_PROFILE` CMake cache selector. Local rehearsal now builds
and runs the same topology used by HIL.

Worker A's cancel wiring is preserved:

- `missionManager.cancelRequestOut -> scienceManager.cancelRequestIn`

## Complete Former Topology Delta List

Compared current `topology.fpp` against former `topology.local-demo.fpp` before
deletion:

| Delta | Decision | Reasoning |
| --- | --- | --- |
| Whitespace-only differences near imports and connection comments | Kept `topology.fpp` formatting | No behavior impact. |
| Rate group 1 ordering: HIL had `payloadDownlinkManager.run` at index 7 and `scienceManager.run` at index 8; local demo had `scienceManager.run` at index 7 and `payloadDownlinkManager.run` at index 9 | Kept HIL ordering | Both periodic paths remain active. The automated demo does not require the local ordering, and the HIL ordering is the judge-facing baseline. |
| `sohManager.run`: HIL disabled; local demo enabled | Kept disabled | `SoHManager.run` only writes periodic telemetry channels. The demo already has command-triggered `EMIT_SOH_SNAPSHOT`; enabling the periodic loop would add RF downlink pressure. |
| `commsManager.run`: disabled in both files, different comments only | Kept disabled | No functional delta. Keeping it disabled avoids adapter polling/event noise. |
| `payloadService.run`: HIL disabled; local demo enabled | Kept disabled | `PayloadService.run` only publishes periodic telemetry/SOH. Collection handoff uses `requestIn_handler` and adapter status ports, so the command-driven demo path does not require this periodic loop. |
| `storageService.run`: HIL disabled; local demo enabled | Kept disabled | `StorageService.run` only publishes periodic telemetry/SOH. Storage, report, and downlink handoff paths are command/port driven. |

No former local-demo-only rate-group connection was enabled in the unified
topology. RF-budget-sensitive periodic emitters remain off; this is now noted
in `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`.

## Files Changed

- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.fpp`
- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.local-demo.fpp` (deleted)
- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/CMakeLists.txt`
- `ArtemisRpiTeensy_N2/tools/run_neutron2_local_demo.sh`
- `tools/validate_local.sh`
- `EMULATION.md`
- `docs/NEUTRON2_LOCAL_EMULATION_RUNBOOK.md`
- `docs/NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`
- `docs/MISSION_OPS_QUICK_RUN.md`
- `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`
- `docs/STUDENT_COMPONENT_STARTERS.md`
- `docs/agents_notes.md`
- `docs/DEMO_RELIABILITY_GAP_AUDIT_2026-07-06.md`

Historical references were intentionally left in:

- `docs/HARDENING_SPRINT_2026-07-06_SCRATCH.md`
- `docs/hardening-reports/A-scheduling.md`
- archived review docs under `docs/archive/`

## Validation

Command run from repo root:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate && ./tools/validate_local.sh
```

Result: **FAIL in this Codex sandbox before command sequencing**.

Passing portions before the failure:

- shared Teensy firmware drift check passed
- generated transport headers check passed
- F Prime/Teensy transport constants check passed
- Python local-emulation and payload-receiver tests passed
- unified topology generated and built
- all 6 F Prime component unit-test executables passed

The failure was not a missing topology run connection. The automated demo could
not start its local socket services because this sandbox denies local socket
binds:

```text
100% tests passed, 0 tests failed out of 6

Total Test time (real) =   2.72 sec
[validate-local] running automated local demo sequence
[neutron2-demo] dictionary: /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
[neutron2-demo] capture dir: /tmp/neutron_payload_captures
[neutron2-demo] logs: /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/tools/logs/neutron2_local_demo_20260706_142740
[neutron2-demo] started local emulator pid=83900; GDS: http://127.0.0.1:5061
[neutron2-demo] started payload viewer pid=83901; viewer: http://127.0.0.1:8063
Timed out waiting for fprime-gds on 127.0.0.1:5061
```

Relevant log excerpts:

```text
zmq.error.ZMQError: Operation not permitted (addr='ipc:///tmp/fprime-server-in')
zmq.error.ZMQError: Operation not permitted (addr='ipc:///tmp/fprime-server-out')
 * Serving Flask app 'fprime_gds.flask.app'
 * Debug mode: off
Operation not permitted
```

```text
PermissionError: [Errno 1] Operation not permitted
```

Independent sandbox probe:

```text
tcp PermissionError [Errno 1] Operation not permitted
unix PermissionError [Errno 1] Operation not permitted
```

Next required verification outside this restricted sandbox:

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
./tools/validate_local.sh
```
