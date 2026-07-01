# Basic Radio Ping-Pong

This folder is a clean first hardware test for new students.

Goal:
- upload one sketch to the ground Teensy
- upload one sketch to the satellite Teensy
- watch the ground board send `g`
- watch the satellite board answer with `pong from satellite`

This is intentionally simpler than the main relay firmware. It does not use:
- F'
- UART relay framing
- RF segmentation
- command parsing

It only proves the most basic project assumption:
- the two Teensy 4.1 + RF23BP boards can exchange packets over the air

## Folder Layout

- `ground_radio_ping_pong/`
  - sketch for the ground Teensy
- `satellite_radio_ping_pong/`
  - sketch for the satellite Teensy

## Hardware Assumptions

These sketches match the existing project wiring:

- board: `Teensy 4.1`
- radio library: `RadioHead`
- radio chip path: `RH_RF22`
- SPI bus: `SPI1`
- RF frequency: `433.0 MHz`
- modem config: `GFSK_Rb125Fd125`
- TX power: `RH_RF22_RF23BP_TXPOW_30DBM`
- RF23BP control polarity:
  - receive = `RX_ON LOW`, `TX_ON HIGH`
  - transmit = `RX_ON HIGH`, `TX_ON LOW`

Pin use:

- radio chip select: `38`
- radio interrupt: `40`
- RF23BP RX enable: `30`
- RF23BP TX enable: `31`
- LED: `13`

Satellite-only extra pin:

- Raspberry Pi power enable: `36`

## What The Sketches Do

Ground behavior:
- boots the radio
- sends `g` continuously with a 2 second pause between cycles
- waits up to 1 second for a reply
- prints either `pong from satellite` or a timeout

Satellite behavior:
- boots the radio
- waits for `g`
- replies with `pong from satellite`

Both sketches include a short amp settle delay before and after each RF state change so the RF23BP front-end has time to switch cleanly between receive and transmit.

## Build

Build the ground sketch from the ground workspace so Arduino CLI uses the same Teensy package cache as the project:

### macOS

```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile \
  --fqbn teensy:avr:teensy41 \
  --build-path "$PWD/build/basic-radio-ping-pong-ground" \
  ../student_onboarding/basic_radio_ping_pong/ground_radio_ping_pong
```

Build the satellite sketch from the satellite workspace:

```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile \
  --fqbn teensy:avr:teensy41 \
  --build-path "$PWD/build/basic-radio-ping-pong-satellite" \
  ../student_onboarding/basic_radio_ping_pong/satellite_radio_ping_pong
```

### Windows Laptop (WSL2)

```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile \
  --fqbn teensy:avr:teensy41 \
  --build-path "$PWD/build/basic-radio-ping-pong-ground" \
  ../student_onboarding/basic_radio_ping_pong/ground_radio_ping_pong
```

```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
export ARDUINO_CONFIG_FILE="$PWD/tools/arduino-cli/arduino-cli.yaml"
arduino-cli compile \
  --fqbn teensy:avr:teensy41 \
  --build-path "$PWD/build/basic-radio-ping-pong-satellite" \
  ../student_onboarding/basic_radio_ping_pong/satellite_radio_ping_pong
```

## Upload

Use Arduino IDE, Teensy Loader, or `arduino-cli upload` if your local setup is already working.

This repo’s existing upload helpers still apply for the main firmware workspaces, but this onboarding example is meant to be simple enough that compiling and uploading by sketch path is explicit.

## Walkthrough

1. Connect both Teensy boards over USB.
2. Upload `satellite_radio_ping_pong` to the satellite board.
3. Upload `ground_radio_ping_pong` to the ground board.
4. Open a serial monitor for the ground board at `115200`.
5. Open a serial monitor for the satellite board at `115200`.
6. Reset both boards if needed.

Expected ground output:

```text
[ground] radio ready
[Ping 1] Sending ping to satellite...
[ground] rx: pong from satellite
[Ping 2] Sending ping to satellite...
[ground] rx: pong from satellite
```

Expected satellite output:

```text
[satellite] radio ready
[satellite] rx: g
[satellite] tx: pong from satellite
[satellite] rx: g
[satellite] tx: pong from satellite
```

## If It Fails

If both radios fail to initialize:
- check power
- check RF23BP wiring
- confirm both boards are really Teensy 4.1

If ground sends `g` but never receives `pong from satellite`:
- verify both radios use `433.0 MHz`
- verify both radios use the same modem config
- verify one board is not still running the main relay firmware
- verify antennas are connected
- move the boards farther apart if they are extremely close

If satellite sees packets but ground never sees replies:
- verify the TX/RX enable pins are wired the same way on both boards
- verify the ground board is actually returning to receive mode after transmit

## Why This Test Exists

Students should pass this test before touching:
- the UART relay
- segmented RF transport
- the F' deployment

If ping-pong fails here, the problem is probably hardware, radio setup, or very basic board bring-up.
