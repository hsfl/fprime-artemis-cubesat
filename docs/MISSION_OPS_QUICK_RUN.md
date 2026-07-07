# Mission Ops Quick Run

BLUF: mission ops should use scripts, not hand-assemble F Prime commands.

Use `docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md` when the quick run fails and
you need to identify which layer owns the issue.

## Laptop Rehearsal

Use this when the FlatSat bench is not available.

```bash
cd ~/Developer/fprime-artemis-cubesat
./tools/validate_local.sh
```

This validates the generated transport headers, local Python tooling, F Prime
native build, component unit tests, and the automated local demo sequence.

For a faster smoke run after a known-good build:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_neutron2_local_demo.sh --skip-build --exit-after-sequence
```

Pass criteria:

- `fprime-gds` accepts the demo command sequence.
- a new neutron capture CSV is produced.
- payload downlink completes.
- the payload viewer parses the CSV.

## FlatSat / HIL

Use this only when the bench hardware is available.

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/run_gds_uart.sh --port "$PORT"
```

HIL validates what laptop rehearsal cannot:

- Pi UART behavior
- satellite Teensy firmware behavior
- RF link behavior
- real PDU response behavior over channel 2
- real payload hardware behavior when available

## Ground Rules

- `Top/topology.fpp` is the one topology for laptop rehearsal and HIL.
- Services own mission commands, state, telemetry, and events.
- Adapters own board protocols, buses, radios, packet formats, and hardware quirks.
- Missing hardware gets a simulator adapter wired in the topology, not a runtime command.
- Scheduled collection accepts delays from `1..300` seconds; capture duration
  accepts `1..120` seconds and defaults to `30`.
- Invalid operator command values return `VALIDATION_ERROR` and emit a
  rejection warning event every time; storm-capable link/downlink warnings
  remain throttled for RF event budget.
- `missionManager.CANCEL_COLLECTION` cancels a pending collection; so does
  `missionManager.ENTER_BASE_MODE`.
- `scienceManager.SCIENCE_CAPTURE(durationSeconds)` is one-shot and does not
  change the default duration. Persist a new default with
  `PRM_SET scienceManager.CAPTURE_DURATION_SECONDS`, then `PRM_SAVE`.
- F Prime writes `PrmDb.dat` in the runtime working directory.
- Teensy bench serial logs print `watchdog reset detected` after a WDT-caused
  reset; `hardware watchdog armed (12s)` is the normal boot arming line.

## Top Failure Checks

1. If the build fails after RF/UART edits, run:
   ```bash
   python3 tools/generate_transport_constants.py --check
   python3 tools/check_transport_constants.py
   ```
2. If GDS opens but commands do not work, verify the dictionary matches the current build.
3. If payload progress appears but no file is viewable, check `tools/payload_receiver.py` or the local capture directory.
4. If HIL channel 2 fails, treat it as EPS/PDU adapter or satellite-Teensy-local RPC work first, not as a ground RF problem.
5. If PDU behavior changes while the ICD settles, update `EpsAdapter_Artemis` first. Refactor `EpsService` only if the mission-facing EPS command contract becomes misleading.

For the full layer-by-layer checklist, use
`docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`.
