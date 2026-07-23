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

### C3M/macOS primary ground path

Confirm the exact qualified HackRF/9–10 inch monopole/no-attenuator geometry,
then run:

```bash
cd ~/Developer/fprime-artemis-cubesat/ground-station/hackrf-rf22
.venv/bin/python run_hackrf_ground_station.py \
  --enable-tx \
  --tx-safety-confirmed
```

The supervisor pins ACK mode, TX `16`, RX `8/8`, `100 ms` lead, amplifier/bias
off, starts GDS and the payload receiver, and prints both URLs. There is no AGC
or student RF tuning. Use
[`HACKRF_GROUND_STATION_RUNBOOK.md`](HACKRF_GROUND_STATION_RUNBOOK.md) for the
ready gates and demo sequence. If the fixed path fails, stop it and use the
GDS Teensy/RFM23BP cold fallback.

### Neutron-2 `D2` or ground-Teensy fallback

The current HackRF proof does not qualify `D2` or Windows. For that path,
identify the exact ground-Teensy channel-0 port—never select the first wildcard
device when multiple Teensies are present—then run:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
./tools/run_gds_uart.sh --port "<verified-ground-channel-0-port>"
```

HIL validates what laptop rehearsal cannot:

- Pi UART behavior
- satellite Teensy firmware behavior
- RF link behavior
- real PDU response behavior over channel 2
- real payload hardware behavior when available

## Ground Rules

- `Top/topology.fpp` is the one topology for laptop rehearsal and HIL.
- Managers own subsystem commands, state, telemetry, and events.
- Drivers own board protocols, buses, radios, packet formats, and hardware quirks.
- Missing hardware gets a simulator driver wired in the topology, not a runtime command.
- Scheduled collection accepts delays from `1..300` seconds; capture duration
  accepts `1..120` seconds and defaults to `30`.
- Invalid operator command values return `VALIDATION_ERROR` and emit a
  rejection warning event every time; storm-capable link/downlink warnings
  remain throttled for RF event budget.
- `missionApp.CANCEL_COLLECTION` cancels a pending collection; so does
  `missionApp.ENTER_BASE_MODE`.
- `scienceApp.SCIENCE_CAPTURE(durationSeconds)` is one-shot and does not
  change the default duration. Persist a new default with
  `PRM_SET scienceApp.CAPTURE_DURATION_SECONDS`, then `PRM_SAVE`.
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
4. If HIL channel 2 fails, treat it as EPS/PDU driver or satellite-Teensy-local RPC work first, not as a ground RF problem.
5. If PDU behavior changes while the ICD settles, update `EpsDriver_Artemis` first. Refactor `EpsManager` only if the mission-facing EPS command contract becomes misleading.

For the full layer-by-layer checklist, use
`docs/SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`.
