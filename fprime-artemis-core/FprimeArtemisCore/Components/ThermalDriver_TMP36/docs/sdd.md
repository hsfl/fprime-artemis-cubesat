# Components::ThermalDriver_TMP36

> **Status: implemented, not yet tested on hardware.** Builds in
> `FlightControllerDeployment` (Zephyr only); wired as `thermalDriver`.

`ThermalDriver_TMP36` is the driver tier for the board's seven TMP36 analog
temperature sensors. It implements the `ThermalManager` contract
(`Types/Thermal.fpp`) and replaces the ArtemisRpiTeensy_N2
`ThermalDriver_Artemis`, which returned a deterministic fake model.

## How a read works

`ThermalManager` calls `readingGet`. For each sensor i, the driver:

1. clears `m_received[i]`
2. calls `adcRead[i]`, and `ZephyrADCDriver` i converts one sample and calls
   `adcMvIn[i]` back before returning
3. if no callback arrived, uses 0 mV and emits `AdcNoResponse` (a wiring fault)
4. converts: `temperature C = (mV - 500) / 10`
5. marks the sensor valid if 100 mV <= mV <= 1750 mV (-40 C to +125 C, the
   TMP36 rated range)

It returns `OK` if any sensor is valid, `NO_DATA` if none is.

`adcMvIn` is `sync`, not `guarded`: it runs inside `readingGet` on the same
thread, so a component mutex would already be held.

## ADC configuration

- `boards/teensy41.overlay`: pins, channels (10-bit, gain 1, 3.3 V internal
  reference), and the `temp_sensors` node that lists them in sensor order.
- `FlightControllerDeploymentTopology.cpp`: builds the `adc_dt_spec`s from
  `temp_sensors` and calls `configure()` on each `thermalAdc*`. A
  `static_assert` checks the channel count against `THERMAL_SENSOR_COUNT`.
- `prj.conf`: `CONFIG_ADC=y`. `ZephyrADCDriver::configure` asserts if the ADC is
  not ready, so without it the board would assert at boot.

The ADC drivers' `poll` ports are unconnected, and their
`ENABLE_ADC_Schedule` commands do nothing useful here. Their
`LastMvValue`/`LastRawValue` channels are in the packet omit list;
`SensorMillivolts` carries the same data.

## Telemetry and events

| Item | Meaning |
|---|---|
| `SensorMillivolts` | Raw mV per sensor. ~750 mV at room temperature; 0 mV = failed conversion |
| `AdcNoResponse(sensor)` | An ADC did not answer: check the topology connections |

## Limits

- **A disconnected input can float into the valid range.** "Not valid" is
  reliable; "valid" only means the voltage looks like a TMP36.
- Zephyr-only: the driver depends on `fprime-zephyr_Drv_ZephyrADCDriver`, so
  `Components/CMakeLists.txt` registers it only when `ARTEMIS_TARGET_ZEPHYR`.
- The `epscorc3m` satellite sketch converts with 1 mV/F and a 58 mV offset,
  which is wrong for a TMP36 (room temperature would read ~366 C). This driver
  uses the datasheet formula, as `external/artemis-pdu` does.
