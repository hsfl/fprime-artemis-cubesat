# Neutron-2 CDR satellite Teensy — radio-free UART/PDU bridge

Work date: October 21, 2026. Target branch: `neutron2-develop`.

## Plan and scope

1. Keep the deployed Pi CDR profile: ComCcsds/TCP/FileDownlink for ground
   traffic, with the independent physical UART mux channel 2 for PDU RPC.
2. Make the satellite Teensy CDR firmware run without RFM23 initialization,
   polling, transmission, radio status reads, or radio pin writes.
3. Preserve Pi `Serial2` at 115200, PDU `Serial1` at 9600, existing framed
   channel-2 request/response bytes, PDU proxy timeouts, and watchdog service.
   Do not change generated transport constants or PDU protocol semantics.
4. Reject unsupported radio operations truthfully. Preserve a clearly named
   legacy RF build only if needed for the existing comparison baseline.
5. Clean-build Teensy 4.1 firmware on macOS, inspect the resulting artifact,
   and check local framing/proxy behavior and transport constants.
6. Stop at reviewed build artifacts. Flashing and physical UART/PDU acceptance
   are separate steps; this task does not authorize an upload or rail switching.

The existing Pi-enable output behavior must be called out explicitly before a
future flash. A radio-free build is not a guarantee that all other GPIOs are
inactive. FSR's SatNOGS/SDR integration is a separate implementation; the old
RFM23 comparison profile does not implement that path.

## Pi readiness checked live

On `n2-obc.local` (`192.168.0.152`), the deployed CDR service was running with
zero restarts. Its open file descriptors included `/dev/ttyAMA0`;
`/dev/serial0` resolves to that device. The UART was configured at 115200,
8 data bits, no parity, one stop bit, with hardware/software flow control off.
The service user belongs to `dialout` and the device is group-accessible.

This establishes Pi-side configuration and an open UART. PDU status remains
UNKNOWN and earlier requests timed out. It does not establish correct physical
wiring, the installed Teensy firmware, the PDU firmware, or a successful PDU
response. Do not open a second UART reader while F′ owns the device.

The existing [Pi setup guide](pi-zero-w/SETUP.md#2-wire-and-enable-uart) specifies
3.3 V TTL, crossed TX/RX and common ground:

| Pi Zero W | Satellite Teensy 4.1 |
| --- | --- |
| GPIO14/TX, physical pin 8 | pin 7 / Serial2 RX |
| GPIO15/RX, physical pin 10 | pin 8 / Serial2 TX |
| GND | GND |

The physical harness has not been inspected during this task.

## Acceptance results

The CDR profile is selected with `ARTEMIS_CDR_NO_RADIO`. It compiles out the
radio driver instance, RF receive/transmit paths, radio-status reads, radio pin
setup, and radio-traffic LED updates. The existing framed UART parser and PDU
proxy remain in use; channels 0/1 are dropped in this profile. Well-formed
radio-status RPCs return `TEENSY_STATUS_TARGET_ERROR`, rather than fabricated
healthy radio telemetry. This profile does not change the deployed Pi binary.

The host test compiles the actual relay, router, and PDU proxy against in-memory
serial streams, without radio or SPI libraries:

```sh
./tools/tests/teensy_cdr/run.sh
python3 tools/generate_transport_constants.py --check
python3 tools/check_transport_constants.py
```

Passed: fragmented channel-2 request/response roundtrip with exact returned
bytes, CRC-corrupt frame rejection, PDU timeout and subsequent recovery,
unsupported RF-status rejection, and channel-0/1 isolation through repeated
polls. Generated transport headers and shared transport constants match.
These are software checks, not a hardware UART/PDU test.

The startup still asserts the existing Pi-enable GPIO (Teensy pin 36 HIGH),
lights the status LED (pin 13), and arms the 12-second hardware watchdog.
Those retained behaviors must be reviewed against the actual harness before
any later flash. The CDR change removes radio GPIO activity only.

Clean Teensy 4.1 compilation passed. From `ArtemisTeensy_N2_Baremetal/`:

```sh
./tools/arduino-cli/build.sh              # default: cdr
./tools/arduino-cli/build.sh legacy-rf    # historical RFM23 comparison only
```

macOS selects native ARM64 Arduino CLI. Windows uses the same script in WSL2.
CDR outputs remain at `build/arduino-cli/`, compatible with the existing upload
helper. Legacy outputs use `build/arduino-cli-legacy-rf/`, so a legacy build
does not replace the default CDR artifact. `firmware-profile.txt` is written
only after successful compilation; a startup banner also identifies the profile.
The source defaults to CDR even outside this build script. An explicit
`ARTEMIS_CDR_LEGACY_RF` definition selects the historical radio build.

Built from the modified working tree based on commit
`a468d2899bce32157c7eb4f672cf22bdefe05278`; these firmware edits are not committed.

| Default CDR artifact | SHA-256 |
| --- | --- |
| `build/arduino-cli/satellite_teensy.ino.hex` | `df135fe4531e5e753a9846ac2a9e6fe5b511d63e73876e09a394b37fb722e654` |
| `build/arduino-cli/satellite_teensy.ino.elf` | `474a365b93d11d6c27c1b47940dff84148d1c63c82be4bee41a0e4118ab5ccf4` |

Independent `arm-none-eabi-nm -C` inspection found no `Rf23Driver`, `RH_RF22`,
`RHHardwareSPI`, `hardware_spi`, `SPIClass`, or RF send/receive-path symbols in
the CDR ELF. The framed UART parser, PDU proxy request/response methods, and
Teensy hardware serial symbols remain present. Independent source review found
no actionable correctness issues. This is build and software-transport proof;
the host tests use a synthetic PDU envelope and do not validate real summary
status decoding or electrical/watchdog behavior.

The legacy comparison profile also compiled successfully in its separate
directory. CDR FLASH code uses 43,488 bytes and RAM1 variables use 14,048 bytes
(legacy: 56,240 and 32,352 bytes respectively). Full compile logs are under
`ArtemisTeensy_N2_Baremetal/build/logs/`. The default build directory also holds
`cdr-build-manifest.json` with artifact/source hashes, the base commit, working
tree status and validation scope; the commit alone does not identify these edits.

## Authorized satellite flash — October 21, 2026

After build-only acceptance, Dennis explicitly authorized identifying and
flashing the satellite Teensy. The attached Teensy 4.1 had USB serial
`11550220`, debug port `/dev/cu.usbmodem115502201`, and current physical upload
ID `usb:100000`. Its serial/debug port matches the historical satellite map.
The upload ID is location-derived; the old `usb:2100000` mapping was not used.

The default CDR profile marker and HEX SHA-256 above were rechecked, then
`./tools/arduino-cli/upload.sh usb:100000` completed with exit status zero.
Post-flash serial output contained repeated `[ArtemisTeensy] counters` lines:
UART RX increased from 0 to 80 bytes and TX from 0 to 44 bytes during the
short observation; all RF counters remained zero. The debug capture is at
`ArtemisTeensy_N2_Baremetal/build/logs/teensy-cdr-post-flash.log`, with the flash
identity/status in `build/arduino-cli/cdr-flash-manifest.json`.

The Pi F′ service remained active with zero restarts. PDU health remained
UNKNOWN. This proves post-flash satellite runtime and Pi/Teensy UART byte flow;
it does not prove a successful PDU status response, rail operation, payload
acquisition, watchdog recovery or RF behavior. No capture, rail-switching or
RF commands were issued.

## Pre-commit verification — October 26, 2026

Both profiles were clean-built again without flashing. The CDR HEX and ELF
SHA-256 values exactly match the artifacts recorded above. The host channel-2
smoke test and both transport-constant checks passed again; independent review
found no remaining actionable firmware changes. Logs are retained locally at
`ArtemisTeensy_N2_Baremetal/build/logs/cdr-precommit-20261026.log` and
`legacy-rf-precommit-20261026.log`. This rerun adds build/host evidence only;
it does not establish a current hardware connection or PDU response.
