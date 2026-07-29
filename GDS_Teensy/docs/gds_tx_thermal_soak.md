# GDS OBC RF TX Soak

`firmware/gds_tx_thermal_soak/` is a standalone, TX-only Arduino sketch for
the ground Teensy OBC. It is intentionally separate from the normal GDS
bridge: no GDS relay, receive path, or peer ACK is involved.

## Why this tool exists

During several weeks of C3M GDS-node testing, the RFM23BP sometimes entered an
intermittent local TX wedge: packets stopped completing and recovery required
either a manual reset or the firmware's SDN recovery path. The symptom was
below F Prime and the normal GDS relay, so this standalone loop holds the
software path constant while stressing the ground OBC transmitter.

The working hardware diagnosis was an unstable RF load/feed rather than a
single deterministic software bug. Suspects included reflected RF power and,
more concretely, intermittent contact between the steel monopole and the
antenna pad. The antennas are now soldered directly to the antenna pad instead
of relying on mechanical pressure alone. That improved measured SWR and changed
the observed meter reading from roughly 20–30 ohms to 40–55 ohms.

Those DC meter readings are **not** a proof of a 50-ohm RF impedance. Treat
them as a contact-screening observation; verify the installed antenna and
counterpoise with a calibrated NanoVNA/SWR sweep before sustained high-power
TX. This tool records local TX completion and recovery behavior, not radiated
link quality or the electrical root cause by itself.

On every boot it initializes the RFM23BP at 433 MHz / GFSK Rb125Fd125 / 30 dBm,
then continuously sends full 49-byte packets. It never calls the receive path
or starts the GDS relay. There is no intentional inter-packet delay. The radio
must briefly become idle after every packet-sent completion so RadioHead can
start the next packet; it never enters receive mode.

The sketch reports TX counters and fault snapshots on the second USB serial
port while connected to a laptop. After flashing, every power-up starts the
soak automatically; remove power to stop it. After testing, flash the normal
GDS image back.

Pin 13 blinks every 250 ms while confirmed TX packets continue. It latches
solid HIGH immediately on a TX failure, during recovery, or while the radio is
not initialized; it resumes blinking only after a later successful packet.

Before applying power, connect a correct 433 MHz antenna or rated 50-ohm RF
load and do not change that load during transmission.

For an antenna comparison, keep the power, packet size, test duration, radio,
and receiver geometry fixed. This tool proves local TX completion; record
remote receive count, CRC/pass rate, and RSSI separately to compare antennas.

```bash
cd GDS_Teensy
./tools/arduino-cli/build_tx_thermal_soak.sh
arduino-cli board list
./tools/arduino-cli/upload_tx_thermal_soak.sh usb:100000
```

Restore normal GDS firmware:

```bash
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000
```
