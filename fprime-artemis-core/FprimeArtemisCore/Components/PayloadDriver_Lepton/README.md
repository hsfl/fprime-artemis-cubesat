# Lepton capture and cancellation

The Pi driver selects the recorded sample, synthetic, or libuvc camera backend
through `LeptonCamera`. A 1..3600 second collection uses `CaptureWindow`; the
driver's active task continues handling health and cancellation while the
interval is running, then packages one 160x120 U16 frame as a real F′ data
product. The existing frame read itself retains its bounded five-second camera
timeout.

## Local twin milestone — active cancellation (2026-11-12)

`PayloadLinkService.cancelOut → PayloadManager.cancelIn → driverCancelOut →
PayloadDriver_Lepton.cancelIn` uses async inputs on both active tasks. This keeps
the driver's capture state serialized and avoids callbacks into the service
while its guarded request lock is held.

Cancellation completion uses a separate local signal:
`PayloadDriver_Lepton.cancelDoneOut → PayloadManager.driverCancelDoneIn →
cancelDoneOut → PayloadLinkService.cancelDoneIn`. The driver sends this signal
after the cancellation's failure descriptor and readiness report. Ordinary
success/failure descriptors never emit it. The service retains the old capture
until this explicit acknowledgment, so a success or failure already pending at
ABORT cannot release ownership before the queued cancellation reaches the
driver. The FC/Pi wire protocol is unchanged.

Cancelling a collection interval ends it as soon as the cancellation is
dispatched, publishes one failure descriptor (zero product bytes), then reports
the current readiness. A streaming camera becomes READY without waiting for the
original collection deadline. A cancellation that races with already completed
work also returns a failure descriptor and readiness, allowing the service to
release its cancellation barrier.

If DpWriter already owns a submitted product, the driver retains its filename
correlation and BUSY state until that write acknowledges or the existing
10-scheduler-tick timeout expires. It suppresses the cancelled write's success
descriptor, then reports one cancellation failure and readiness. The on-disk
cancelled file can remain; it is never offered to the payload service as a new
committed product. Cancellation does not free or reuse a DpWriter-owned buffer.

Writer filenames use the container timestamp as their identity. This driver
assigns strictly increasing microsecond tags over its lifetime, including when
the supplied clock is fixed or moves backward. A late notification for an old
filename cannot complete a newer collection after a timeout. This uniqueness is
within one driver process; restarting the process does not preserve that counter.

The focused F′ unit tests exercise a real synthetic-frame serialization and
successful follow-on product after a long collection cancellation, pending-write
cancellation, timeout followed by a stale callback, and completion racing with
cancellation. They advance the collection window directly to avoid wall-clock
sleeps. Camera hardware and electrical effects are outside this evidence.
Every cancellation case also asserts exactly one explicit acknowledgment after
the driver has cleared the collection/write and reported failure/readiness;
ordinary successful and failed requests assert no cancellation acknowledgment.

Verification result: all four F′ component tests passed in the isolated ARM64
Linux Docker build with address, undefined-behavior, and leak sanitizers enabled
(exit 0). `git diff --check` passed for the owned component/deployment paths.

Build and run using the configured isolated native Linux UT volume (macOS Docker
Desktop, or the same Docker commands in Windows WSL):

```sh
docker run --rm -v "$PWD:/workspace:ro" -v artemis-eps-native-ut:/build/ut \
  -w /workspace artemis-local-twin:local cmake --build /build/ut \
  --target FprimeArtemisCore_Components_PayloadDriver_Lepton_ut_exe --parallel 4
docker run --rm -v artemis-eps-native-ut:/build/ut artemis-local-twin:local \
  /build/ut/bin/native-linux/FprimeArtemisCore_Components_PayloadDriver_Lepton_ut_exe
```

This isolated cache is configured with `BUILD_TESTING=ON`, the repository's
`twin/native-linux.cmake`, and F′ framework/autocoder UTs disabled. It does not
modify the running twin's `/build/pi` cache.
