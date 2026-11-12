# Portable full product cache and C3M downlink

Milestone M2, portable engine: implemented 2026-11-11 (HST). F′ component,
GenericHub, target memory placement, virtual RF, and Yamcs integration are owned
by the surrounding deployment; these host checks do not establish hardware or
complete-twin acceptance.

## Ownership and memory

`ProductCache` references deployment-supplied storage, capped at the generated
196,608-byte limit. It allocates no memory and contains no second product copy.
The Teensy deployment places that storage in its explicitly configured internal
OCRAM region; the engine does not select a linker region. Construction/reset
clears validity and metadata rather than assuming NOLOAD product bytes are zero.

`ProductCache` and `Downlink` have one serialized owner. Call them from one
worker, or protect **all** access with the same mutex. Concurrent UART staging
and radio callbacks without synchronization are unsupported.

Pi owns the persistent source file. A committed cache is independent of Pi
availability; RF transmission and requested repairs read only the supplied RAM.
FC reset invalidates cached bytes, and the retained Pi product can be restaged.

## Interface and lifecycle

Include `FprimeArtemisCore/Science/ProductCache.hpp` and link `artemis_science`.
The subtree can be added with `add_subdirectory(Science)`.

- `ProductKey` contains admitted Pi session, monotonically increasing cache
  generation within that session, product ID, byte count, and CRC16/CCITT.
- `setSession()` explicitly admits a new nonzero Pi session. A session change
  aborts incomplete receiving; READY/SENDING data remains usable for RF.
- `begin()` accepts a new transaction only while idle. Exact repeated BEGIN
  is idempotent, including during transmission, and does not rewind progress.
- `append()` accepts sequential chunks up to 200 bytes. An identical repeated
  range succeeds; conflicting duplicates, gaps, out-of-bounds/overflow offsets,
  wrong keys, and stale sessions fail without changing received progress.
- `commit()` requires every advertised byte and matching whole-file CRC16.
  Incomplete commit preserves staging; CRC failure invalidates it. COMMIT does
  **not** start RF transmission.
- `abort()` invalidates an idle/incomplete transaction; active RF is BUSY.
  Stop the downlink first for an explicit operator abort. Aborted/failed
  generations cannot be reused; select a newer generation for another stage.
- `reset()` invalidates everything, including prior session/generation state.

State flow: `EMPTY → RECEIVING → READY → SENDING → READY`. Accepted replacement
invalidates the old single slot immediately. Failed replacement cannot expose
the old or partially overwritten data as a valid product.

`Downlink.start()` creates a fresh nonzero U8 transfer ID. `nextPacket()` returns
one packet; call `packetSent(success)` after local transmission. Failure retains
that same packet for another attempt. Local END completion returns the cache to
READY and retains its bytes; it does not prove ground receipt. `stop()` cancels
transmission/repair and refuses old retry requests until another `start()`.

Repairs from multiple bitmaps coalesce in a bounded 703-byte bitmap. A prepared
packet is stable if another retry arrives before its TX completion. Requests
arriving during initial transmission are sent before END, and late repairs
after END resume from cached bytes. Replacement is BUSY while repairs remain.

## Existing ground contract

The engine adapts the production C3M `payload_cache.cpp` protocol, removing its
Arduino, RPC response, and embedded-array coupling. Generated constants remain
owned by `config/transport_constants.json` and `config/rf_networks.json` through
the existing radio `LinkConstants.hpp`; no protocol constants are independently
redefined for size/payload magic.

All integer fields below are little-endian, and packets begin with `N2`:

| Packet | Bytes after magic | Length |
|---|---|---|
| HEADER, repeated three times | type=1, transfer U8, product U32, bytes U32, packets U16, data-size U8, file-CRC U16 | 17 |
| DATA | type=2, transfer U8, packet-index U16, length U8, data, packet-CRC U16 | ≤44 |
| END | type=3, transfer U8, packet-count U16, file-CRC U16 | 8 |
| RETRY | type=4, transfer U8, start-index U16, bitmap-size U8, bitmap | 8–43 |

DATA carries at most 35 bytes; its CRC covers magic through the final data byte.
File CRC covers the complete product. Both use initial `0xFFFF`, polynomial
`0x1021`, no reflection/final XOR. The engine emits channel-1 application
packets; the existing radio manager owns RF segmentation and scheduling.

## Validation

macOS, from `fprime-artemis-core/FprimeArtemisCore/Science`:

```sh
cmake -S . -B /tmp/artemis-science-cache-build \
  -DPython3_EXECUTABLE="$(pwd)/../../fprime-venv/bin/python"
cmake --build /tmp/artemis-science-cache-build -j4
ctest --test-dir /tmp/artemis-science-cache-build --output-on-failure
```

Use the equivalent project Python path under WSL2 on Windows. Python requires
the existing project `pyserial` dependency because the interoperability test
loads the unchanged ground `payload_receiver.py`; it does not open hardware.

- `science_cache`: maximum-size product with canaries; stale sessions and
  generations; incomplete/corrupt commit; chunk conflicts/gaps; BUSY replacement;
  failed-TX retry; repeated transfer IDs including rollover; coalesced repairs;
  Pi-session change with committed data; reset invalidation; END/repair race.
- `science_legacy_receiver`: packets from the real C++ engine pass through the
  existing receiver's stream parser in three-byte slices, lose two DATA packets,
  generate a real RETRY bitmap, repair locally, and save byte-identical,
  CRC-valid ground data. This fixture is a generic 2051-byte blob, not a claim
  of `.fdp`/Lepton decode acceptance.

Observed host result: **2/2 tests pass** with AppleClang 21 and Python 3.13.
The same **2/2 pass with ASan + UBSan**. A Cortex-M7 C++14 compile with Zephyr
SDK 0.17.4 passes `-Wall -Wextra -Werror -Wconversion`; the unoptimized object
contains 4,658 bytes text, **zero data/BSS**. This is a compile check, not a
target link, timing, or accessible-OCRAM result.
