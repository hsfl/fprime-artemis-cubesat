# Components::GpsManager

> **Status: implemented, not yet tested on hardware** (the driver's parser is
> host-verified; see its SDD). Builds in
> `FlightControllerDeployment`; wired as `gpsManager` on `rateGroup_1Hz`.

`GpsManager` is the manager tier for the flight controller's GPS. It owns the
hardware-independent contract: reporting whether the module is powered and
talking, whether it has a satellite lock, and publishing its fixes as
telemetry. It knows nothing about the module or about NMEA. Every read goes
through the driver's port, so replacing the PA1010D means replacing the driver
instance only.

```
operator ──(GET_GPS_STATUS)──> GpsManager ──(GpsReadingGet)──> GpsDriver_AdafruitMiniGps <──(Drv.ByteStreamDriver)── ZephyrUartDriver
```

| Layer | Component | Owns |
|---|---|---|
| **Manager** | **`GpsManager`** | state, telemetry, ready/not-ready notification |
| Driver | `GpsDriver_AdafruitMiniGps` | NMEA sentences, checksums, unit conversion |
| Bus | `Zephyr.ZephyrUartDriver` | bytes on `lpuart7` at 9600 baud |

## States

| State | Meaning |
|---|---|
| `OFF` | No sentences arriving. The module is unpowered, unplugged, or miswired. The state at boot |
| `ACQUIRING` | Sentences arriving, no satellite lock yet. Position is not current |
| `READY` | Locked. Position, altitude, and satellite count published every `run` tick |

`GpsPowered` telemetry is `OFF` in state `OFF` and `ON` otherwise: with no
enable line on the module, "powered" can only be observed, never commanded.

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
| `GET_GPS_STATUS` | Emits `GpsStatusReport` with the state, satellite count, and fixes lost since boot |

There is no power command: the module has no enable line and no software
power-down. If the GPS rail is ever put behind a PDU channel, the command
belongs on the PDU manager, and this component keeps observing.

## Telemetry

Position channels (`Latitude`, `Longitude`, `Altitude`, `Satellites`,
`GpsUtcSeconds`) are written only while `READY`. On a lost fix they keep their
last values with an old timestamp rather than being zeroed, so the ground sees
the last known position and an aging clock instead of a jump to the Gulf of
Guinea.
