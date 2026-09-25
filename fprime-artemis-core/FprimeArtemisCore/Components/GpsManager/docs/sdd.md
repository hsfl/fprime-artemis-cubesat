# Components::GpsManager

> **Status: implemented, not yet tested on hardware** (the driver's parser is
> host-verified; see its SDD). Builds in
> `FlightControllerDeployment`; wired as `gpsManager` on `rateGroup_1Hz`.

`GpsManager` is the manager tier for the flight controller's GPS. It owns the
hardware-independent contract: turning the module on and off, reporting
whether it is talking, whether it has a satellite lock, and publishing its fixes as
telemetry. It knows nothing about the module or about NMEA. Every read and
power request goes through the driver's ports, so replacing the PA1010D means replacing the driver
instance only.

```
MissionApp ──(GpsPowerRequest)──> GpsManager ──(GpsPowerRequest, GpsReadingGet)──> GpsDriver_AdafruitMiniGps <──(Drv.ByteStreamDriver)── ZephyrUartDriver
operator ──(SET_GPS_POWER, GET_GPS_STATUS)──┘
```

| Layer | Component | Owns |
|---|---|---|
| **Manager** | **`GpsManager`** | state, telemetry, ready/not-ready notification |
| Driver | `GpsDriver_AdafruitMiniGps` | NMEA sentences, checksums, unit conversion |
| Bus | `Zephyr.ZephyrUartDriver` | bytes on `lpuart7` at 9600 baud |

## States

| State | Meaning |
|---|---|
| `OFF` | No sentences arriving. The module is in standby, unpowered, unplugged, or miswired. The state at boot |
| `ACQUIRING` | Sentences arriving, no satellite lock yet. Position is not current |
| `READY` | Locked. Position, altitude, and satellite count published every `run` tick |

The state is observed, not commanded. `GpsPowered` telemetry is the commanded
power state. The module does not acknowledge power requests, so the two are
read together:

| `GpsPowered` | `GpsState` | Meaning |
|---|---|---|
| `ON` | `ACQUIRING` / `READY` | Normal |
| `ON` | `OFF` | Asked to talk and silent: unpowered, unplugged, or TX not wired. Send `SET_GPS_POWER(ON)` again to retry the wake |
| `OFF` | `OFF` | In standby, as commanded |
| `OFF` | `ACQUIRING` / `READY` for more than ~3 s after the command | Ignored the standby request: check the Teensy TX to module RX wire. A brief `READY` right after the command is sentences already in flight, not a fault |

## Power

"Off" is the module's PMTK standby mode (`$PMTK161,0`), not a power cut: the
module keeps power and any byte on its RX line wakes it. Cutting power is an
EPS job.

**The GPS is on by default.** `GpsManager` requests `ON` on its first `run`
tick. Standby survives a Teensy reset, so a module put to sleep before a reset
has to be woken, not assumed awake.

**A commanded standby is not a lost fix.** Leaving `READY` because of
`SET_GPS_POWER(OFF)` emits `GpsStateChanged` but not `GpsNotReady`, and does
not count in `FixLostCount`.

**The manager keeps reading while `OFF` is commanded**, so a module that ignores
standby is visible instead of hidden.

## Ready notification

The transition into and out of `READY` is what the rest of the flight software
watches:

- `GpsReady(satellites)` on entering `READY`
- `GpsNotReady(state)` on leaving it, with the state that replaced it: `OFF` if
  the module went silent, `ACQUIRING` if it is still talking but lost its lock

`GpsStateChanged` fires on every transition and is the audit trail;
`FixLostCount` counts the losses since boot.

Nothing consumes these events in the flight software today. When an application
component needs to gate on the GPS -- waiting for a fix before a timed
collection, say -- add an output port here rather than having it read telemetry.

## Commands

| Command | Effect |
|---|---|
| `SET_GPS_POWER(state: Fw.On)` | Engineering: wake the module or put it in standby. Responds `OK` if the request was sent, `EXECUTION_ERROR` (plus `GpsPowerFailed`) otherwise. `OK` does not mean the module obeyed: watch `GpsState` |
| `GET_GPS_STATUS` | Emits `GpsStatusReport` with the state, commanded power, satellite count, and fixes lost since boot |

**Bench check for the TX wire:** send `SET_GPS_POWER(OFF)`. `gpsDriver.SentencesParsed`
should stop climbing within about 3 seconds (a sentence or two in flight may
still land). Send `SET_GPS_POWER(ON)` and it
should resume.

## Telemetry

Position channels (`Latitude`, `Longitude`, `Altitude`, `Satellites`,
`GpsUtcSeconds`) are written only while `READY`. On a lost fix they keep their
last values with an old timestamp rather than being zeroed, so the ground sees
the last known position and an aging clock instead of a jump to the Gulf of
Guinea.
