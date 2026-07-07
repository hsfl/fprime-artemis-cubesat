# Worker F - Params and FPP Ops Hardening

Date: 2026-07-06
Branch observed: `neutron2-develop`

## BLUF

- Added `ScienceManager.CAPTURE_DURATION_SECONDS` as a persistent boot-default parameter with default `30`.
- Kept `CONFIGURE_CAPTURE_DURATION` as Worker A left it: a volatile runtime override, not a parameter conversion.
- Added FPP `throttle`, `update on change`, and RSSI low limits where they reduce repeat traffic without hiding judge-visible progress.
- Production build and component unit tests pass.
- Full `tools/validate_local.sh` reached the automated local demo, then failed in this managed sandbox because local socket/IPC binds are denied.

## Param Design

`ScienceManager.fpp` now declares:

```fpp
param CAPTURE_DURATION_SECONDS: U32 default 30
```

Runtime behavior:

- Boot default: `CAPTURE_DURATION_SECONDS`.
- Runtime override: `CONFIGURE_CAPTURE_DURATION(durationSeconds)` remains volatile.
- Ground persistence flow: `PRM_SET ScienceManager.CAPTURE_DURATION_SECONDS`, then `PRM_SAVE ScienceManager.CAPTURE_DURATION_SECONDS`.
- Component init path: deployment calls topology `loadParameters()`, then `ScienceManager::preamble()` seeds `m_captureDurationSeconds` from the loaded param.
- Live `PRM_SET` path: F Prime autocode calls `ScienceManager::parameterUpdated`, which reseeds from the parameter.
- Guard behavior: if the param value is valid by the existing duration guard (`1..120` seconds), it becomes the boot default. If invalid, ScienceManager falls back to `30` and emits `ScienceCommandRejected(reason=2, value=<bad>)`.

## PrmDb Facts

- `ArtemisRpiTeensyDeployment/Top/topology.fpp` has `param connections instance FileHandling.prmDb`.
- Repo-local F Prime `Svc/Subtopologies/FileHandling/FileHandling.fpp` instantiates `FileHandling.prmDb`.
- FileHandling config calls `FileHandling::prmDb.configure("PrmDb.dat")`.
- FileHandling parameter-read phase calls `FileHandling::prmDb.readParamFile()`.
- `ArtemisRpiTeensyDeploymentTopology.cpp` calls `loadParameters()` during startup after topology configuration.
- Effective parameter file name/path is relative to the runtime working directory: `PrmDb.dat`.

## FPP Ops Changes

### Event Throttles

- `ScienceManager.ScienceCommandRejected`: `throttle 5`
- `MissionManager.ModeUpdateRejected`: `throttle 5`
- `MissionManager.MissionCommandRejected`: `throttle 5`
- `CommsManager.DownlinkFailed`: `throttle 5`
- `CommsManager.LinkStateUpdated`: `throttle 10`
- `CommsAdapter_TeensyRfm23.RequestHandled`: `throttle 10`
- `PayloadDownlinkManager.PayloadDownlinkFailed`: `throttle 5`
- `PayloadDownlinkManager.PayloadRetryRequested`: `throttle 10`
- `PayloadDownlinkManager.PayloadStatus`: `throttle 10`
- `EpsAdapter_Artemis.PduRequestFailed`: `throttle 5`
- `EpsAdapter_Artemis.PduRequestTimedOut`: `throttle 5`
- `UartChannelMux.FrameDropped`: `throttle 10`

### Update On Change

- `ScienceManager`: `CaptureDurationSeconds`, `CollectionCount`
- `MissionManager`: `CurrentMode`, `LastScheduledDelaySeconds`, `PingCount`
- `CommsManager`: `LinkState`, `PendingScienceBytes`, `RssiDbm`
- `CommsAdapter_TeensyRfm23`: `LinkState`, `RssiDbm`, `RfRxPackets`, `RfTxPackets`, `RfTxDrops`
- `PayloadDownlinkManager`: `PayloadState`, `TransferId`, `ProductId`, `TotalBytes`, `TotalPackets`, `LastError`
- `UartChannelMux`: `FrameDrops`

### Channel Limits

- `CommsManager.RssiDbm`: `low { yellow -100, orange -110, red -120 }`
- `CommsAdapter_TeensyRfm23.RssiDbm`: `low { yellow -100, orange -110, red -120 }`

These RSSI limits are the only limits added. They match the existing dBm semantics and demo link-state ranges in the comms adapter path.

## Skipped Ops Changes

- No `update on change` on judge-visible ticking/progress channels: `ScienceManager.PendingDelaySeconds`, `MissionManager.ModeHeartbeat`, `CommsManager.LinkHeartbeat`, and payload progress counters remain tick/progress-visible.
- No throttles on rare high-value operator activity events such as `ModeChanged`, `CollectionScheduled`, `CollectionTriggered`, `DownlinkRequested`, `DownlinkFinished`, `PayloadDownlinkStarted`, `PayloadDownlinkComplete`, `CaptureDurationConfigured`, or `LinkRssiPing`.
- No SoH/thermal/voltage/current limits were added. The available docs/components still describe placeholder/demo values and TODO bench ranges, not validated flight bounds.
- No RSSI high limits were added. Stronger RSSI is not a fault condition in the current demo, and no saturation threshold is documented as an operational bound.

## Unit Test Changes

- Added ScienceManager UT: valid param seeds capture duration at init.
- Added ScienceManager UT: invalid param falls back to `30` and emits rejection event.
- Adjusted MissionManager and ScienceManager telemetry assertions only where `update on change` intentionally suppresses duplicate telemetry samples.

## Validation Evidence

### Production Build

Command:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util generate -f && fprime-util build
```

Result: PASS

### Unit Tests

Command:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
cd ArtemisRpiTeensy_N2
fprime-util build --ut && fprime-util check
```

Result: PASS

Evidence:

```text
100% tests passed, 0 tests failed out of 6
Total Test time (real) =   1.46 sec
```

### Full Local Validation

Command:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
./tools/validate_local.sh
```

Result: FAIL/BLOCKED in managed Codex sandbox during automated local demo.

Stages passed before the sandbox failure:

- transport drift checks
- generated transport header checks
- Python local-emulation tests
- unified topology production build
- UT build/check: `100% tests passed, 0 tests failed out of 6`

Tail:

```text
Internal ctest changing into directory: /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/build-fprime-automatic-native-ut
Test project /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/build-fprime-automatic-native-ut
    Start 1: Components_UartChannelMux_ut_exe
1/6 Test #1: Components_UartChannelMux_ut_exe ...........   Passed    0.52 sec
    Start 2: Components_PayloadDownlinkManager_ut_exe
2/6 Test #2: Components_PayloadDownlinkManager_ut_exe ...   Passed    0.39 sec
    Start 3: Components_MissionManager_ut_exe
3/6 Test #3: Components_MissionManager_ut_exe ...........   Passed    0.39 sec
    Start 4: Components_ScienceManager_ut_exe
4/6 Test #4: Components_ScienceManager_ut_exe ...........   Passed    0.38 sec
    Start 5: Components_CommsManager_ut_exe
5/6 Test #5: Components_CommsManager_ut_exe .............   Passed    0.39 sec
    Start 6: Components_EpsAdapter_Artemis_ut_exe
6/6 Test #6: Components_EpsAdapter_Artemis_ut_exe .......   Passed    0.39 sec

100% tests passed, 0 tests failed out of 6

Total Test time (real) =   2.92 sec
[validate-local] running automated local demo sequence
[neutron2-demo] dictionary: /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
[neutron2-demo] capture dir: /tmp/neutron_payload_captures
[neutron2-demo] logs: /Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2/tools/logs/neutron2_local_demo_20260706_144216
[neutron2-demo] started local emulator pid=22797; GDS: http://127.0.0.1:5061
[neutron2-demo] started payload viewer pid=22798; viewer: http://127.0.0.1:8063
Timed out waiting for fprime-gds on 127.0.0.1:5061
```

Sandbox failure evidence from demo logs:

```text
zmq.error.ZMQError: Operation not permitted (addr='ipc:///tmp/fprime-server-out')
PermissionError: [Errno 1] Operation not permitted
```
