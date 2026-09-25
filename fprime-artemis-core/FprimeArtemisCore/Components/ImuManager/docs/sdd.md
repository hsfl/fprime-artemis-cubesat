# Components::ImuManager

> **Status: implemented, not yet tested on hardware.** Builds in
> `FlightControllerDeployment`; wired as `imuManager` on `rateGroup_1Hz`.

`ImuManager` is the manager tier for the flight controller's IMU. It owns the
hardware-independent contract: turning the IMU on and off, reporting whether it
is on, off, or faulted, and publishing its readings as telemetry. It knows
nothing about the chip. Every hardware action goes through the driver's ports,
so replacing the LSM6DSOX means replacing the driver instance only.

```
MissionApp ──(ImuPowerRequest)──> ImuManager ──(ImuPowerRequest, ImuReadingGet)──> ImuDriver_LSM6DSOX ──(Drv.I2c)──> ZephyrI2cDriver
operator ──(SET_IMU_POWER, GET_IMU_STATUS)──┘
```

| Layer | Component | Owns |
|---|---|---|
| Application | `MissionApp` | *when* the IMU should be on |
| **Manager** | **`ImuManager`** | commands, state, telemetry, fault detection |
| Driver | `ImuDriver_LSM6DSOX` | LSM6DSOX registers, power-down mode, unit conversion |
| Bus | `Zephyr.ZephyrI2cDriver` | bytes on `lpi2c1` |

## States

| State | Meaning |
|---|---|
| `OFF` | IMU in power-down. No readings. The state until the first `run` tick |
| `ON` | IMU sampling; readings published every `run` tick |
| `FAULT` | IMU was requested on but is not responding. No readings |

### Transitions

| From | Trigger | To | Action |
|---|---|---|---|
| `OFF` | boot (first `run` tick) | `ON` or `FAULT` | request power-on, as a power request `ON` |
| `OFF` / `FAULT` | power request `ON`, driver returns `SUCCESS` | `ON` | clear the consecutive-error count |
| `OFF` / `FAULT` | power request `ON`, driver returns `FAILURE` | `FAULT` | `ImuPowerFailed(ON)` |
| `ON` | power request `ON` | `ON` | none |
| any | power request `OFF`, driver returns `SUCCESS` | `OFF` | |
| any | power request `OFF`, driver returns `FAILURE` | `OFF` | `ImuPowerFailed(OFF)` |
| `ON` | read returns `OK` | `ON` | publish the reading, clear the consecutive-error count |
| `ON` | read returns `BUS_ERROR` or `POWERED_OFF` | `ON` | count the error |
| `ON` | 3rd consecutive failed read | `FAULT` | `ImuFaulted(3)` |

### Rules behind the table

**The IMU is on by default.** `ImuManager` requests power-on on its first `run`
tick. A Teensy reset does not always reset the IMU: if the board's 3.3 V rail
stays up (a reflash, `fatalHandler.RESTART`, a FATAL), the chip keeps its last
configuration. Power-on software-resets the chip first, so the result is the
same either way. With no IMU connected, every boot ends in `FAULT` with an
`ImuPowerFailed(ON)` warning on the first tick.

**"Off" is the chip's power-down mode.** The chip keeps power and still answers
on I2C (see the driver SDD). Cutting power is an EPS job.

**A failed `OFF` is still `OFF`, with a warning.** `FAULT` means "requested on
but not responding." After a failed power-down the driver has stopped reading,
so no reading is claimed either way. The `ImuPowerFailed(OFF)` event, plus the
driver's `I2cError`s, say the chip may still be sampling.

**Only a power request leaves `FAULT`.** The manager does not retry on its own. A
flaky bus that keeps failing would otherwise flood the event log with power
cycles. An operator (or later `MissionApp`) sends `ON` to retry. A request that
fails again stays in `FAULT`.

**Telemetry channels hold their last value in the ground system.** When the IMU
leaves `ON`, the reading channels stop updating but GDS keeps showing the last
reading. `ImuState` is the channel that says whether a reading is current.

**Readings are 1 Hz.** `run` is on `rateGroup_1Hz`, the same rate as telemetry
downlink (`CdhCore.tlmSend.Run`), so reading faster would not reach the ground
any sooner. The chip itself samples at 104 Hz; each tick takes the newest sample.

## Ports

| Port | Kind | Connects to | Purpose |
|---|---|---|---|
| `powerRequestIn` | guarded input, `Components.ImuPowerRequest` | `MissionApp` (future) | turn the IMU on or off; returns `Fw.Success` |
| `run` | guarded input, `Svc.Sched` | `rateGroup_1Hz` | boot power-on, reading, fault detection |
| `driverPowerOut` | output, `Components.ImuPowerRequest` | `imuDriver.powerRequestIn` | driver power-up / power-down |
| `driverReadingGet` | output, `Components.ImuReadingGet` | `imuDriver.readingGet` | read the latest sample |

The component is **passive**, like `RpiPowerManager`: it adds no thread or queue.
Its input ports and commands are **`guarded`**, so they share one mutex. That
matters because commands run on the command dispatcher's thread and `run` runs on
the rate group's thread: without the mutex, a power-up's multi-register I2C
sequence could interleave with a reading.

`powerRequestIn` uses the same port type as `driverPowerOut`. `MissionApp` sees
the manager exactly as the manager sees the driver.

## Commands

| Command | Purpose |
|---|---|
| `SET_IMU_POWER(state: Fw.On)` | engineering: turn the IMU on or off. Responds `OK` if the driver accepted the request, `EXECUTION_ERROR` otherwise |
| `GET_IMU_STATUS` | report the current state and error count as an event |

`SET_IMU_POWER` is for bring-up and anomaly response, like
`RpiPowerManager.SET_RPI_POWER`. Once `MissionApp` controls the IMU, routine
operations go through `MissionApp`.

A power request is a handful of I2C transfers at 100 kHz, a few milliseconds, so
the command responds after the driver returns rather than later.

## Events and telemetry

| Name | Kind | Meaning |
|---|---|---|
| `ImuStateChanged(state)` | event, activity high | the state changed |
| `ImuPowerFailed(state)` | event, warning high | the driver rejected a power request |
| `ImuFaulted(consecutiveErrors)` | event, warning high | reads kept failing; the IMU is now `FAULT` |
| `ImuStatusReport(state, readErrors)` | event, activity low | response to `GET_IMU_STATUS` |
| `ImuState` | telemetry, update on change | `OFF`, `ON`, or `FAULT` |
| `Acceleration` | telemetry, `Vector3` | m/s², updated each tick while `ON` |
| `AngularRate` | telemetry, `Vector3` | rad/s, updated each tick while `ON` |
| `ImuTemperature` | telemetry, `F32` | °C (die temperature, not the board), updated each tick while `ON` |
| `ReadErrors` | telemetry, `U32`, update on change | failed reads since boot |

Readings are three channels rather than one `ImuReading` struct, so each
quantity has its own channel and units. Bus-level detail (`I2cError`, `ChipIdMismatch`)
is reported by the driver; the manager reports only what it means for the IMU.

Telemetry names carry no chip, bus, or register words, so they stay the same when
the hardware changes.

## Shared types

The manager and driver share the port and data types in the driver SDD
(`Vector3`, `ImuReading`, `ImuReadStatus`, `ImuPowerRequest`, `ImuReadingGet`).
They are in [`Types/Imu.fpp`](../../../Types/Imu.fpp), next to `FcPcLink.fpp`,
with the manager's state enum:

```fpp
module Components {
  @ IMU state as reported by ImuManager
  enum ImuState: U8 {
    OFF = 0    @< power-down; no readings
    ON = 1     @< sampling; readings current
    FAULT = 2  @< requested on but not responding
  }
}
```

## Configuration

Topology wiring in `FlightControllerDeployment/Top/topology.fpp`. The channels
are in the `Imu` packet (id 8) in `FlightControllerDeploymentPackets.fppi`.

```fpp
rateGroup_1Hz.RateGroupMemberOut[7] -> imuManager.run

# connections Imu
imuManager.driverPowerOut -> imuDriver.powerRequestIn
imuManager.driverReadingGet -> imuDriver.readingGet

imuDriver.busWriteRead -> imuI2cBus.writeRead
imuDriver.busWrite -> imuI2cBus.write
```

Nothing in the manager counts ticks as seconds, so it works on any rate group.
The fault threshold is 3 ticks at any rate: 3 s on `rateGroup_1Hz`.

| Constant | Value | Meaning |
|---|---|---|
| `FAULT_THRESHOLD` | 3 | consecutive failed reads before `FAULT` |

## Not yet implemented

- **`MissionApp` control.** Whether the IMU is on in `BASE` and off in `STANDBY`
  is a mission decision not made yet. Until then the IMU is on after boot
  until `SET_IMU_POWER(OFF)`.
- **Push readings to other components.** An `ImuReadingUpdate` output port would
  let a future ADCS manager receive each reading. No consumer exists yet.
- **Automatic retry from `FAULT`.** For example, one power-on attempt every N
  ticks, with a cap.
- **Magnetometer.** The LIS3MDL gets its own driver. Whether its readings join
  this manager or a separate `MagManager` is open.
- **Unit tests.** The transitions table is the test plan.
