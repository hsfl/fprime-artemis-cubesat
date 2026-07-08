# EPSCoR C3M Lepton Reference Test Data

BLUF: this folder is the checked-in real Lepton reference sample for the C3M
demo. The laptop local demo uses this sample grid to generate a new F Prime
`.fdp`, downlink it through the local channel-1 path, decode it, and verify the
decoded CSV matches this reference data.

## Files

```text
ground-station/c3m-lepton-test-data/
  Dp_20260707_120740.fdp              # reference F Prime Data Product
  data/Dp_20260707_120740.csv         # decoded 120x160 Celsius grid
  data/Dp_20260707_120740.json        # decoded fprime-dp JSON
  data/Dp_20260707_120740.png         # reference thermal PNG
  dp_lepton_viewer.py                 # original source-branch viewer script
```

Reference frame:

- Dimensions: `160x120` pixels.
- File size: `38480` bytes for the `.fdp`.
- Temperature range: about `14.55C` to `31.05C`, mean about `19.34C`.
- Capture timestamp in the decoded product: `2026-06-18T22:59:09.528687+00:00`.

## Local Demo Use

From the F Prime project root:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
. fprime-venv/bin/activate
./tools/run_c3m_local_demo.sh --delay 10 --capture-seconds 10 --exit-after-sequence
```

The script defaults to:

```text
../ground-station/c3m-lepton-test-data/data/Dp_20260707_120740.csv
```

It exports that path as `C3M_LEPTON_SAMPLE_CSV`, then runs the normal C3M
mission sequence:

1. `missionApp.ENTER_BASE_MODE`
2. `sohApp.EMIT_SOH_SNAPSHOT`
3. `payloadDriverLepton.ENABLE`
4. `scienceApp.CONFIGURE_CAPTURE_DURATION`
5. `missionApp.SCHEDULE_COLLECTION`
6. `storageManager.REPORT_LATEST_DATASET`
7. `storageManager.REPORT_STORAGE_HISTORY`
8. `commsApp.REQUEST_SCIENCE_DOWNLINK`

Pass criteria:

- a new `ArtemisRpiTeensy_N2/DpCat/Dp_*.fdp` is produced.
- the local channel-1 downlink completes.
- the generated `.fdp` decodes to JSON, CSV, and PNG.
- the decoded CSV matches `data/Dp_20260707_120740.csv`.
- the PNG opens for normal operator runs; use `--no-open` for headless runs.

To test a different Lepton sample intentionally:

```bash
./tools/run_c3m_local_demo.sh --sample-csv /path/to/other_lepton_sample.csv
```

## Manual Decode

Use the maintained viewer under `ground-station/lepton-dp-viewer`:

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 ground-station/lepton-dp-viewer/lepton_dp_viewer.py \
  ground-station/c3m-lepton-test-data/Dp_20260707_120740.fdp \
  --outdir /tmp/c3m_real_sample_decode \
  --summary --no-show
```

Expected summary:

```text
width=160 height=120 pixels=19200 min=14.55C max=31.05C mean=19.34C
```

The viewer writes `.json`, `.csv`, and `.png` outputs. It can render PNGs
without `matplotlib`; if `matplotlib` is installed, it uses the richer chart
rendering path.

## HIL Use

For real bench downlink, run the payload receiver against the ground Teensy
payload serial port and write outputs to a run directory, not over this checked
in reference sample:

```bash
cd ~/Developer/fprime-artemis-cubesat
RUN_DIR=/tmp/neutron_hil/c3m_rf_demo_$(date +%Y%m%d_%H%M%S)
mkdir -p "$RUN_DIR"
python3 ArtemisRpiTeensy_N2/tools/payload_receiver.py \
  --port <ground-teensy-payload-port> \
  --output-dir "$RUN_DIR" \
  --ext .fdp \
  --debug
```

Then decode the reconstructed `.fdp`:

```bash
python3 ground-station/lepton-dp-viewer/lepton_dp_viewer.py \
  "$RUN_DIR"/*.fdp \
  --outdir "$RUN_DIR/lepton_decode" \
  --summary --no-show
```

Commit new HIL captures here only when they are intentionally promoted as
reference samples.

Future receiver UI intent is captured in
[`docs/C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md`](../../docs/C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md).
The reason is simple: GDS shows flight-side downlink progress, while the
payload receiver proves the ground node actually reconstructed and decoded the
`.fdp`.
