# C3M Payload Receiver Web App

BLUF: this is the normal channel-1 operator surface for the EPSCoR C3M demo.
Use F Prime GDS for commands/events/telemetry and keep this laptop browser open
for receiver readiness, payload progress, CRC proof, automatic Lepton decode,
and previous-run History.

## Start

From the repository root on macOS:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py
```

The app opens `http://127.0.0.1:8064/` and stores each run under repo-root
`data/`. It selects the third ground Teensy serial interface only when that
mapping is unambiguous; otherwise choose the channel-1 port in the page.

Do not request science downlink until the page shows
`Ready — awaiting downlink`. Only one receiver may own the channel-1 serial
port, so stop this app before using `payload_receiver.py` as a CLI fallback.

On Windows, run the same script in the documented WSL2 F Prime environment
with the Ground Teensy USB device attached, then open the printed localhost URL
in the normal Windows browser. Native Windows Python is also compatible when
`pyserial` and `fprime-dp` are installed and on `PATH`; COM ports are detected
without hard-coded device names.

Useful overrides:

```bash
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py \
  --port "$GDS_PAYLOAD_PORT" \
  --web-port 8064 \
  --no-open
```

## Local Replay

Replay the checked-in full-resolution Lepton product through the actual raw
channel-1 receiver parser without hardware:

```bash
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py \
  --replay-fdp ground-station/c3m-lepton-test-data/Dp_20260707_120740.fdp \
  --no-open
```

Use `--replay-bad-crc` to exercise the CRC failure state. Replay is local test
evidence, not a substitute for the final three-run HIL rehearsal.

Exercise best-effort recovery with a controlled missing packet:

```bash
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py \
  --replay-fdp ground-station/c3m-lepton-test-data/Dp_20260707_120740.fdp \
  --replay-drop-packet 500 \
  --transfer-timeout 5 \
  --no-open
```

Normal operation keeps the preferred CRC-verified path. If repair is still
incomplete at the default 90-second deadline, the app saves a positional
`.fdp.partial`, labels it partial, renders pixels touched by missing packets as
white/`NaN`, and records the missing packet map and timeout reason in
`run.json`. Hovering over white pixels reports `No data`.

## Validation

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 -m unittest \
  ArtemisRpiTeensy_N2/tools/tests/test_payload_receiver.py \
  ArtemisRpiTeensy_N2/tools/tests/test_c3m_payload_receiver_ui.py
node --check ground-station/c3m-payload-receiver-ui/static/app.js
```

See [`docs/C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md`](../../docs/C3M_PAYLOAD_RECEIVER_WEB_UI_PLAN.md)
and [`docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`](../../docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md)
for the full operator sequence and acceptance gates.
