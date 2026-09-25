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
4. undoes the board's voltage divider: `TMP36 mV = pin mV * 55.3 / 10`
5. converts: `temperature C = (TMP36 mV - 500) / 10`
6. marks the sensor valid if 100 mV <= TMP36 mV <= 1750 mV (-40 C to +125 C,
   the TMP36 rated range; 18 to 316 mV at the pin)

## Voltage divider

Each TMP36 output reaches the Teensy through a 45.3 kΩ / 10 kΩ divider, the
"Fahrenheit Thermometer Version 1" circuit (TMP36 datasheet Rev. H,
Figure 26). The pin sees `TMP36 mV * 10 / 55.3`, about 1 mV/°F with a 58 mV
offset. Reading the pin as a bare TMP36 gives ~-35 C at room temperature.

It returns `OK` if any sensor is valid, `NO_DATA` if none is.

`adcMvIn` is `sync`, not `guarded`: it runs inside `readingGet` on the same
thread, so a component mutex would already be held.

## ADC configuration

- `boards/teensy41.overlay`: pins, channels (12-bit, gain 1, 3.3 V internal
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
| `SensorMillivolts` | ADC pin mV per sensor, after the divider. ~136 mV at room temperature; 0 mV = failed conversion |
| `AdcNoResponse(sensor)` | An ADC did not answer: check the topology connections |

## Limits

- **A disconnected input can float into the valid range.** "Not valid" is
  reliable; "valid" only means the voltage looks like a TMP36.
- Zephyr-only: the driver depends on `fprime-zephyr_Drv_ZephyrADCDriver`, so
  `Components/CMakeLists.txt` registers it only when `ARTEMIS_TARGET_ZEPHYR`.
- 12-bit ADC at 3.3 V: 0.8 mV per count at the pin, so ~0.45 C per count
  after the divider. At 10-bit it would be ~1.8 C per count.
- The `epscorc3m` and `artemis-cubesat-examples` sketches convert with 1 mV/F
  and a 58 mV offset: the divider's transfer function, so they agree with this
  driver. `external/artemis-pdu` applies the bare TMP36 formula to its own
  A1 input; check that board for a divider before trusting its numbers.
