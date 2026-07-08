# C3M Payload Receiver Web UI Plan

BLUF: future HIL quality-of-life work should wrap `payload_receiver.py` with a
small local web UI so operators can see the ground-node receive state, not just
the F Prime/GDS flight-side downlink state.

## Intent

During C3M HIL, `fprime-gds` shows the spacecraft/F Prime side of the demo:
commands, events, telemetry, `PayloadDownlinkProgress`, and
`DownlinkFinished`. That is necessary, but it is not sufficient proof that the
ground node actually reconstructed the science product. The ground proof is
`payload_receiver.py` on the ground Teensy payload serial port.

The future UI should make that receiver proof visible in a browser:

- live receiver logs.
- header/product metadata.
- packet receive progress.
- missing packet and retry state.
- final CRC status.
- saved `.fdp` path.
- decoded Lepton JSON/CSV/PNG output.
- rendered thermal PNG preview.

Do this during HIL work, not as a current local-emulation blocker.

## Current Baseline

Current HIL operator flow uses two tools:

1. `fprime-gds` on channel 0 for command, events, and telemetry.
2. `ArtemisRpiTeensy_N2/tools/payload_receiver.py` on channel 1 for payload
   reconstruction.

The receiver already prints useful terminal output:

- `header: product=<id> transfer=<id> bytes=<n> packets=<n> crc=<crc>`
- `progress: <received>/<total>`
- `retry: start=<n> count=<n> bitmap_bytes=<n>`
- `saved: <file> product=<id> transfer=<id> bytes=<n> packets=<n> [ok]`
- `complete:` or `incomplete:` status in single-file mode.

The web UI should preserve this CLI behavior. Do not remove or weaken the
terminal path.

## Why GDS Alone Is Not Enough

GDS progress is flight-side evidence. It can show that F Prime believes it
queued or completed a payload downlink. It does not prove:

- the ground Teensy payload port received all channel-1 packets.
- retry requests were sent correctly.
- missing packets were repaired.
- the whole-file CRC matched.
- the reconstructed `.fdp` was actually written on the laptop.
- the Lepton product decoded into a viewable thermal frame.

The receiver UI is the ground-side proof panel.

## Proposed User Experience

Run one command from the repo root:

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py \
  --port "$GDS_PAYLOAD_PORT" \
  --baud 115200 \
  --output-dir "$RUN_DIR" \
  --decode-lepton \
  --web-port 8064
```

Open:

```text
http://127.0.0.1:8064
```

Suggested screen layout:

- top status strip: `Waiting`, `Receiving`, `Retrying`, `Complete`,
  `CRC Failed`, `Timed Out`.
- metadata panel: product ID, transfer ID, expected bytes, expected packets,
  packet payload bytes, expected CRC.
- progress bar: received packets / total packets, received bytes / total bytes.
- retry panel: missing packet count, last retry request, retry rounds.
- log panel: same event stream as terminal output.
- output panel: saved `.fdp`, decoded `.json`, `.csv`, `.png`.
- preview panel: Lepton PNG thumbnail/full image once decode succeeds.

## Implementation Shape

Keep this KISS and student-friendly:

- Use Python standard library HTTP where practical.
- Keep `pyserial` as the only required serial dependency.
- Avoid a frontend build chain unless there is a strong reason.
- Serve one local HTML page with lightweight JavaScript polling or server-sent
  events.
- Reuse the maintained decoder:
  `ground-station/lepton-dp-viewer/lepton_dp_viewer.py`.
- Keep the receiver engine reusable by extracting status callbacks from
  `payload_receiver.py`, or by wrapping it with a thin adapter that emits JSON
  state.

Recommended internal state object:

```text
status
product_id
transfer_id
total_bytes
received_bytes
total_packets
received_packets
missing_packets
retry_rounds
expected_crc
actual_crc
output_fdp
decoded_json
decoded_csv
decoded_png
logs[]
```

Do not make the UI responsible for F Prime commanding. GDS remains the command
and telemetry tool.

## HIL Integration

Update the C3M runbook so HIL starts:

1. GDS on channel 0.
2. Receiver web UI on channel 1.
3. Browser tabs side by side:
   - `http://127.0.0.1:<gds-port>`
   - `http://127.0.0.1:8064`
4. Run the normal C3M command sequence from GDS or `fprime-cli`.
5. Accept the run only when both panels agree:
   - GDS shows downlink completion.
   - Receiver UI shows CRC-complete file reconstruction and Lepton decode.

## Acceptance Criteria

- Existing `payload_receiver.py` CLI tests still pass.
- Existing CLI behavior still works without the web UI.
- Web UI shows live progress during a real or simulated receiver transfer.
- Web UI writes the reconstructed `.fdp` to the selected run directory.
- For C3M `.fdp`, web UI decodes and displays a Lepton PNG.
- UI explicitly reports CRC success/failure.
- HIL runbook documents the UI as optional QOL, not a required blocker for
  first RF bring-up.

## Non-Goals

- Do not replace `fprime-gds`.
- Do not command the spacecraft from this UI.
- Do not make local emulation depend on this UI.
- Do not store HIL output in `ground-station/c3m-lepton-test-data` unless a
  capture is intentionally promoted as a new reference sample.
