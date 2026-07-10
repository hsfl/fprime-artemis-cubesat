# C3M Payload Receiver Web UI Plan

BLUF: the C3M payload receiver web app is the primary channel-1 operator tool
for the refined demo. The normal operator uses `fprime-gds` for spacecraft
commands, events, and telemetry, and this web app for receiver readiness,
payload progress, CRC proof, automatic Lepton decode, and payload history.
`payload_receiver.py` and `lepton_dp_viewer.py` remain engineering fallbacks.

This plan implements the ground-side portion of
[`C3M_DEMO_HARDENING_PLAN_2026-07-09.md`](C3M_DEMO_HARDENING_PLAN_2026-07-09.md).

## Operator Intent

The demo operator should not need to manage receiver flags, retry bitmaps,
output paths, or decoder commands. Their channel-1 flow is:

1. Start or open the payload web app.
2. Confirm `Ready — awaiting downlink`.
3. Run the mission commands in GDS.
4. Watch the current product arrive, verify, decode, and render.
5. Optionally browse earlier products in History.

The web app does not send F Prime commands. GDS remains the command and
telemetry authority.

## Why GDS Alone Is Not Enough

GDS shows flight-side evidence such as `PayloadDownlinkProgress` and
`DownlinkFinished`. It does not prove that the ground node:

- received every channel-1 packet.
- requested and received any required repairs.
- matched the whole-file CRC.
- wrote the reconstructed `.fdp` on the laptop.
- decoded the product into a viewable thermal frame.

The web app is the ground-side proof panel. A demo run passes only when GDS and
the web app both show success.

## Existing Receiver Baseline And Fallback

`ArtemisRpiTeensy_N2/tools/payload_receiver.py` already owns the channel-1
packet parser, missing-packet bitmap requests, whole-file CRC, and file
reconstruction. Preserve both existing CLI modes:

- default single-file mode waits for one transfer, writes `payload_blob.bin`,
  and exits.
- explicit `--output-dir DIR` mode listens continuously and writes uniquely
  named products.

The web app must consume the same receiver engine through structured
events/callbacks. Do not duplicate the channel-1 protocol or make terminal-log
scraping the primary integration.

Only one process may own the channel-1 serial port. Stop the web app before
using the CLI fallback, and stop the CLI before returning to the web app.

## Start And Open

From the repo root:

```bash
cd ~/Developer/fprime-artemis-cubesat
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py
```

The app opens `http://127.0.0.1:8064/` by default. It should auto-select the
ground Teensy payload port only when the triple-serial grouping is unambiguous.
Otherwise, the browser presents a simple port selector. The app may not show
`Ready` until the selected serial port is successfully open and listening.

Useful engineering overrides remain available:

```bash
python3 ground-station/c3m-payload-receiver-ui/c3m_payload_receiver_ui.py \
  --port "$GDS_PAYLOAD_PORT" \
  --web-port 8064 \
  --no-open
```

## Primary Views

### Current

The Current view always represents the active or most recently completed live
transfer. It shows:

- `Starting`, `Ready`, `Receiving`, `Retrying`, `Verifying`, `Decoding`,
  `Complete`, `Delayed`, `Failed`, or `Disconnected`.
- selected serial port and connection health.
- product ID and transfer ID.
- expected and received bytes and packets.
- packet progress bar, elapsed time, and smoothed ETA.
- missing-packet count and retry rounds.
- expected and actual CRC.
- the current receiver event log.
- links to the saved `.fdp`, JSON, CSV, and PNG.
- the current thermal PNG after CRC-successful decode.

Never leave an earlier successful image looking current after a new transfer
starts or fails. The current product/transfer identity and state remain visible
next to the image.

### History

History browses previous run directories beneath repo-root `data/`. Each row
shows enough proof to distinguish runs: timestamp, product ID, transfer ID,
elapsed time, retry count, CRC result, and decode result. Opening a historical
run must be visually labeled `History` and must not replace the Current state.

## Data Contract

The web app defaults to repo-root `data/`, not filesystem-root `/data` and not
`/tmp/neutron_hil`. Each transfer receives a unique directory:

```text
data/
  c3m_YYYYMMDD_HHMMSS_transfer_<id>/
    payload.fdp
    payload.json
    payload.csv
    payload.png
    run.json
```

If whole-file CRC fails, preserve the received bytes as `payload.fdp.badcrc`,
record the failure in `run.json`, and do not invoke the Lepton decoder.

The app decodes the exact `.fdp` completed by the current receiver transfer. It
does not search for a generic "latest" file.

`run.json` records at minimum:

- run ID and UTC start/end timestamps.
- receiver state and explicit failure reason, if any.
- serial port and baud.
- product ID and transfer ID.
- total and received bytes and packets.
- elapsed time and last estimated remaining time.
- retry rounds and missing-packet count.
- expected and actual CRC.
- local SHA-256.
- exact `.fdp`, JSON, CSV, and PNG paths.

Generated `data/` contents remain local and are ignored by Git. Promote a
capture into `ground-station/c3m-lepton-test-data` only when intentionally
creating a new checked-in reference sample.

## Timing Presentation

- Under `60 s`: nominal/green.
- `60–120 s`: taking longer than nominal but acceptable/yellow.
- Over `120 s`: delayed/operator attention/red.

The delayed state does not hide or stop an active transfer. Continue showing
real packet progress, retries, and elapsed time, and offer a simple reset or
reconnect path. Keep transport success separate from timing acceptance: a late
CRC-valid product may be technically complete while still missing the live-demo
timing gate.

For an active transfer beyond `120 s`, the page exposes `Reset receiver`. Use it
only after declaring the timed run unsuccessful; wait for `Ready` again before
requesting a deliberate retry from GDS.

ETA is explicitly an estimate. Display it only after enough packets have
arrived to produce a useful rate, and smooth it so 1 Hz bursts and retry rounds
do not create a misleading countdown.

## Implementation Shape

Keep the app KISS and student-friendly:

- Python standard-library HTTP plus the existing `pyserial` dependency.
- one local HTML page with lightweight JavaScript; no frontend build chain.
- target the laptop/desktop demo browser; phone/mobile presentation is not an
  acceptance requirement.
- bind to `127.0.0.1` by default.
- auto-detect the payload port only when unambiguous; otherwise use a selector.
- reuse the maintained
  `ground-station/lepton-dp-viewer/lepton_dp_viewer.py` decoder.
- decode only after whole-file CRC succeeds.
- serve output files through safe local URLs.
- restrict any reveal/open-folder operation to paths beneath repo-root `data/`.
- support macOS first and the documented Windows/WSL2 student path without
  hard-coded macOS device names or path separators.

Minimum structured receiver state includes:

```text
status
message
port
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
elapsed_seconds
estimated_remaining_seconds
output_fdp
decoded_json
decoded_csv
decoded_png
logs[]
```

## HIL Integration

The normal demo uses two browser surfaces side by side:

1. GDS on channel 0.
2. C3M payload web app on channel 1.

Use this event-gated sequence:

1. Confirm the web app is `Ready`.
2. Enter Base Mode, emit SOH, enable Lepton, configure capture, and schedule it
   in GDS.
3. Wait for a new `ScienceStored` event with an incremented product count.
4. Request science downlink for that newly stored product.
5. Confirm the GDS `PayloadDownlinkStarted` product ID agrees with the web-app
   header.
6. Send exactly one mid-transfer channel-0 PING; otherwise keep channel 0 quiet
   during bulk transfer.
7. Accept only after the web app reports CRC success and displays the decoded
   current image.

## Acceptance Criteria

- Existing `payload_receiver.py` tests and CLI modes still pass.
- App reaches `Ready` with no channel-1 traffic.
- Successful packet replay reaches `Complete`, writes `data/` outputs, and
  renders the Lepton PNG.
- Induced CRC failure produces a prominent failure state and never presents an
  older image as current.
- Two consecutive transfers complete without restarting the app and appear as
  distinct History entries.
- Port unavailable, port busy, and serial disconnect states provide clear
  operator actions.
- Three consecutive fresh HIL products at the intended demo geometry each
  reach CRC-complete decode within `120 s`; the first begins from a fresh
  app/bench startup.

## Transport Freeze And Non-Goals

- Do not change transport-constant values for this UI work.
- Do not reflash either Teensy merely to build or test the web app.
- Do not replace `fprime-gds` or send F Prime commands from this UI.
- Do not replace the maintained Lepton decoder.
- Do not make local F Prime emulation depend on the web app.
- Do not treat local replay as HIL repeatability proof.
