# Ground Teensy TX Load Test

## Purpose

This special ground-Teensy image separates the two observed stall classes:

- `rf`: maximum-length local RFM23BP transmissions, without remote ACK waits
- `usb`: queued Teensy-to-GDS USB CDC writes through the production
  `availableForWrite()` path

The normal `build.sh` image does not include the load-test controls. Nothing
transmits until an operator explicitly starts a test on the debug port.

## RF Safety

- Connect the RFM23BP to a correct 433 MHz antenna or 50-ohm RF load before TX.
- Do not touch or change the RF load while transmitting.
- Keep the satellite powered off for the ground-local discriminator test.
- Run the same physical setup at 30 dBm and 28 dBm. Change only TX power.

## Build And Flash

From `GDS_Teensy` on macOS or WSL2:

```bash
./tools/arduino-cli/build_tx_load_test.sh
arduino-cli board list
./tools/arduino-cli/upload_tx_load_test.sh usb:100000
```

Recheck the `usb:*` upload ID every session. Do not upload through a `/dev/*`
path when multiple Teensys are connected.

After testing, restore the production image:

```bash
./tools/arduino-cli/build.sh
./tools/arduino-cli/upload.sh usb:100000
```

## Port Map

The load-test image keeps the normal triple-serial layout:

1. first port: GDS/channel-0 data
2. second port: debug control and `GDS_LOAD` results
3. third port: payload/channel-1 data

Discover current paths with:

```bash
python3 -m serial.tools.list_ports -v
```

Do not run `fprime-gds` during these isolation tests. The host runner owns the
needed serial ports and saves the diagnostic output when `--log` is supplied.

## Test 1: Local RF TX Soak

Power the satellite off. Use the ground debug port only:

```bash
. ../ArtemisRpiTeensy_N2/fprime-venv/bin/activate
python tools/run_tx_load_test.py \
  --debug-port /dev/cu.usbmodemGROUND03 \
  --log /tmp/gds-rf-30dbm.log \
  rf --packets 100 --interval-ms 100 --power-dbm 30
```

Repeat unchanged at 28 dBm:

```bash
python tools/run_tx_load_test.py \
  --debug-port /dev/cu.usbmodemGROUND03 \
  --log /tmp/gds-rf-28dbm.log \
  rf --packets 100 --interval-ms 100 --power-dbm 28
```

Interpretation:

- `completed=100`, zero timeouts/terminal failures: local TX completed for this run.
- `rf_tx_timeouts>0`: local packet-sent completion disappeared; remote ACK and
  range are not involved.
- `rf_terminal_failures>0` plus `RF_FAULT`: preserve the log. Compare `nirq`,
  `rh_mode`, and register values with the known wedge signature.
- failure at 30 dBm but not 28 dBm strengthens a PA-current/power-integrity
  hypothesis; it does not alone prove the electrical mechanism.

## Test 2: Teensy-To-GDS USB TX Load

Keep the satellite off. The host opens and continuously drains the first/data
port while commands and results use the second/debug port:

```bash
python tools/run_tx_load_test.py \
  --debug-port /dev/cu.usbmodemGROUND03 \
  --log /tmp/gds-usb-load.log \
  usb --data-port /dev/cu.usbmodemGROUND01 --bytes 1000000 --chunk-bytes 220
```

Interpretation:

- `host_data_bytes=1000000` and `LOAD_COMPLETE`: USB TX made forward progress.
- transient `usb0_backpressure` followed by `usb0_recoveries`: host draining
  resumed and the queue recovered.
- rising `usb0_backpressure`, a full `usb0_high_water`, and no completion while
  the host still owns/drains the correct data port reproduces the USB CDC stall.
- RF counters should remain unchanged during this test. If they move, the
  satellite was not isolated or another process wrote to the GDS data port.

## Manual Debug-Port Commands

```text
load help
load rf 100 100 30
load usb 1000000 220
load status
load stop
```
