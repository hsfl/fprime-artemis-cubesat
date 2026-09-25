# Components::GpsDriver_AdafruitMiniGps

> **Status: implemented; parser verified on host, not yet tested on hardware.**
> The NMEA parsing was exercised on a laptop against captured GGA sentences
> (valid fix, cold-start no-fix, bad checksum, hemisphere signs, split buffers,
> overflow re-sync). Builds in
> `FlightControllerDeployment`; wired as `gpsDriver`, fed by `gpsUartDriver` on
> `lpuart7` (Teensy RX=28 / TX=29) at 9600 baud.

Driver tier for the Adafruit Mini GPS PA1010D. It implements the `GpsManager`
contract (`Types/Gps.fpp`) against the module's NMEA output: bytes arrive from
a byte stream driver, sentences are reassembled and checked, and the latest fix
is handed to the manager in SI units.

The module has no enable line. `powerRequestIn(OFF)` sends `$PMTK161,0*28`
(standby) and marks the last fix stale at once. `powerRequestIn(ON)` sends
`$PMTK000*32` (test packet): its first byte is what wakes the module, and the
`PMTK001` reply is ignored like any non-GGA sentence. The module acknowledges
neither, so "talking" is still observed: silence means off. `SUCCESS` means the
UART driver accepted the bytes, nothing more.

## Data path

```
ZephyrUartDriver ──($recv, Fw.Buffer)──> drvReceiveIn ──> line buffer ──> GGA parse ──> m_fix
                 <──(recvReturnIn)────── drvReceiveReturnOut
GpsManager ──(readingGet)──> OK | NO_FIX | NO_DATA
```

Buffers are returned to the UART driver on every path, error included. A
dropped buffer is a leak from a 4-buffer pool, so there is no path that keeps
one.

## Sentence handling

Only `GGA` is parsed: it is the one sentence carrying fix quality, satellite
count, altitude, and position together. The talker ID varies with the
constellation in use (`$GPGGA`, `$GNGGA`, `$GLGGA`), so the match is on the
last three characters of the sentence name. Other sentences are ignored and are
not errors.

A sentence is accepted only if it starts with `$`, ends with `*hh`, and the
checksum matches the XOR of everything between. Accepting a sentence resets the
staleness counter *whether or not it carried a lock* -- a `GGA` with fix quality
`0` still proves the module is alive and talking, which is exactly what
distinguishes `ACQUIRING` from `OFF` in the manager.

## Read status

| Status | Condition |
|---|---|
| `OK` | A fix was parsed within the staleness window |
| `NO_FIX` | Sentences are arriving but the last `GGA` reported fix quality `0` |
| `NO_DATA` | No accepted sentence for `STALE_TICKS` run ticks |

`STALE_TICKS` is 3, and `run` is on the 1Hz rate group, so three missed
sentences (the PA1010D emits `GGA` once a second) means no data. The state
starts stale: nothing has been heard at boot.

## Failure modes

| Symptom | Likely cause |
|---|---|
| `SentenceOverflow` repeating | Wrong baud rate: line noise never produces a line ending |
| `SentenceErrors` climbing with no `SentencesParsed` | Wrong baud rate, or RX and TX swapped |
| `SentencesParsed` climbing, manager stuck in `ACQUIRING` | Working link, no sky view. Expected indoors |
| Nothing at all | Module unpowered, or `lpuart7` not wired to the breakout |

One thing the host test cannot cover: position parsing relies on `strtod` from
the Zephyr C library. If latitude and longitude come back as exactly `0` on
hardware while `SentencesParsed` climbs, the float parsing is the suspect, not
the sentence handling.
