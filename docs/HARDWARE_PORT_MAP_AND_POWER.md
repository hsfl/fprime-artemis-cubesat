# C3M Hardware Port Map & Power Bring-Up

A practical reference for the current C3M student bench: which USB/serial
device is which, how to tell the two Teensies apart when using the cold
fallback, and how to power the bench safely. This is the doc to open when "it's
plugged in but which port do I use?" or "are we ready to power this from the PDU
yet?"

See [`SYSTEM_ARCHITECTURE.md`](SYSTEM_ARCHITECTURE.md) for why the link is built this way, and the [Glossary](GLOSSARY.md) for terms.

## Current student ground baseline

```text
Mac USB -> HackRF One -> 9--10 inch vertical monopole, no attenuator
```

Start it with the single fixed launcher in
[`HACKRF_GROUND_STATION_RUNBOOK.md`](HACKRF_GROUND_STATION_RUNBOOK.md). The
tested profile is `433 MHz`, TX gain `16`, RX LNA/VGA `8/8`, ACK mode, RF
amplifier off, antenna bias off, and a `100 ms` TX settle lead. There is no
AGC, adaptive gain, automatic power control, or student runtime tuning.

The HackRF does not enumerate as three serial ports. The launcher owns the USB
device and creates the channel-0 and channel-1 virtual paths consumed by GDS
and the payload receiver. Use `hackrf_info` to confirm the device. Do not also
connect or start the ground Teensy stack.

This baseline is specific to the tested Mac, antenna, direct/no-attenuator RF
path, USB path, separation, and geometry. Any physical change is an engineering
requalification, not a reason for students to adjust gains.

## Bench devices

| Device | Role | How the host sees it |
| --- | --- | --- |
| Satellite Raspberry Pi Zero W | Hosts the F´ flight-software deployment | SSH over the network; talks to the satellite Teensy on its own `/dev/serial0` UART |
| Satellite Teensy 4.1 | UART↔RF relay + local subsystem RPC | One USB serial (debug console) when plugged into a laptop |
| HackRF One | **Primary** C3M ground RF adapter | One USB SDR visible to `hackrf_info`; software provides channel 0 and channel 1 |
| Ground Teensy 4.1 + RFM23BP | **Cold fallback** ground RF adapter | **Three** USB serial ports (triple-serial), see below |

## Cold-fallback USB serial enumeration

This section is only for the ground Teensy/RFM23BP fallback and optional
satellite debug USB. There is no fixed serial-device name — the suffix depends
on the machine and which USB port you used. **Always enumerate, don't
hardcode.**

- **macOS:** `ls /dev/cu.usbmodem*`
- **Windows (WSL2):** `ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null`
- **Raspberry Pi → satellite Teensy:** the Pi's hardware UART is `/dev/serial0` (not USB).

Tip: unplug everything, plug in **one** Teensy, run the enumerate command, and note which port(s) appear before adding the next device. Label the physical USB cables.

On Windows, the Teensy must be attached into WSL2 before these Linux device
names appear. See
[`STUDENT_WINDOWS_LAPTOP_SETUP.md`](STUDENT_WINDOWS_LAPTOP_SETUP.md) for the
`usbipd-win` bind/attach/detach workflow. Those instructions cover the existing
Teensy fallback workflow; they do not qualify the current HackRF baseline on
Windows.

## Cold fallback: Ground Teensy three USB serial ports

When the ground Teensy is built with `USB_TRIPLE_SERIAL`, it presents three ports to the laptop. **The port index is its own axis — it is *not* the same as the satellite UART channel numbers.** (See [Transport Architecture](SYSTEM_ARCHITECTURE.md#the-three-ground-usb-serial-ports).)

| Ground USB serial port | Firmware name | Use it for | Carries |
| --- | --- | --- | --- |
| 0 | `Serial` | `fprime-gds` data port | RF channel 0 (CCSDS) |
| 1 | `SerialUSB1` | Read-only **debug** console (UART+RF link counters) | diagnostics only |
| 2 | `SerialUSB2` | `tools/payload_receiver.py` | RF channel 1 (payload) |

On macOS these usually appear as three consecutive `…usbmodemXXXX1/3/...` suffixes from the same device; confirm by opening port 1 and watching for the periodic counter print.

## Example device map (illustrative — yours will differ)

From the RF debug session in [`archive/RF_CHAIN_ROOT_CAUSE_ANALYSIS_2026-04-24.md`](archive/RF_CHAIN_ROOT_CAUSE_ANALYSIS_2026-04-24.md). **Treat the exact suffixes as examples**, not constants:

| Device | Example port | Purpose |
| --- | --- | --- |
| Satellite Teensy | `/dev/cu.usbmodem115502201` | USB debug console |
| Ground Teensy | `/dev/cu.usbmodem115551201` | GDS data stream (port 0) |
| Ground Teensy | `/dev/cu.usbmodem115551203` | debug console (port 1) |
| Raspberry Pi | `192.168.0.152` (`artemis-pi`) | F´ flight target over SSH |

The Pi runs the deployment as a systemd service (`artemis-fprime.service`) executing `ArtemisRpiTeensyDeployment -d /dev/serial0`. Stop the service before re-deploying a new binary (it holds the file open).

## Powering the bench

### Current reality: USB power only

The current ground HackRF is USB-powered by the Mac. The satellite OBC/Teensy
and the fallback ground Teensy have been brought up with USB power. The PDU,
battery board, and solar panels are **not yet integrated** into this bring-up.
This is the safe default for software/RF work.

### Future: power from the Artemis bus (PDU → battery → solar)

Moving off USB means bringing up the EPS chain: PDU v2.2 + battery board v2, and eventually solar panels (the full [Artemis CubeSat Kit](https://sites.google.com/hawaii.edu/artemiscubesatkit) bus). Drive the PDU only through `EpsManager` / `EpsDriver_Artemis` (PDU v2 protocol), never raw packets. The authoritative wire format is the [PDU Protocol ICD](../external/artemis-pdu/PDU_PROTOCOL_ICD.md).

### PDU rail map (PDU v2 protocol)

| ID | Rail | Notes |
| --- | --- | --- |
| `0x01` | `PDU_OUTPUT_3V3_1` | 3.3 V rail 1 |
| `0x02` | `PDU_OUTPUT_3V3_2` | 3.3 V rail 2 |
| `0x03` | `PDU_OUTPUT_5V_1` | 5 V rail 1 |
| `0x04` | `PDU_OUTPUT_5V_2` | 5 V rail 2 |
| `0x05` | `PDU_OUTPUT_5V_3` | 5 V rail 3 |
| `0x06` | reserved | 5 V input to the 12 V switch regulator (not independently commandable) |
| `0x07` | `PDU_OUTPUT_12V` | 12 V logical rail |
| `0x08` | `PDU_OUTPUT_VBATT` | Battery bus enable |
| `0x09` | `PDU_OUTPUT_BURN1` | ⚠️ Burn-wire deployment channel 1 |
| `0x0A` | `PDU_OUTPUT_BURN2` | ⚠️ Burn-wire deployment channel 2 |

### ⚠️ Safety notes

- **Never fire the burn-wire channels (`BURN1`/`BURN2`) on the bench.** They are antenna/deployment release channels — they get hot and are for real deployment only. The ICD keeps burn operations time-bounded for this reason; do not defeat that.
- Bring rails up deliberately and watch current; do not enable everything at once when first moving off USB.
- Confirm battery polarity and charge state before connecting the battery board.
- Keep one known-good USB-power fallback configuration documented so you can always return to a working bench.

> **TODO (team to fill in):** exact per-rail load assignment (which subsystem hangs off which rail), nominal voltages/currents observed on the bench, and the verified safe power-on sequence once the bench runs off the PDU.
