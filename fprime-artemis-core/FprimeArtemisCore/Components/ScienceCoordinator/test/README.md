# ScienceCoordinator verification

Milestone M2 component evidence, 2026-11-11 (HST).

The real F′ v4.4.2 component, generated FPP ports, and production cache/downlink
engine passed **11/11 Linux ARM64 tests**. The test links the same cache storage
provider as the component; the host storage allocation does not establish the
Teensy's linker-bank placement or runtime RAM access.

Covered behavior:

- Mission admission, fresh READY state, bounded schedule delay, and BUSY refusal.
- Correlated capture request and full **196,608-byte** staging/CRC commitment.
- Refusal of corrupted staging and no downlink from an invalid cache.
- Duplicate COMMIT during transmission re-acknowledges without changing job state.
- Late ERROR after sender completion cannot turn a successful send into failure.
- Committed cache remains readable/downlinkable when Pi is UNKNOWN and after cancel.
- Timed-out capture ignores a late product descriptor.
- Four consecutive local transmit failures stop honestly while retaining the cache.
- Failed staging invalidates partial data and flushes pending messages to ABORT.
- New scheduling retires the previous transfer's delayed repair lease.
- Explicit restaging after FC reset learns size/CRC from the matching Pi BEGIN.

Tests are registered with F′ `register_fprime_ut(... UT_AUTO_HELPERS)`.
Configure a separate native Linux UT build with the project's
`twin/native-linux.cmake`, `BUILD_TESTING=ON`,
`FPRIME_ENABLE_FRAMEWORK_UTS=OFF`, `FPRIME_ENABLE_AUTOCODER_UTS=OFF`, and
library location `tools/cross`. Build and run the specific target:

```sh
cmake --build <pi-ut-build> --target FprimeArtemisCore_Components_ScienceCoordinator_ut_exe --parallel 8
<pi-ut-build>/bin/native-linux/FprimeArtemisCore_Components_ScienceCoordinator_ut_exe
```

The twin-image validation used `/build/pi/component-ut` and wrote JUnit to
`/data/science-coordinator-ut.xml` in the payload-computer service's run
directory. Set a separate `GCOV_PREFIX` directory per executable when running
several instrumented component binaries: their linked OSAL implementations can
otherwise write incompatible profiles to the same build-cache `.gcda` path.

These component tests do not prove end-to-end GenericHub/radio operation,
Yamcs receipt, physical RF performance, target memory accessibility, or flight
qualification. Those remain separate integration/bench evidence.
