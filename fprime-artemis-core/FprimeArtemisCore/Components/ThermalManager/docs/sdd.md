# Components::ThermalManager

> **Status: implemented, not yet tested on hardware.** Builds in
> `FlightControllerDeployment`; wired as `thermalManager` on `rateGroup_0_25Hz`.

`ThermalManager` is the manager tier for the flight controller's board
temperatures. It reads every sensor, decides whether the spacecraft is
`NOMINAL`, `COLD`, or `HOT`, and publishes the temperatures as telemetry. It
knows nothing about TMP36s or ADCs: every read goes through the driver's port,
so replacing the sensors means replacing the driver instance only.

It replaces the ArtemisRpiTeensy_N2 `ThermalManager`, which drove a
deterministic fake model. The command names are kept.

```
operator ──(REQUEST_THERMAL_STATUS, SET_THERMAL_MODE)──> ThermalManager ──(ThermalReadingGet)──> ThermalDriver_TMP36 ──(Fw.Signal / ADCMvValue)──> 7x ZephyrADCDriver
```

| Layer | Component | Owns |
|---|---|---|
| **Manager** | **`ThermalManager`** | state, limits, telemetry, commands |
| Driver | `ThermalDriver_TMP36` | TMP36 transfer function, rated-range check |
| Bus | `Zephyr.ZephyrADCDriver` (x7) | one `adc1` channel each |

## Sensors

Index order is `Components.ThermalSensor`, which matches the `temp_sensors`
`io-channels` order in `boards/teensy41.overlay`:

| Index | Sensor | Teensy pin | ADC1 channel |
|---|---|---|---|
| 0 | `OBC` | 14 | 7 |
| 1 | `PDU` | 15 | 8 |
| 2 | `BATTERY` | 41 | 10 |
| 3 | `SOLAR_1` | 20 | 15 |
| 4 | `SOLAR_2` | 21 | 0 |
| 5 | `SOLAR_3` | 22 | 13 |
| 6 | `SOLAR_4` | 23 | 14 |

## States

| State | Meaning |
|---|---|
| `NO_DATA` | No sensor reads inside its rated range. The state at boot |
| `NOMINAL` | Every valid sensor is between `COLD_LIMIT_C` and `HOT_LIMIT_C` |
| `COLD` | At least one valid sensor is below `COLD_LIMIT_C`, none above `HOT_LIMIT_C` |
| `HOT` | At least one valid sensor is above `HOT_LIMIT_C` (wins over `COLD`) |

**The limits are placeholders** (-10 C and 60 C, one pair for every sensor).
The TCS owner should replace them with per-sensor limits from the thermal
analysis. Nothing acts on `COLD` or `HOT` yet; they are reported only.

## Commands

| Command | Effect |
|---|---|
| `REQUEST_THERMAL_STATUS` | Reads now, then emits `ThermalStatusReport` with the state, valid mask, and min/max valid temperature |
| `SET_THERMAL_MODE` | Records `OFF`, `OBSERVE`, or `HEATER_AUTO`. **Intent only: no heater is driven** |

## Telemetry

| Channel | Meaning |
|---|---|
| `ThermalState` | `NO_DATA`, `NOMINAL`, `COLD`, or `HOT` |
| `Temperatures` | Degrees C per sensor. Only entries set in `ValidSensorMask` are real |
| `ValidSensorMask` | Bit i set when sensor i reads inside its rated range |
| `ThermalMode` | Requested mode (intent only) |

`Temperatures` is written only when at least one sensor is valid. With none,
the last values stay on the ground display with an old timestamp, and
`ThermalState` = `NO_DATA` says they are stale.

## Events

| Event | When |
|---|---|
| `ThermalStateChanged` | Every state transition |
| `SensorValidityChanged` | A sensor enters or leaves its rated range. Expect one per unplugged sensor on the first read |
| `ThermalModeUpdated` | `SET_THERMAL_MODE` |
| `ThermalStatusReport` | `REQUEST_THERMAL_STATUS` |

## Rate

`run` is on `rateGroup_0_25Hz`: one read every 4 s. Board temperatures change
over minutes, and the 1 Hz group has no free slots (10 of 10 used).
