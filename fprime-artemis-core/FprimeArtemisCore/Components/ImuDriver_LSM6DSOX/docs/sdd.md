# Components::ImuDriver_LSM6DSOX

> **Status: implemented, not yet tested on hardware.** Builds in
> `FlightControllerDeployment`; wired as `imuDriver` on `imuI2cBus` (`lpi2c1`).

`ImuDriver_LSM6DSOX` is the driver tier for the ST LSM6DSOX 6-axis IMU
(accelerometer + gyroscope) on the Adafruit LSM6DSOX+LIS3MDL breakout. It does
two things for its manager:

1. **Turn the IMU on or off.**
2. **Return the latest reading** (acceleration, angular rate, temperature).

It knows the LSM6DSOX register map and nothing about the mission. It never
touches the I2C bus directly: every transfer goes through the `Drv.I2c` ports to
a `Zephyr.ZephyrI2cDriver` instance, so the same component runs against any bus
driver that speaks `Drv.I2c`.

```
ImuManager ──(ImuPowerRequest, ImuReadingGet)──> ImuDriver_LSM6DSOX ──(Drv.I2c)──> ZephyrI2cDriver ──> lpi2c1 (Teensy pins 18/19)
```

The LIS3MDL magnetometer on the same breakout is a separate chip at a separate
address and is **out of scope**; it gets its own driver.

## What "off" means

**The breakout has no power switch for the chip. "Off" is the LSM6DSOX's
power-down mode, not a cut rail.** Writing an output data rate (ODR) of `0000`
to both `CTRL1_XL` and `CTRL2_G` stops sampling and drops the chip to a few
microamps. The chip stays powered and still answers on I2C. Removing power
entirely is an EPS job, and this driver does not claim to do it.

The chip comes out of its own power-on reset in power-down. A Teensy reset that
leaves the 3.3 V rail up does not reset the chip, which keeps its last
configuration, so the driver's `OFF` state at boot is only true once
`ImuManager` has requested power-off (see the manager SDD).

## Requirements

| ID | Description | Rationale | Validation |
|---|---|---|---|
| IMU-001 | The driver shall put the LSM6DSOX into its sampling mode on an ON request and into power-down on an OFF request. | Basic on/off control, and a low-power state when the IMU is not needed. | Hardware: read back `CTRL1_XL`/`CTRL2_G` after each request. |
| IMU-002 | On an ON request, the driver shall verify `WHO_AM_I` = `0x6C` before configuring the chip, and fail the request otherwise. | Catches a wrong address, a missing board, or a different chip before writing to it. | Unit test with a mocked bus returning a wrong ID. |
| IMU-003 | On request, the driver shall read acceleration, angular rate and temperature from the chip in a single I2C transfer and return them in SI units. | One burst keeps all three values from the same sample; SI units keep the manager hardware-independent. | Unit test with known raw bytes; hardware check that Z reads about +9.8 m/s² when flat. |
| IMU-004 | The driver shall refuse a reading request while OFF, without touching the bus. | A powered-down chip returns stale output registers; returning them as "latest" would be false. | Unit test: OFF, then request, then assert no bus call. |
| IMU-005 | The driver shall report I2C failures to its caller and as a throttled warning event. | The manager decides whether to retry; operators see a failing bus without event floods. | Unit test with a mocked bus returning an error. |

## Design

### Ports

| Port | Kind | Connects to | Purpose |
|---|---|---|---|
| `powerRequestIn` | sync input, `Components.ImuPowerRequest` | `ImuManager` | turn the IMU on or off; returns `Fw.Success` |
| `readingGet` | sync input, `Components.ImuReadingGet` | `ImuManager` | read the latest sample; fills `reading`, returns `Components.ImuReadStatus` |
| `busWriteRead` | output, `Drv.I2cWriteRead` | `imuI2cBus.writeRead` | register reads |
| `busWrite` | output, `Drv.I2c` | `imuI2cBus.write` | register writes |

The component is **passive**. Both input ports are `sync`, so each request runs
on the manager's thread, and the driver adds no thread or queue to the flight
controller's 12-thread pool. There is no `run` port: the manager decides how
often to read.

### Shared types (manager ↔ driver contract)

These live in [`Types/Imu.fpp`](../../../Types/Imu.fpp), not in this component,
because they are the hardware-independent boundary. A future IMU driver implements the same ports and
the manager does not change.

```fpp
module Components {
  @ Three-axis vector
  struct Vector3 { x: F32, y: F32, z: F32 }

  @ One IMU sample, in SI units
  struct ImuReading {
    acceleration: Vector3  @< m/s^2
    angularRate: Vector3   @< rad/s
    temperature: F32       @< degrees C
  }

  enum ImuReadStatus: U8 {
    OK = 0           @< reading is valid
    POWERED_OFF = 1  @< IMU is off; bus was not touched
    BUS_ERROR = 2    @< I2C transfer failed; reading is not valid
  }

  port ImuPowerRequest($state: Fw.On) -> Fw.Success
  port ImuReadingGet(ref reading: ImuReading) -> ImuReadStatus
}
```

### Turning on

On `powerRequestIn(ON)`:

1. Read `WHO_AM_I` (`0x0F`). If it is not `0x6C`, emit `ChipIdMismatch` and
   return `FAILURE`.
2. Software reset: write `CTRL3_C` (`0x12`) = `0x01`. Poll `CTRL3_C` until bit 0
   clears, up to a small fixed number of reads. No sleeping inside the sync
   handler.
3. Write `CTRL3_C` = `0x44`: block data update (`BDU`), so a reading never mixes
   the high byte of one sample with the low byte of the next, plus register
   auto-increment (`IF_INC`) for the burst read.
4. Disable I3C: set bit 1 (`I3C_disable`) of `CTRL9_XL` (`0x18`) with a
   read-modify-write, since the register's other bits have non-zero defaults.
   The bus is plain I2C, and Adafruit's library does the same.
5. Write `CTRL1_XL` (`0x10`) = `0x48`: accelerometer 104 Hz, ±4 g.
6. Write `CTRL2_G` (`0x11`) = `0x44`: gyroscope 104 Hz, ±500 dps.
7. Set the state to `ON` and return `SUCCESS`.

Resetting on every ON puts the chip in a known configuration however it was left,
including after a failed ON. If any write fails, the driver writes power-down
where it can, stays `OFF`, and returns `FAILURE`.

### Turning off

On `powerRequestIn(OFF)`: write `CTRL1_XL` = `0x00` and `CTRL2_G` = `0x00`, set
the state to `OFF`, and return the result. OFF while already OFF still writes the
registers, so the command always matches the hardware.

### Reading

On `readingGet`:

1. If `OFF`, return `POWERED_OFF` without touching the bus.
2. One `busWriteRead`: write register `0x20` (`OUT_TEMP_L`), read 14 bytes.
3. Unpack the bytes. Every value is a **little-endian** signed 16-bit integer:

   | Bytes | Registers | Value |
   |---|---|---|
   | 0–1 | `0x20`–`0x21` | temperature |
   | 2–7 | `0x22`–`0x27` | gyro X, Y, Z |
   | 8–13 | `0x28`–`0x2D` | accel X, Y, Z |

4. Convert to SI units:

   | Value | Conversion at the default ranges |
   |---|---|
   | acceleration | raw × 0.122 mg/LSB (±4 g) × 9.80665 / 1000 → m/s² |
   | angular rate | raw × 17.5 mdps/LSB (±500 dps) / 1000 × π/180 → rad/s |
   | temperature | 25 + raw / 256 → °C |

5. Return `OK`, or `BUS_ERROR` if the transfer failed.

The read is on demand. The sensor samples at 104 Hz on its own, so "latest" means
the newest sample in the output registers at the moment of the call.

### Events and telemetry

Readings and IMU health are telemetered by `ImuManager`, which reports them in
hardware-independent terms. The driver reports only what is specific to this
chip and bus.

| Name | Kind | Meaning |
|---|---|---|
| `PowerStateChanged(state)` | event, activity high | the IMU was turned on or off |
| `ChipIdMismatch(expected, actual)` | event, warning high | `WHO_AM_I` did not return `0x6C` |
| `ResetTimeout` | event, warning high | `SW_RESET` did not clear within `RESET_POLL_LIMIT` reads |
| `I2cError(regAddr, status)` | event, warning high, throttle 5 | a bus transfer failed |
| `PowerState` | telemetry, update on change | current `Fw.On` state |

## Configuration

| Item | Where | Value |
|---|---|---|
| I2C bus | `boards/teensy41.overlay`, `&lpi2c1` | Teensy pins 18 (SDA) / 19 (SCL), 100 kHz |
| Device address | `boards/teensy41.overlay`, `lsm6dsox@6a` | `0x6A` (Adafruit default) |
| Kconfig | `prj.conf` | `CONFIG_I2C=y` |

The deployment passes the address to the driver at startup, read from the
devicetree so it has a single source. Both calls are `configComponents` phases in
`FlightControllerDeployment/Top/instances.fpp`:

```cpp
imuDriver.configure(DT_REG_ADDR(DT_NODELABEL(lsm6dsox)));
imuI2cBus.open(DEVICE_DT_GET(DT_NODELABEL(lpi2c1)));  // logs an error if lpi2c1 is not ready
```

**Leave `CONFIG_SENSOR` off.** The overlay's `lsm6dsox` node has
`compatible = "st,lsm6dso"`. With `CONFIG_SENSOR=y`, Zephyr's own LSM6DSO driver
would probe and configure the chip at boot, and the two drivers would fight over
it.

## Reference implementation

Adafruit's Arduino library for this breakout,
[Adafruit_LSM6DS](https://github.com/adafruit/Adafruit_LSM6DS)
(`Adafruit_LSM6DSOX::_init`, `Adafruit_LSM6DS::_read`), is the reference for the
register sequence above. The address, chip ID, 14-byte burst layout, scale
factors and temperature sensitivity in this document match it.

The library itself is not used. It depends on the Arduino core (`Wire`,
`delay()`) through Adafruit BusIO, and neither exists in the Zephyr build.
Porting its register logic onto `Drv.I2c` keeps the bus swappable and the driver
testable with a mocked bus.

Differences from the library, on purpose:

| | Adafruit library | This driver | Why |
|---|---|---|---|
| Gyro range | ±2000 dps | ±500 dps | 4× finer resolution; CubeSat rates are far below 500 dps |
| Reset wait | `delay(1)` loop, unbounded | bounded register poll, no sleep | never block the caller's thread |
| Off | `setAccelDataRate(LSM6DS_RATE_SHUTDOWN)` + gyro same | same registers, one request | the same power-down mode |

## Not yet implemented

- **Range and data-rate parameters.** Ranges are fixed at ±4 g / ±500 dps and the
  data rate at 104 Hz. F Prime parameters could make them adjustable, as
  fprime-sensors' `MpuImu` does.
- **Data-ready check.** The first reading within about 70 ms of ON may predate the
  first real sample. `STATUS_REG` (`0x1E`) could gate the first reading.
- **Automatic recovery.** The driver does not retry or re-initialize after a bus
  error; the manager decides. fprime-sensors uses a reset/configure/run state
  machine for this.
- **Unit tests.**
