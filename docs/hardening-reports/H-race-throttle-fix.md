# Worker H - Race and Throttle Fix

## Summary

Fixed the adversarial review findings by moving manager state-mutating inputs onto each active component queue and splitting human command rejections from storm-capable warning events.

## Async Port Conversions

All converted ports use the F Prime default async queue-full behavior: `assert`.

Rationale:
- Repo-local F Prime docs state async port calls are queued and dispatched on the active component thread.
- Repo-local dictionary docs list async queue-full behaviors and define `assert` as system assert on full.
- `instances.fpp` gives these active managers queue size `Default.QUEUE_SIZE = 10`.
- The converted traffic is 1 Hz tick plus human/operator/status traffic, so queue size 10 is sane. Dropping cancel, mode, or science status messages would be worse than a visible overload assert.

| Component | Port | Before | After | State touched |
| --- | --- | --- | --- | --- |
| ScienceManager | `run` | sync input | async input | pending delay, telemetry, payload/mode outputs |
| ScienceManager | `requestIn` | sync input | async input | pending delay, telemetry, collection event |
| ScienceManager | `cancelRequestIn` | sync input | async input | pending delay, telemetry, cancel event |
| ScienceManager | `payloadStatusIn` | sync input | async input | collection count, telemetry, product/mode outputs |
| MissionManager | `run` | sync input | async input | heartbeat telemetry |
| MissionManager | `modeUpdateIn` | sync input | async input | current mode, telemetry, mode events |
| CommsManager | `run` | sync input | async input | adapter poll, SOH output, telemetry from mutable state |
| CommsManager | `linkStatusIn` | sync input | async input | link state, link-state event |
| CommsManager | `scienceReadyIn` | sync input | async input | pending science product fields |
| CommsManager | `adapterStatusIn` | sync input | async input | link state, poll flags, RSSI ping flag |
| CommsManager | `rssiStatusIn` | sync input | async input | RSSI state |
| CommsManager | `payloadDownlinkStatusIn` | sync input | async input | downlink state, pending science fields, mission mode output |

String/com sizing:
- Async serialized science strings use `sourcePath: string size 192`.
- The relevant science ports carry four small scalar fields plus the 192-byte string and size metadata.
- Pinned framework default `FW_COM_BUFFER_MAX_SIZE` is 512, so the serialized science port calls fit with margin.

## Throttle Decisions

| Event | Decision | Reason |
| --- | --- | --- |
| `MissionManager.MissionCommandRejected` | removed throttle | operator command rejection, human-rate-limited |
| `ScienceManager.ScienceCommandRejected` | removed throttle | operator/request validation rejection, human-rate-limited |
| `CommsManager.CommsCommandRejected` | added unthrottled event | separates no-science command rejection from downlink transport failure |
| `MissionManager.ModeUpdateRejected` | kept throttle 5 | reachable from service/port paths |
| `CommsManager.DownlinkFailed` | kept throttle 5 | payload/status failure path can storm |
| `CommsManager.LinkStateUpdated` | kept throttle 10 | polling/status path can storm |
| `PayloadDownlinkManager.PayloadDownlinkFailed` | kept throttle 5 | retry/status path can storm |
| `PayloadDownlinkManager.PayloadRetryRequested` | kept throttle 10 | retry path can storm |
| `PayloadDownlinkManager.PayloadStatus` | kept throttle 10 | progress/status path can storm |
| `CommsAdapter_TeensyRfm23.RequestHandled` | kept throttle 10 | polling/transport path can storm |
| `EpsAdapter_Artemis.PduRequestFailed` | kept throttle 5 | adapter/transport path can storm |
| `EpsAdapter_Artemis.PduRequestTimedOut` | kept throttle 5 | adapter/transport path can storm |
| `UartChannelMux.FrameDropped` | kept throttle 10 | byte-stream parser path can storm |

Added one-line FPP comments near kept throttles documenting the storm-capable reason.

## Tests Changed

- Updated MissionManager, ScienceManager, and CommsManager UTs to call `doDispatch()` after async port invocations.
- Added `ScienceManagerTester::testQueuedCancelAtFinalTickBoundaryPreventsCollection`.
  - Drives a pending collection to one second remaining.
  - Queues cancel before the final tick dispatch.
  - Dispatches cancel, then final tick.
  - Asserts no `payloadRequestOut` and no `COLLECTING` mode output after cancel.
- Updated CommsManager no-science downlink rejection expectation from throttled `DownlinkFailed` to unthrottled `CommsCommandRejected`.

## Validation Evidence

Environment:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
```

From `ArtemisRpiTeensy_N2`:

```bash
fprime-util generate -f
fprime-util build
fprime-util build --ut
fprime-util check
```

Result:
- `generate -f`: PASS
- deployment build: PASS
- UT build: PASS
- `fprime-util check`: PASS, 6/6 UT executables passed

From repo root:

```bash
./tools/validate_local.sh
```

Result: FAIL only at automated local-demo socket/IPC startup under sandbox.

Passed before the failure:
- shared Teensy firmware drift check
- generated transport header checks
- Python local-emulation tests
- unified topology generate/build
- component UT suite, 6/6 passed

Failure evidence:
- `fprime-gds` ZMQ IPC bind failed with `Operation not permitted` on `ipc:///tmp/fprime-server-in` and `ipc:///tmp/fprime-server-out`.
- payload viewer HTTP bind failed with `PermissionError: [Errno 1] Operation not permitted`.
- `validate_local.sh` then timed out waiting for `fprime-gds` on `127.0.0.1:5061`.

No commit made.
