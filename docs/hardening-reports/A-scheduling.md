# Worker A — Scheduling Hardening

## Summary

Hardened the demo-critical scheduling command path in `MissionManager` and `ScienceManager`, added the MissionManager-to-ScienceManager cancel path, and added focused unit tests for `ScienceManager` and `CommsManager`.

## Files Changed

- `ArtemisRpiTeensy_N2/Components/MissionManager/MissionManager.fpp`
- `ArtemisRpiTeensy_N2/Components/MissionManager/MissionManager.hpp`
- `ArtemisRpiTeensy_N2/Components/MissionManager/MissionManager.cpp`
- `ArtemisRpiTeensy_N2/Components/MissionManager/test/ut/*`
- `ArtemisRpiTeensy_N2/Components/ScienceManager/ScienceManager.fpp`
- `ArtemisRpiTeensy_N2/Components/ScienceManager/ScienceManager.hpp`
- `ArtemisRpiTeensy_N2/Components/ScienceManager/ScienceManager.cpp`
- `ArtemisRpiTeensy_N2/Components/ScienceManager/CMakeLists.txt`
- `ArtemisRpiTeensy_N2/Components/ScienceManager/test/ut/*`
- `ArtemisRpiTeensy_N2/Components/CommsManager/CMakeLists.txt`
- `ArtemisRpiTeensy_N2/Components/CommsManager/test/ut/*`
- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.fpp`
- `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.local-demo.fpp`

## Design Choices

- Schedule delay guard lives in `MissionManager`: `0` and `>300` reject with `VALIDATION_ERROR` and `MissionCommandRejected`.
- Capture duration guard lives in `ScienceManager`: `0` and `>120` reject with `VALIDATION_ERROR` for commands, or warning event + ignore for the port path.
- Cancel port shape: `MissionManager.cancelRequestOut: Svc.Ping -> ScienceManager.cancelRequestIn: Svc.Ping`.
  - Reason: no new shared type was needed; the cancel signal has no payload requirement beyond "cancel now".
  - `ScienceManager` logs `CollectionCancelled(remainingSeconds)`, clears `m_pendingDelaySeconds`, and writes telemetry.
- `ENTER_BASE_MODE` and `CANCEL_COLLECTION` both return to `BASE` through the same transition validation path and send the cancel port.
- `SCIENCE_CAPTURE(durationSeconds)` is now one-shot only and does not persist into `m_captureDurationSeconds`; `CONFIGURE_CAPTURE_DURATION` remains the default-duration command.
- Default capture duration is now `30` seconds.

## Unit Tests Added / Extended

`MissionManager`
- `RejectsInvalidScheduleCommandInputs`
- `RejectsInvalidCommandTransition`
- `EnterBaseModeCancelsPendingCollection`
- `CancelCollectionReturnsBaseAndCancels`

`ScienceManager`
- `DefaultDurationIsThirty`
- `ScheduledCountdownFiresExactlyOnce`
- `RejectsZeroDelayAndBadDurations`
- `CancelClearsCountdown`
- `ScienceCaptureDoesNotPersistDuration`

`CommsManager`
- `RejectsDownlinkWithoutScience`
- `RequestsScienceDownlinkAndCompletionClearsState`
- `DownlinkFailureReturnsBase`
- `IgnoresPayloadStatusWhenInactive`
- `AdapterStatusPollingAndRssiPing`
- `RunPublishesHealthFromLinkState`

## Validation Evidence

Environment:

```bash
. ArtemisRpiTeensy_N2/fprime-venv/bin/activate
```

Default HIL profile:

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f && fprime-util build
```

Result: PASS, command exited 0.

Local-demo profile, matching `tools/run_neutron2_local_demo.sh`:

```bash
fprime-util generate -f --build-cache build-neutron2-local-demo "-DNEUTRON2_TOPOLOGY_PROFILE=local-demo"
fprime-util build --build-cache build-neutron2-local-demo
```

Result: PASS, command exited 0.

Unit tests:

```bash
fprime-util generate --ut -f
fprime-util build --ut
fprime-util check
```

Test summary:

```text
1/6 Test #1: Components_UartChannelMux_ut_exe ...........   Passed
2/6 Test #2: Components_PayloadDownlinkManager_ut_exe ...   Passed
3/6 Test #3: Components_MissionManager_ut_exe ...........   Passed
4/6 Test #4: Components_ScienceManager_ut_exe ...........   Passed
5/6 Test #5: Components_CommsManager_ut_exe .............   Passed
6/6 Test #6: Components_EpsAdapter_Artemis_ut_exe .......   Passed
100% tests passed, 0 tests failed out of 6
```

Note: for this pinned `fprime-util` version, the working UT command is `fprime-util check` after `generate --ut`; `fprime-util check --ut` is not accepted.

## Follow-ups Deliberately Skipped

- Did not delete or merge `topology.local-demo.fpp`; Worker E owns topology de-forking.
- Did not add parameter persistence for capture duration; Worker F owns params/FPP ops hygiene.
- Did not touch CI, Teensy firmware, Pi provisioning, or broader docs beyond this report.
