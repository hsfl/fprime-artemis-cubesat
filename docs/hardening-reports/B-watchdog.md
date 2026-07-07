# Worker B Report: Teensy Hardware Watchdogs

## Scope

Added hardware watchdog coverage to both Teensy 4.1 bridge firmwares:

- Satellite: `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/`
- Ground: `GDS_Teensy/firmware/gds_teensy/`

No F Prime tree changes, UART setting changes, link protocol changes, or hardware uploads.

## Mechanism Chosen

Used direct RT1062 `WDOG1_*` registers through the Teensy 4.x core instead of `Watchdog_t4`.

Reason: `Watchdog_t4` is not installed in the repo-local Arduino CLI toolchain (`arduino-cli lib list` found no watchdog library). Direct compile validation against the installed Teensy 1.62.0 core passed for both sketches.

Added byte-identical helper copies:

- `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/wdt_guard.hpp`
- `GDS_Teensy/firmware/gds_teensy/src/wdt_guard.hpp`

Verified identical:

```bash
cmp -s ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/wdt_guard.hpp \
  GDS_Teensy/firmware/gds_teensy/src/wdt_guard.hpp && echo 'wdt_guard.hpp copies match'
```

Output:

```text
wdt_guard.hpp copies match
```

## Timeout Value

Timeout: 12 seconds.

Why: it is inside the requested 8-16 s window and comfortably above normal relay timing. Current RF constants bound a full 220-byte frame to about 5 RF segments; each segment has up to 5 ACK waits at 80 ms plus short 8 ms inter-segment gaps. That is well below 12 s, so normal downlink bursts should not trip the watchdog.

## Feed Placement

Main feed points:

- Each board's `loop()` after `g_relay.poll()`.
- `RelayUartRf::poll()` entry.
- Per-byte UART drain loops for GDS/RPi and payload streams.
- RF send path around inter-segment delay.
- ACK/retry path at each send attempt.
- `waitForAck()` spin loop and after processing non-ACK RF packets while waiting.

These feed points cover normal loop service and the longest plausible bounded blocking sections without changing relay semantics.

## Reset-Reason Detection

Cheap detection enabled.

The helper checks `SRC_SRSR_WDOG_RST_B` on boot and clears that bit if present. The board-specific sketches then emit one debug line:

- Satellite: `[ArtemisTeensy] watchdog reset detected` on `Serial`
- Ground: `[GDS_Teensy] watchdog reset detected` on `SerialUSB1` when debug USB is compiled in

Both boards also log watchdog arming:

- Satellite: `[ArtemisTeensy] hardware watchdog armed (12s)`
- Ground: `[GDS_Teensy] hardware watchdog armed (12s)`

## Build Evidence

Requested build scripts were run from each workspace root, but both failed before compilation because `arduino-cli core update-index` could not resolve external Arduino/PJRC hosts in this sandbox:

```text
Downloading index: package_index.tar.bz2 Download failed: performing HEAD request: Head "https://downloads.arduino.cc/packages/package_index.tar.bz2": dial tcp: lookup downloads.arduino.cc: no such host
Downloading index: package_teensy_index.json Download failed: performing HEAD request: Head "https://www.pjrc.com/teensy/package_teensy_index.json": dial tcp: lookup www.pjrc.com: no such host
Some indexes could not be updated.
```

I attempted to stash only my watchdog paths to prove the failure without the patch, but this Codex sandbox cannot create `.git/index.lock`:

```text
error: Unable to create '/Users/sozodennis/Developer/fprime-artemis-cubesat/.git/index.lock': Operation not permitted
error: could not write index
```

Direct compile validation, using the same FQBN/build paths/libraries as the scripts but skipping only the network `update-index`/`core install` preflight, passed for both workspaces.

Satellite command:

```bash
cd ArtemisTeensy_N2_Baremetal
ARDUINO_CONFIG_FILE=tools/arduino-cli/arduino-cli.yaml arduino-cli compile \
  --fqbn teensy:avr:teensy41 \
  --libraries firmware/libs \
  --build-path build/arduino-cli \
  firmware/satellite_teensy
```

Satellite output:

```text
Memory Usage on Teensy 4.1:
  FLASH: code:56240, data:11008, headers:8524   free for files:8050692
   RAM1: variables:32352, code:53672, padding:11864   free for local variables:426400
   RAM2: variables:12416  free for malloc/new:511872
```

Ground command:

```bash
cd GDS_Teensy
ARDUINO_CONFIG_FILE=tools/arduino-cli/arduino-cli.yaml arduino-cli compile \
  --fqbn teensy:avr:teensy41:usb=serial3 \
  --libraries ../ArtemisTeensy_N2_Baremetal/firmware/libs \
  --build-path build/arduino-cli \
  firmware/gds_teensy
```

Ground output:

```text
Memory Usage on Teensy 4.1:
  FLASH: code:56688, data:11228, headers:8880   free for files:8049668
   RAM1: variables:28416, code:54120, padding:11416   free for local variables:430336
   RAM2: variables:37088  free for malloc/new:487200
```

`git diff --check` passed.

## Later HIL Bench Test

Deliberately test the WDT only on the bench after normal RF smoke is healthy:

1. Temporarily add a local-only test hang after watchdog arming, for example `while (true) {}` guarded by a short-lived compile-time test macro.
2. Flash one board at a time using physical Teensy upload IDs, not `/dev/cu.usbmodem*` guesses.
3. Watch the board disappear/re-enumerate after about 12 seconds.
4. Confirm the next boot prints the watchdog reset line on that board's debug serial.
5. Remove the test hang and rebuild/reflash normal firmware.

