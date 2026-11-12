# Pi payload science link

Milestone M2 implementation started 2026-11-11 (HST). This is the Pi side of
the split Artemis science path; the FC remains mission and radio authority.

## Contract

`Types/TwinScience.fpp` defines one fixed-size typed message on
`FcPcLink.SCIENCE = 6`. Both peers compile the same definition. Fields include
operation, link session, capture request, product, cache generation, offset,
size, CRC16, chunk length, requested duration, error, and 200 data bytes.
No filesystem path crosses this link.

The service accepts one capture at a time, checks fresh local readiness,
deduplicates an identical current session/request, and rejects stale request
numbers, busy service, conflicting duration, or invalid duration. It calls the
existing PayloadManager with the duration, rather than treating a collection
identifier as a duration. Pi capture stays BUSY for a bounded 1–3600 second
monotonic collection interval, then selects one final frame and writes one FDP.
The interval is nonblocking so health pings continue to run.

`PayloadManager.statusOut` receives only the existing driver's matching
DpWriter completion/CRC descriptor or an explicit failure. The service reports
PRODUCT_COMMITTED only after that successful descriptor and persistence of its
bounded numeric product catalog. `ARTEMIS_PAYLOAD_CATALOG` optionally selects the index
path; the default is `payload-link.catalog` in the Pi working directory, with
one small `<index>.records/<numeric-id>.product` sidecar per retained committed
product. These records originate only in successful DpWriter descriptors;
DpCatalog continues to own the FDP files. F′ v4.4.2 DpCatalog has no ID lookup
port, so this bounded staging index avoids changing its API or scanning its
directory. The persistent latest record allocates monotonically increasing
public product handles even when the driver-local counter restarts at one.

`ARTEMIS_PAYLOAD_PRODUCT_DIR` selects the local DpWriter directory (default
`./DpCat`). Every lookup canonicalizes the descriptor path, confines it to
that directory, and verifies a regular file, exact size, and CRC16 before
staging. A remote request supplies only a numeric product ID. Lookup reads one
metadata record of at most 1,400 bytes and one FDP of at most 192 KiB, with
fixed working memory independent of the number of retained products. Deleted,
modified, missing, and escaped paths fail closed. An older committed product
remains reusable after a newer capture succeeds, fails, or is cancelled, and
after a Pi process restart. Existing single-product metadata migrates lazily;
products with no persisted staging record cannot be inferred from filenames.
The latest identity is reserved before its sidecar is written, so a process
stop between atomic renames cannot reuse an old handle. This is process-restart
recovery; sudden-power-loss durability/fsync qualification remains separate.

FC requests staging. Pi sends BEGIN, sequential chunks of at most 200 bytes,
then COMMIT. The peer's ACK offset is the next required byte. Identical lost
requests/ACKs are handled without duplicate capture or speculative progress.
Every stage frame must match session, request, product, and cache generation.
Each unacknowledged frame gets four retries at two-second intervals. A new FC
session can explicitly restage any indexed retained product. A replay of the
completed stage key returns CACHE_READY without reopening the file; older
request/generation keys are rejected. CACHE_READY closes the
file; radio send and repair subsequently require no Pi file reads.

Inputs cache/enqueue work under the component guard. Science sends and capture
requests originate only from the 100 Hz `run` callback. The PC deployment's
base clock is 10 ms with divisors 100, 1, 400 for its 1 Hz, 100 Hz, and 0.25 Hz
groups. Command sequencing now runs at 1 Hz. The shared transport adapter must
serialize complete frames across the 1 Hz heartbeat and 100 Hz science writers.

Error codes: 0 none, 1 busy, 2 not ready, 3 invalid request, 4 stale request,
5 capture failure, 6 file/index I/O, 7 retry timeout, 8 invalid ACK, 9 aborted,
10 bounded-queue overflow. ABORT stops staging and discards the transaction's
later capture result. For an active capture it sends `cancelOut` once through
the asynchronous manager/driver cancellation path and retains transaction
ownership until the driver's dedicated local `cancelDoneIn` barrier. The driver emits
that signal only after its cancellation failure descriptor and readiness, or
a pending DpWriter acknowledgment/timeout has drained. Both pre-cancel pending
results and later positive/zero product callbacks are discarded while
cancelling; neither can release ownership. A new capture requires explicit
cancel completion and fresh READY, so an old callback cannot complete a new
request. Committed Pi files are retained.

## Recorded-frame configuration

Set `ARTEMIS_TWIN=1`, `LEPTON_CAMERA_BACKEND=sample`, and an explicit readable
`C3M_LEPTON_SAMPLE_CSV`. The twin entry point fails before topology startup
when this configuration or its explicit UART endpoint is missing. The existing
sample camera backend rejects a missing/invalid CSV and never substitutes a
synthetic frame when `sample` was requested. Outside the twin, the existing
explicit UVC/synthetic/auto selection remains available.

## Validation

From `fprime-artemis-core`:

```sh
cmake -S FprimeArtemisCore/Components/PayloadLinkService/test -B /tmp/artemis-payload-link-tests
cmake --build /tmp/artemis-payload-link-tests
ctest --test-dir /tmp/artemis-payload-link-tests --output-on-failure
```

Observed 2026-11-11: 1/1 host test executable passed. It checks monotonic
capture timing and duration bounds, exact 196,608-byte reads through 200-byte
chunks, repeat reads, size/CRC/missing-file refusal, index persistence/reload,
malformed metadata, older-product/restart lookup, driver-ID reuse prevention,
deleted latest-product recovery, directory escape/symlink refusal, and refusal
of a product modified after index persistence.

The actual F′ v4.4.2 component UT executable also passed **7/7 tests** in the
Linux/ARM64 twin image. These execute the production component and generated
FPP port/telemetry/event code: readiness admission, duplicate request handling,
conflicting duplicate refusal, committed-product reporting, staging bytes,
stale generation refusal, matching ACK progression, new-FC-session restaging,
bounded retry exhaustion, retained older-product lookup after a failed newer
capture, actual component reconstruction from persistent metadata, monotonic
IDs across driver restart, duplicate CACHE_READY completion, stale completed
stage refusal, and cancellation/recovery admission with an explicit local completion barrier. Build in a separate native Linux UT cache with
`BUILD_TESTING=ON`, `FPRIME_ENABLE_FRAMEWORK_UTS=OFF`, and the project's native
Linux toolchain, then build/run:

```sh
cmake --build <pi-ut-build> --target FprimeArtemisCore_Components_PayloadLinkService_ut_exe --parallel 8
<pi-ut-build>/bin/native-linux/FprimeArtemisCore_Components_PayloadLinkService_ut_exe
```

The container validation used `/build/pi/component-ut`; its JUnit output is
`/data/payload-link-ut.xml` in the payload-computer service's run directory.

The real Linux/ARM64 Pi deployment build passed with the F′ v4.4.2 dependency
set and normalized binary/dictionary artifacts were generated. A separate PTY
smoke run rejected a missing sample and a missing UART with exit status 2,
reported the recorded Lepton backend READY, stayed alive for five seconds,
and shut down cleanly on SIGTERM (exit 0). The Linux host clamps 64 KiB thread
stack requests to its 128 KiB minimum; this is Pi host evidence, not a Teensy
stack requirement. Full FC↔Pi/radio/Yamcs acceptance remains an integration
gate. Component/helper tests and this smoke do not establish physical camera
operation or ground receipt.

## Recovery milestone (2026-11-11 HST)

Persistent multi-product staging and final-stage replay recovery are implemented.
The Linux/ARM64 generated F′ component UTs pass 7/7 and portable helper suite
passes 1/1. The cancellation UT holds a 3,600-second transaction, emits exactly
one cancel signal, refuses a new capture before terminal callback, then accepts
and commits a clean next capture. A second race regression checks both an
already-pending positive descriptor and an already-pending failure: neither,
nor additional positive/zero callbacks, releases ownership before explicit
cancelDoneIn. A spurious completion before cancel dispatch is also ignored. The driver cancellation task has its own
component evidence; full FC/Pi/Yamcs recovery is the integration gate.
