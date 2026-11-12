# C3M local twin: Yamcs command-to-file walkthrough

Verified 2026-11-11 HST on `codex/artemis-local-twin`.

## Verified file compatibility

**The repaired twin's downloaded FDP works with the unchanged existing C3M app.**
Yamcs delivers the raw file and verified receipt; image/temperature analysis
stays in your existing mission ops software.

The Pi producer now preserves the established Lepton container **and record**
ID `0x10027000` (component base plus local ID 0). Its link buffer manager moved
to unused `0x10034000`, avoiding an ID collision. No C3M application, decoder,
header recognition or source code was changed. The mismatched `0x10031000`
identity belonged to the earlier split-core deployment; previously archived
pre-fix files retain that ID and are not made compatible retroactively.

Products **16, 17, 18** (three nominal cycles) and **19** (Pi-off packet repair)
from run `20261112T055046Z-d8611b` were captured, committed on Pi,
staged in full FC RAM, transmitted and downloaded from Yamcs. Explicit offline
verification then used the canonical C3M app's real `ReceiverController`, local
file replay, unchanged classifier, default decoder and History finalization.
The existing macOS Python 3.11 / F′ tools **4.2.1** environment decoded the new
F′ 4.4.2 producer's files using its matching Pi dictionary. The app produced
complete Lepton History entries, **120×160 CSV (19,200 samples)** and PNG output;
every temperature matched the recorded capture fixture. Source hashes before
and after confirmed the app/decoder remained unchanged. All four downloaded
files passed this complete app path. The Pi-off file required one repair round
from retained FC RAM.

[Exact build/file/app evidence](../../development/evidence/ARTEMIS_C3M_ID_COMPATIBILITY_2026-11-11.json)
records the regenerated dictionaries, source hashes, existing decoder launcher
and per-product results.

The original failure was the public product ID, not a demonstrated decoder
version incompatibility. Use the **producing Pi dictionary** explicitly. These
results cover complete Lepton files in this recorded-camera twin; they do not
qualify physical camera operation or every partial/Boson workflow.

Browser proof also passed: the unchanged app was opened with product 19's
Yamcs-downloaded file and showed Payload complete/CRC passed, thermal image,
statistics and a Complete History entry. Its local replay display uses product
1/transfer 42; the source download remains product 19. See the
[current architecture](../../architecture/ARTEMIS_LOCAL_TWIN.md) for ownership,
interfaces and storage boundaries.

## 1. Start the local stack

From `zephyr_dev` on this Mac:

```sh
python3 tools/twin/twin.py up
python3 tools/twin/twin.py status
```

Use `python3 tools/twin/twin.py build` first after changing compiled flight/Pi
source or on a fresh installation. Open http://127.0.0.1:8090 and select
`fprime-project`, processor `realtime`.

This operates Docker processes and virtual power/UART/RF, not physical hardware.
A fresh twin starts in STANDBY with the Pi payload application OFF. Existing
running sessions can already be in BASE. Use current telemetry to determine state.
The Pi camera backend is explicitly the recorded Lepton fixture.

## 2. Power on Pi and confirm readiness

Open **Commanding → Send a command**. Search for the command suffixes below;
Yamcs adds the deployment/component namespace to their full names.

Send `missionApp.ENTER_BASE_MODE` with no arguments. BASE enables the virtual
Pi power GPIO; the supervisor starts the Linux payload application. Wait for:

- `missionApp.CurrentMode = BASE`.
- `pcLinkManager.PayloadState = READY`, with fresh updates/heartbeats.
- `rpiPowerManager.RpiState` showing its powered/ready state.

Use the Telemetry parameter browser to find these component/channel suffixes.
Send `missionApp.PING` and `missionApp.EMIT_SOH_SNAPSHOT` if desired to confirm
command/telemetry responsiveness. Mode alone does not prove the payload is ready.

The pinned adapter does not implement Yamcs `CommandComplete` verification.
Check the actual F′ dispatcher `OpCodeCompleted` event in Events/F′ Events and
the requested telemetry transition. `OpCodeError` means the command failed.
A command appearing in command history only proves submission.

## 3. Schedule and capture

Send these commands, one at a time:

| Command | Arguments for a short rehearsal |
|---|---|
| `scienceCoordinator.CONFIGURE_CAPTURE_DURATION` | `durationSeconds = 1` |
| `missionApp.SCHEDULE_COLLECTION` | `delaySeconds = 2` |

Watch `scienceCoordinator.JobState` move through SCHEDULED → CAPTURING → STAGING.
The duration is a collection interval, after which Pi selects a recorded frame.
Pi writes the FDP persistently through DpWriter. It reports PRODUCT_COMMITTED
only after the matching write result; successful command submission is not
successful file creation.

`missionApp.CANCEL_COLLECTION` cancels a pending/active transaction. Wait for
CANCELLED and fresh Pi READY before scheduling again. The driver completion
barrier prevents an old cancellation from cancelling the replacement capture.

## 4. Wait for the complete FC cache

Staging is automatic after the committed-product report; no extra cache command
is required. Pi sends acknowledged chunks of at most 200 bytes. FC validates
correlation, size, offsets and final whole-file CRC before marking its cache valid.

Wait for all of:

- `scienceCoordinator.JobState = CACHE_READY`.
- `scienceCoordinator.CacheValid = true` (Yamcs may display `True`).
- `CacheReceivedBytes = ProductBytes`.
- Record `ProductId` / `CachedProductId` and `ProductBytes`.

The cache capacity is **196,608 bytes (192 KiB)**. Current single-frame rehearsal
products are **38,486 bytes**; capacity is not the per-file length.
Do not request downlink during CAPTURING or STAGING.

Optional retained-cache demonstration: after CACHE_READY, send
`missionApp.ENTER_STANDBY_MODE`. This stops the Pi application. The committed FC
cache remains valid, so the following downlink still works. Do not turn Pi off
before the entire file is staged.

## 5. Downlink the cached file

Send `missionApp.REQUEST_SCIENCE_DOWNLINK` with no arguments.
Watch JobState DOWNLINKING → SENT and the radio/science packet counters.
The FC reads its RAM cache, including for repair; Pi is not a file server during
downlink. Channel 1 carries science and channel 0 retains command/telemetry.

**SENT is sender completion, not verified delivery.** Ground must receive the
whole file and validate CRC, then upload it to Yamcs and independently GET the
same bytes. Only then is its verified receipt published.

## 6. Download from Yamcs and hand off

Open **Storage → artemis-science**. Select the current run/product, not an older
similarly named object. Download:

1. `lepton-<run-id>-<product-id>-<transfer-id>-<sha-prefix>.fdp`.
2. Its matching `.fdp.receipt.json`.

Check the receipt's `product_id`, `bytes`, `crc16`, `sha256` and
`verified_ground_receipt = true`. On macOS, `shasum -a 256 FILE.fdp` must match
its SHA. The receipt also provides the exact REST `download_path`.

The matching **producing Pi dictionary** is:

```text
.twin-runs/<run-id>/ground/dictionaries/pi.json
```

Use that dictionary, not the FC dictionary or an automatically selected legacy
Pi-release dictionary. Independent Yamcs GET copies also remain at:

```text
.twin-runs/<run-id>/ground/archived/<exact-object-name>.fdp
```

The existing C3M app's file entry point is `--replay-fdp FILE --dictionary DICT
--no-open` (replays a local file through its parser without a serial device).
This works for newly captured files after the producer ID repair, with the
matching dictionary. Do not use `./tools/c3m` for this handoff: that launcher
also starts the legacy GDS/hardware receiver workflow.

## Automation and evidence

For the already verified command-to-file rehearsal:

```sh
python3 tools/twin/twin.py demo --captures 3
python3 tools/twin/twin.py demo --captures 1 --pi-off-repair
```

These commands check file delivery/integrity. To separately exercise the
unchanged analysis app, use its existing Python environment with `fprime-dp`
on PATH and run the explicit offline acceptance helper:

```sh
python tools/twin/verify_c3m_handoff.py \
  --app-root /path/to/existing/epscorc3m \
  --fdp /path/to/Yamcs-downloaded.fdp \
  --dictionary .twin-runs/RUN_ID/ground/dictionaries/pi.json \
  --output .twin-runs/RUN_ID/new-app-verification
```

Use a new output directory. The helper runs the existing app, verifies complete
History, original FDP SHA, sample/CSV shape, fixture temperatures and PNG, and
records source hashes in `compatibility.json`. It opens no physical serial
port, web UI or hardware connection. It is not part of the Yamcs runtime.
Full source/ground/Yamcs evidence is in the milestone record and compact
`docs/development/evidence/ARTEMIS_LOCAL_TWIN_ACCEPTANCE_2026-11-11.json`.
Stop the stack with `python3 tools/twin/twin.py down`; persistent volumes remain.
