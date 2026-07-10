# C3M Demo Hardening Plan — 2026-07-09

## BLUF

Move the proven EPSCoR C3M Lepton RF path from MVP/debug operation into a
refined, repeatable demo workflow. The normal demo operator should use only:

1. `fprime-gds` for channel-0 commands, events, and telemetry.
2. The C3M payload receiver web app for channel-1 readiness, receive progress,
   retry visibility, CRC proof, Lepton decode, and payload history.

The existing `payload_receiver.py` CLI and `lepton_dp_viewer.py` CLI remain
engineering fallbacks. Do not change any HIL-validated transport-constant
values in this work.

## Implementation Status

As of 2026-07-09, WP1–WP7 are implemented and validated. The obsolete Pi
rollback binary is removed, the current ARMv6/libuvc release is active, and the
new web app passed three consecutive live HIL capture/downlink/decode runs at
the tabletop demo geometry.

## User Intent

> We are moving beyond the MVP into clean, refined mission operations for the
> live demo. Abstract the receiver, retry, CRC, filesystem, and decoder details
> away from the end-user operator. They should only need to open F Prime GDS and
> the payload web app, confirm the app is ready, run the mission commands in
> GDS, and watch the current thermal product arrive and render.

The happy path should normally finish in about 60 seconds. A real demo geometry
may require retries, so 60–120 seconds remains acceptable when progress is
visible and the final product passes CRC. The operator must never wait several
minutes without a clear state, timer, retry count, or failure explanation.

## Proven Baseline To Preserve

- Branch: `epscorc3m/demo`.
- Real UVC Lepton capture through the current App-Man-Drv architecture.
- Full product: 38,480-byte `.fdp`, 160x120, 19,200 pixels.
- Channel-1 transfer: 1,100 packets.
- Final accepted command-to-file time: 58.557 seconds.
- Receiver: 1,100/1,100 packets, final CRC success, no retry request.
- Source and ground SHA-256 matched.
- A channel-0 ping returned during channel-1 downlink.
- Final satellite CRC, framing, timeout, RF-TX, and queue-drop counters were
  zero.
- Validated RF geometry: antennas approximately 30 inches apart,
  line-of-sight, on the tabletop.

Frozen transport values include the 115200 8N1 Pi UART, 37 ms base drain
margin, 40 ms additional channel-0 margin, 22 payload/retry messages per 1 Hz
run, 15 ms payload RF gap, directional ACK policy, and the RFM23BP 125 kbps PHY
setup. Documentation and app wiring may change; these values may not.

## Target Operator Flow

1. Connect and power the HIL bench.
2. Open `fprime-gds` on channel 0.
3. Start/open the C3M payload receiver web app on channel 1.
4. Confirm a green `Ready — awaiting downlink` state. Do not request downlink
   before this state appears.
5. In GDS, enter Base Mode, emit SOH, enable the real Lepton backend, configure
   capture duration, and schedule collection.
6. Wait in GDS for a new `ScienceStored` event/product identity.
7. Request science downlink for that new product.
8. Watch packet progress, elapsed time, estimated remaining time, retries, and
   CRC state in the web app.
9. On CRC success, the app saves the run, decodes the exact newly received
   `.fdp`, and displays the thermal PNG automatically.
10. Optionally browse earlier successful products through the History view.

The web app does not send F Prime commands. GDS remains the command and
telemetry authority.

## Visual Specification

The generated design concepts are implementation specifications:

- [Receiving state](design/C3M_PAYLOAD_RECEIVER_UI_RECEIVING_CONCEPT.png)
- [Complete and history state](design/C3M_PAYLOAD_RECEIVER_UI_COMPLETE_CONCEPT.png)

The visual direction is a calm, precise aerospace operations surface: true
near-white background, deep navy typography, cool-gray structure, restrained
semantic green/yellow/red, and thermal color used only for decoded imagery.
Avoid dark sci-fi styling, decorative dashboards, fake metrics, and command
controls.

## Data Contract

The web app owns a repo-root `data/` workspace. Each completed or failed
transfer receives a unique timestamped run directory:

```text
data/
  c3m_YYYYMMDD_HHMMSS_transfer_<id>/
    payload.fdp
    payload.json
    payload.csv
    payload.png
    run.json
```

CRC-failed blobs are retained as `payload.fdp.badcrc` and are never decoded.

`run.json` records at minimum:

- run ID and UTC timestamps.
- receiver state and failure reason, if any.
- serial port and baud.
- product ID and transfer ID.
- total/received bytes and packets.
- elapsed seconds and estimated remaining seconds.
- retry rounds and missing-packet count.
- expected and actual CRC.
- local SHA-256.
- exact `.fdp`, JSON, CSV, and PNG paths.

The current transfer must always be visually distinct from history. Selecting a
historical product must never make it look like the active/current transfer.

## State Model

The primary operator states are:

- `Starting`: app is opening the selected serial port.
- `Ready`: serial port is open and listening; safe to request downlink.
- `Receiving`: current header accepted and packets are arriving.
- `Retrying`: receiver is repairing missing packets.
- `Verifying`: all packets are present and whole-file CRC is being checked.
- `Decoding`: CRC passed and the exact current `.fdp` is being decoded.
- `Complete`: CRC passed, outputs exist, and current thermal image is visible.
- `Delayed`: elapsed time exceeded 120 seconds while transfer remains active.
- `Failed`: CRC, timeout, decode, or filesystem failure with an explicit reason.
- `Disconnected`: selected serial port disappeared or cannot be reopened.

Timing presentation:

- Under 60 seconds: nominal.
- 60–120 seconds: yellow, taking longer than nominal but acceptable.
- Over 120 seconds: red operator-attention state; keep showing actual progress
  and retries and expose `Reset receiver`. The timed run is unsuccessful even
  if a later transfer completes.

## Work Packages

### WP1 — Preserve And Expose The Receiver Engine

- Keep `payload_receiver.py` packet parsing, CRC, retry, and CLI behavior
  working as the fallback path.
- Extract or add structured status callbacks/snapshots so the web app consumes
  receiver state without duplicating channel-1 protocol logic.
- Preserve explicit single-file CLI mode and explicit continuous
  `--output-dir` mode.
- Ensure only one process owns the channel-1 serial port.

Acceptance:

- Existing receiver CLI tests pass unchanged.
- CLI can still complete a single-file transfer.
- Directory mode can still receive consecutive transfers.
- Structured state exposes header, progress, retry, CRC, completion, and
  failure information.

### WP2 — Build The Cross-Platform Receiver Web App

- Add `ground-station/c3m-payload-receiver-ui/`.
- Use Python standard-library HTTP plus the existing `pyserial` dependency.
- Use one HTML page with lightweight code-native CSS and JavaScript; no
  frontend package manager or build chain.
- Default to repo-root `data/` and continuous listening.
- Auto-detect the payload serial port when unambiguous; otherwise present a
  simple port selector.
- Provide Current and History views.
- Show progress, elapsed time, smoothed ETA, retry rounds, missing packets, CRC,
  current product/transfer identity, live log, output links, and thermal image.
- Decode only after CRC success by reusing the maintained Lepton decoder.
- Serve output files through safe local URLs.
- Keep any reveal/open-folder action localhost-only, platform-aware, and
  restricted to paths underneath `data/`.

Cross-platform target:

- macOS first.
- Windows student/operator path through WSL2 for serial and Python, with the
  browser opened natively at `http://127.0.0.1:<port>`.
- Linux-compatible path handling and browser links.

### WP3 — Refine The Operator Runbook

- Make the web app the standard channel-1 demo tool.
- Keep raw CLI commands in a clearly labeled engineering fallback section.
- Add the event-gated sequence:
  `UI Ready -> schedule -> new ScienceStored/product ID -> request downlink ->
  CRC complete -> decode`.
- Require one mid-transfer channel-0 ping and otherwise keep channel 0 quiet
  during bulk transfer.
- Add Ground Teensy USB recovery using pinned upload ID `usb:100000`.
- Document nominal and degraded timing semantics.
- Document three-run final rehearsal acceptance.

### WP4 — Remove The Obsolete Pi Rollback Trap

- Status: completed on 2026-07-09. The active service and release symlink were
  verified before and after removal.
- Deleted
  `/home/pi/artemis/backup/ArtemisRpiTeensyDeployment-9eba742-uvc`.
- Remove any active runbook language that presents it as a supported rollback.
- Keep `/home/pi/artemis/releases/` as the only supported runtime release set.
- Git history remains sufficient if the obsolete architecture ever needs to be
  reconstructed.

### WP5 — Document Transport Configuration Without Retuning It

- Add a table mapping each pacing/RF manifest key to its generated headers and
  rebuilt artifacts.
- State that any transport-value change invalidates the validated Pi release
  and both Teensy firmware images and requires another live bench run.
- Keep the RF PHY profile manifest relocation deferred if it would create
  unnecessary firmware churn before the demo.
- Preserve the distance note: UART drain margins are Pi-side flow control;
  range primarily affects RF retry behavior.

### WP6 — Deterministic Local And UI Tests

- Add a raw channel-1 packet replay fixture or deterministic packet generator;
  a completed `.fdp` alone is not a receiver-stream replay.
- Verify:
  - Ready with no traffic.
  - successful complete transfer and Lepton decode.
  - induced CRC failure.
  - two consecutive transfers without restarting the app.
  - unavailable/busy serial port.
  - serial disconnect.
  - stale history never replaces the current-transfer identity.
- Preserve the existing C3M local validation path.

### WP7 — Actual-Geometry Rehearsal

At the final demo geometry, complete three consecutive:

```text
capture -> channel-1 downlink -> CRC -> decode -> display
```

The first pass starts from a fresh app/bench startup. For each run, record
duration, retries, CRC result, product/transfer identity, and output path.

Acceptance:

- 3/3 current products reach CRC-complete and decode successfully.
- Each completes within 120 seconds.
- Nominal goal remains approximately 60 seconds.
- No unexplained multi-minute wait.
- Channel 0 remains usable.

Result (2026-07-09): PASS, 3/3. Products 1–3 completed in `58.573 s`,
`56.661 s`, and `56.268 s`; each received `1,100/1,100` packets with zero
web-receiver retry rounds, passed CRC and Pi/local SHA-256 equality, and decoded
to `160x120`. Mid-transfer GDS pings `37002`, `37003`, and `37004` all returned.
Evidence is recorded in `docs/C3M_LEPTON_RF_HIL_SCRATCHPAD_2026-07-09.md`.

## Implementation Order

1. Save this plan and the UI concepts.
2. Refactor/expose the receiver state without breaking the CLI.
3. Build the web app and `data/` history flow.
4. Add deterministic replay and error-state tests.
5. Update runbooks and cleanup guidance.
6. Run local regression and browser QA on the laptop/desktop demo surface.
7. Delete the obsolete Pi binary when the C3M Pi is reachable.
8. Perform the three-run live HIL rehearsal when the operator and bench are
   available.

## Stop Rules

- Do not change transport-constant values in this work.
- Do not reflash either Teensy merely to build the web app.
- Do not claim HIL repeatability from local replay tests.
- Do not request a science downlink until the web app reports `Ready` and GDS
  shows a newly stored product.
- Do not call a run successful from F Prime/GDS completion alone; the web app
  must prove CRC-complete reconstruction and Lepton decode.
- Do not allow history browsing to obscure which product is current.

## Deferred Plan — Best-Effort Thermal Image Reception

Implementation status (2026-07-10): implemented and locally validated. This
does not alter or replace the completed 2026-07-09 HIL acceptance result; live
RF packet-loss qualification remains a separate bench gate.

The web receiver now keeps the complete CRC-verified path as preferred, caps
an incomplete transfer at 90 seconds, saves a positional `.fdp.partial`, and
decodes recoverable Lepton samples without shifting bytes. Samples touched by
missing channel-1 packets are exported as CSV `NaN` / JSON `null` and rendered
white. The UI labels the result partial, shows received/missing counts, timeout
reason, thermal min/max/mean/range, and hover inspection (`No data` for missing
pixels). `run.json` retains the packet map and explicitly records
`crc_ok: false`.

Local evidence used the checked-in 38,480-byte Lepton product with packet 500
omitted: 1,099/1,100 packets, 19,182 valid pixels, 18 missing pixels, and a
viewable thermal image with the missing region shown in white. The complete,
bad-CRC, consecutive-transfer, disconnect, and packet-loss tests pass, and
`./tools/validate_local.sh --skip-demo` passes including the F Prime build and
6/6 component unit-test executables.

### Intent

A thermal picture can still be useful when a small number of RF packets are
missing. The operator should not wait indefinitely for a perfect file when a
recognizable partial image is sufficient for the demo.

### Planned Behavior

- Keep the existing 100%-received path as the preferred result:
  `Complete — CRC verified`.
- Cap the total downlink/retry window at approximately `90 seconds`.
- At the deadline, stop requesting retries and finalize the available data as
  `Partial — viewable with missing data` when enough image data exists to
  decode safely.
- Do not describe a partial file as CRC-complete. Whole-file CRC cannot pass
  when packets are missing.
- Preserve packet positions and represent missing decoded thermal samples as
  `NaN` (a known null). Configure the thermal colormap to render `NaN` as
  white, producing obvious white lines or regions without shifting or
  corrupting valid pixels.
- Report the received percentage, missing-packet count, retry count, elapsed
  time, and timeout reason in the UI and `run.json`.
- Keep the original partial payload and missing-packet metadata for engineering
  review.
- If the remaining data cannot be decoded safely, show a clear failed result
  instead of fabricating an image.

### Thermal Image Inspection

Extend the web viewer using the behavior of the Python viewer in
`external/epscorc3m` as the reference:

- Show thermal minimum, maximum, mean, and displayed color range.
- Hovering over the image shows pixel coordinates and temperature.
- Hovering over a white `NaN` sample shows `No data`, never a fabricated
  temperature.
- Preserve this known-null meaning in exported data: use `NaN` where the
  format supports it and JSON `null` where strict JSON does not.
- Keep this interaction laptop/desktop focused; mobile support is not required.

### Proposed Acceptance

- A complete transfer still passes CRC and behaves exactly as it does now.
- A controlled packet-loss test stops within the configured approximately
  90-second window and produces an honestly labeled partial image.
- Missing packet locations become `NaN` samples and appear as white
  lines/regions without moving valid pixels.
- Hover inspection reports correct temperatures for valid pixels and `No data`
  for missing pixels.
- Partial-transfer metadata is visible in the UI and saved in `run.json`.

## Deferred Plan — RF Mission Traffic Isolation

This is also follow-on work for the next development session. Neutron 2 and
EPSCoR C3M may use the same RFM23BP hardware, RadioHead stack, frequency, and
similar framing while operating near one another. A ground station must not
accept commands, telemetry, or payload data belonging to the other spacecraft.

### Intent

Add an explicit mission/network identity to the RF link so traffic is separated
before it reaches the normal F Prime or payload decoder paths:

- EPSCoR C3M ground hardware accepts only EPSCoR C3M traffic.
- Neutron 2 ground hardware accepts only Neutron 2 traffic.
- Satellite receivers likewise reject commands addressed to the other mission.
- The rule applies to channel-0 commands/telemetry, channel-1 payload data, and
  any future virtual channels.

### Design Work

- Inspect the RFM23BP driver and RadioHead header fields (`TO`, `FROM`, `ID`,
  and `FLAGS`) before choosing where the mission identity belongs.
- Decide whether RadioHead addressing is sufficient or whether the Artemis link
  frame needs a small, versioned mission/network tag of its own.
- Prefer a compact generated identifier defined once in
  `config/transport_constants.json`; do not hand-edit separate constants in the
  Pi, satellite Teensy, and ground Teensy implementations.
- Include the identity in the protected frame data so corruption cannot turn
  one mission's packet into another mission's accepted traffic.
- Reject wrong-mission frames before RF reassembly, CCSDS/GDS forwarding, or
  payload-file reconstruction whenever the selected framing layer permits it.
- Add explicit wrong-network/wrong-mission counters to both Teensy debug
  streams so intentional rejection is visible and distinguishable from CRC,
  framing, or RF loss.
- Preserve versioning space for future spacecraft or additional ground
  stations without redesigning the entire link header.
- Document that this is traffic isolation, not cryptographic authentication or
  protection against an intentional spoofing attacker.

### Compatibility and HIL Rules

- Update the ground and satellite paths symmetrically; never deploy a mixed
  tagged/untagged pair without an explicit transition mode.
- Do not silently accept untagged traffic after the migration is complete.
- Treat any header-size, segmentation, timing, or RadioHead configuration
  change as a transport-contract change requiring regeneration, rebuilds of all
  consumers, and a new HIL qualification run.
- Preserve the current validated release and tag as the rollback baseline.

### Proposed Acceptance

- A C3M receiver accepts C3M channel-0 and channel-1 traffic normally.
- A Neutron 2 receiver accepts Neutron 2 channel-0 and channel-1 traffic
  normally.
- Injected Neutron 2 frames are rejected by the C3M ground and satellite paths
  without appearing in GDS, the payload viewer, or stored data products.
- Injected C3M frames are rejected by the Neutron 2 ground and satellite paths
  under the same criteria.
- Wrong-mission counters increment while CRC/framing counters retain their
  existing meanings.
- Valid same-mission traffic still meets the demo timing and repeatability
  targets after the added tag/header is enabled.

## Compaction / Resume Point

If work is interrupted, resume from this document, then check:

```bash
git status --short --branch
git submodule status --recursive
```

Read the latest work-package status in the current task plan and continue from
the first incomplete acceptance gate. The live proof sources remain:

- `docs/C3M_LEPTON_RF_HIL_SCRATCHPAD_2026-07-09.md`
- `docs/EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`
- `docs/HIL_BENCH_HANDOFF_2026-07-09.md`
