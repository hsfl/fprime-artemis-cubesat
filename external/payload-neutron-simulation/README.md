# Neutron Detector Test Dataset

Synthetic neutron-count data standing in for the real ASU detector until flight
hardware is in hand. Use it to exercise the `science_capture` command path,
the payload component poll, and any downstream data handling on the FlatSat.

## What this data is

Each row is one **integration window** of a simulated LEO neutron detector.
The detector counts neutron events; in a fixed window that count is a random
draw from a Poisson distribution whose mean is set by the instantaneous flux.

Two physical regimes are represented:

- **Background (BG):** quiet galactic-cosmic-ray + albedo neutron flux. Low,
  roughly steady counts.
- **South Atlantic Anomaly (SAA):** trapped-particle enhancement during a pass
  through the anomaly. Counts spike well above background, ramp up and back down
  over the pass.

This is not calibrated to a specific detector. Treat the numbers as
order-of-magnitude realistic, not flight-truth.

## File format

`neutron_data.csv`, plain CSV with a header row.

| Column    | Type   | Units   | Meaning                                              |
|-----------|--------|---------|------------------------------------------------------|
| `t_s`     | int    | seconds | Mission-elapsed time at the start of the window      |
| `counts`  | int    | counts  | Neutron events detected during that window           |
| `flag`    | string | n/a     | `BG` = background, `SAA` = inside an anomaly pass     |

Key fact for the reader: **one row = one second.** The window length is fixed at
1.0 s, so the row index and `t_s` are equivalent and a requested capture duration
maps directly to a row count.

Example:

```
t_s,counts,flag
0,8,SAA
1,8,SAA
2,9,SAA
3,8,SAA
4,11,SAA
```

Current file: 3600 rows = 1 hour of data, containing one SAA pass near the start.

## How `science_capture` reads it

The test program treats the CSV as the "live" detector. The flow:

1. **Command in.** Ground (or the test harness) issues `science_capture` with a
   capture duration, e.g. capture 10 minutes.
2. **Poll the payload component.** The command handler polls the payload
   component, which is bound to the program that owns this dataset.
3. **Compute row count.** Duration maps to rows directly because each row is 1 s:

   ```
   n_rows = duration_seconds / window_s        # window_s = 1.0
   # 10 min capture -> 600 rows
   ```

4. **Extract a slice.** Pull `n_rows` consecutive rows starting from the current
   read cursor. Advance the cursor so the next capture continues where this one
   left off (simulates time moving forward through the orbit).
5. **Return / log.** Hand the slice back as the science product for that capture.

### Worked example

`science_capture duration=600`

- `window_s = 1.0` -> 600 rows
- Read cursor at row 0 -> return rows `[0 .. 599]` (t_s 0 through 599)
- Cursor advances to row 600 for the next capture

A capture starting at row 0 lands inside the SAA ramp, so the returned slice
will show the count spike. Later captures (cursor past ~600 s) sit in
background, showing low steady counts. This gives you both regimes to test
against without changing the file.

## Notes for using it in tests

- **Reproducibility.** The data was generated with a fixed RNG seed, so the
  file is identical run to run. Re-generate with the emulator if you need a
  different seed or duration.
- **Running off the end.** If a capture requests more rows than remain past the
  cursor, decide on a policy: clamp to the last row, wrap to the top, or return
  an error/short read. Pick whichever matches how you want the FSW to behave on
  a real partial read.
- **Different integration time.** If the real detector integrates over something
  other than 1 s, re-generate with that `window_s` and update the row-to-duration
  math accordingly (`n_rows = duration / window_s`).
- **Scaling realism.** Three knobs in the emulator drive the numbers: baseline
  count rate, SAA multiplier, and window length. Tune them once real detector
  specs are known.

## Repo simulator CLI

The repo-local simulator is:

```bash
./neutron_payload_sim.py capture --duration-seconds 600 --format kv
```

By default it:

- reads `neutron_data.csv`
- advances a cursor in `.neutron_payload_cursor.json`
- writes capture CSV products under `captures/`
- allocates incrementing filenames and never overwrites an existing capture CSV
- wraps back to row 0 at the end of the dataset for repeatable demos

The F Prime adapter runs the same script from the deployment using:

```bash
python3 external/payload-neutron-simulation/neutron_payload_sim.py capture \
  --duration-seconds <seconds> \
  --cursor /tmp/neutron_payload_sim_cursor.json \
  --output-dir /tmp/neutron_payload_captures \
  --end-policy wrap \
  --format kv
```

Set `NEUTRON_PAYLOAD_SIM_ROOT=/abs/path/to/external/payload-neutron-simulation`
if the deployment is launched from a working directory where the default
relative path is wrong.

## F Prime demo commands

- Immediate payload capture:
  - `PayloadService.SCIENCE_CAPTURE(durationSeconds)`
- Immediate science-flow capture through the science manager:
  - `ScienceManager.SCIENCE_CAPTURE(durationSeconds)`
- Scheduled demo flow:
  - `ScienceManager.CONFIGURE_CAPTURE_DURATION(durationSeconds)`
  - `MissionManager.SCHEDULE_COLLECTION(delaySeconds)`
- Storage/operator visibility:
  - `StorageService.REPORT_LATEST_DATASET`
  - `StorageService.REPORT_STORAGE_HISTORY`
  - `StorageService.REMOVE_OLD_DATASETS(confirm=1)`

The simulator adapter reports the captured CSV product size back into the
existing `PayloadService -> ScienceManager -> StorageService` status path.
The cleanup command removes simulator capture CSVs from
`/tmp/neutron_payload_captures` and resets the in-memory storage history. Use it
for testing/debug only until mission-ops policy is agreed with the system
engineer.

Capture filenames are safe by default. If the base timestamp/cursor/duration
name already exists, the simulator appends `_001`, `_002`, etc. Operators should
still clean up old downlink files after test runs so the ground viewer stays easy
to scan.

## Ground-side review

Payload downlink is still blob/file oriented. Once the ground receiver or test
path reconstructs a CSV product, open it with the Neutron 2 viewer:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --file /tmp/neutron_payload_captures/neutron_capture_YYYYMMDDTHHMMSSZ_00000_00600.csv
```

If using the default simulator capture directory, this is enough:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py
```

On Windows PowerShell:

```powershell
py -3 ground-station\neutron2-payload-viewer\neutron2_payload_viewer.py `
  --file C:\path\to\reconstructed\neutron_capture.csv
```

The viewer is intentionally separate from `fprime-gds`: GDS handles command,
event, telemetry, and transfer-progress visibility; this tool handles the
Neutron 2-specific science-data review.
