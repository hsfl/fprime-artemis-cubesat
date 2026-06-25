# Mission Ops Quick Run

BLUF: mission ops should use scripts, not hand-assemble F Prime commands.

## Laptop Rehearsal

Use this when the FlatSat bench is not available.

```bash
cd ~/Developer/fprime-artemis-cubesat
./tools/validate_local.sh
```

This validates the generated transport headers, local Python tooling, F Prime
native build, component unit tests, and the automated `local-demo` sequence.

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

- `hil` is the default hardware profile.
- `local-demo` is for laptop/RPi rehearsal with simulated payload behavior.
- Services own mission commands, state, telemetry, and events.
- Adapters own board protocols, buses, radios, packet formats, and hardware quirks.
- Missing hardware gets a simulator adapter selected by topology profile, not a runtime command.

## Top Failure Checks

1. If the build fails after RF/UART edits, run:
   ```bash
   python3 tools/generate_transport_constants.py --check
   python3 tools/check_transport_constants.py
   ```
2. If GDS opens but commands do not work, verify the selected profile and dictionary.
3. If payload progress appears but no file is viewable, check `tools/payload_receiver.py` or the local capture directory.
4. If HIL channel 2 fails, treat it as EPS/PDU adapter or satellite-Teensy-local RPC work first, not as a ground RF problem.
5. If PDU behavior changes while the ICD settles, update `EpsAdapter_Artemis` first. Refactor `EpsService` only if the mission-facing EPS command contract becomes misleading.
