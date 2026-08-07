# Neutron 2 Dual-GDS Radio Addressing

## Implemented Contract

Neutron 2 uses RadioHead network ID `0xD2`, protocol version `0x01`, and one
explicit endpoint profile per physical radio. RadioHead's CRC-protected header
is the isolation boundary:

```text
TO | FROM | ID | FLAGS | Artemis segment header | data
```

The existing five-byte Artemis segment header and 44-byte data budget are
unchanged. This is traffic isolation, not encryption, authentication, or
collision avoidance.

| Endpoint profile | `TO` when transmitting | `FROM` | `ID` | `FLAGS` | Paired profile |
| --- | ---: | ---: | ---: | ---: | --- |
| `n2-gds-a` | `0xA2` | `0xA1` | `0xD2` | `0x01` | `n2-spacecraft-a` |
| `n2-spacecraft-a` | `0xA1` | `0xA2` | `0xD2` | `0x01` | `n2-gds-a` |
| `n2-gds-b` | `0xA3` | `0xA4` | `0xD2` | `0x01` | `n2-spacecraft-b` |
| `n2-spacecraft-b` | `0xA4` | `0xA3` | `0xD2` | `0x01` | `n2-gds-b` |

The C3M reference profiles remain available as `c3m-gds` (`C3/A1 -> A2`) and
`c3m-spacecraft` (`C3/A2 -> A1`). Reusing addresses across C3M and Neutron 2 is
safe because their network IDs differ.

The source of truth is [`config/rf_networks.json`](../config/rf_networks.json).
[`config/transport_constants.json`](../config/transport_constants.json) selects
the compatibility-default A pair. Run this after changing either manifest:

```bash
python3 tools/generate_transport_constants.py
python3 tools/check_transport_constants.py
```

Do not hand-edit either Teensy `link_protocol.hpp`, F Prime `LinkCfg.hpp`, or
the generated `ComCfg.<profile>.fpp` files.

## Build And Artifact Selection

Each Teensy build takes a named endpoint profile and writes to a profile-named
directory. Omitting `--profile` deliberately selects the existing A pair.
The wrappers pin Teensy core `1.59.0`, the repository's known-compatible
toolchain, so a future package-index update cannot silently change compilers.

Pair A:

```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh --profile n2-gds-a

cd ../ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh --profile n2-spacecraft-a
```

Pair B:

```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh --profile n2-gds-b

cd ../ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh --profile n2-spacecraft-b
```

Artifacts land under:

```text
GDS_Teensy/build/arduino-cli/<profile>/
ArtemisTeensy_N2_Baremetal/build/arduino-cli/<profile>/
```

The upload wrapper requires the same profile, preventing an A/B artifact from
being silently taken from a shared build directory:

```bash
./tools/arduino-cli/upload.sh --profile <profile> usb:<physical-upload-id>
```

Always get the physical `usb:*` IDs from `arduino-cli board list`. Never upload
by `/dev/*` when more than one Teensy is connected. After flashing, the debug
port prints the compiled profile and outgoing `TO/FROM/ID/FLAGS` tuple.

## F Prime, Payload, And GDS Isolation

N2-A keeps the existing CCSDS spacecraft ID `0x044`. N2-B is assigned `0x045`.
Select the F Prime identity at generation/cross-compilation time:

```bash
cd ArtemisRpiTeensy_N2
fprime-util generate -f -DNEUTRON2_SPACECRAFT_PROFILE=n2-spacecraft-a
fprime-util build

# Or cross-compile the Pi Zero W ARMv6 artifact:
./tools/docker_cross_compile_pi_zero_w.sh \
  --local-only \
  --spacecraft-profile n2-spacecraft-a
```

Use `n2-spacecraft-b` for the second spacecraft. A profile switch forces the
cross-build cache to regenerate, so the binary and dictionary use the matching
CCSDS ID.

The Pi service can load a node-specific environment from
`/home/pi/artemis/current/node.env`. Install the matching file from
`deploy/pi/profiles/`:

| Spacecraft | CCSDS ID | Payload namespace | Pi environment file |
| --- | ---: | --- | --- |
| N2-A | `0x044` | `/tmp/neutron_payload_captures/n2-a` | `n2-spacecraft-a.env` |
| N2-B | `0x045` | `/tmp/neutron_payload_captures/n2-b` | `n2-spacecraft-b.env` |

Ground sessions are separated by the GDS launcher:

```bash
./tools/run_gds_uart.sh --session n2-a --port <GDS-A-data-port> --dictionary <N2-A-dictionary>
./tools/run_gds_uart.sh --session n2-b --port <GDS-B-data-port> --dictionary <N2-B-dictionary>
```

Session A defaults to GUI port `5050`; session B defaults to `5051`. Logs and
received files are separated under `logs/gds/n2-a/` and `logs/gds/n2-b/`.

## Receiver Gate

Both radio drivers run RadioHead in promiscuous mode so the project can classify
every valid RF packet itself. A packet is accepted only when all four header
fields match the compiled endpoint:

```text
ID == RF_NETWORK_ID
TO == RF_LOCAL_ADDRESS
FROM == RF_REMOTE_ADDRESS
FLAGS == RF_PROTOCOL_VERSION
```

Classification happens before ACK handling, reassembly, UART/USB forwarding,
F Prime GDS, or payload decoding. Rejected packets increment
`rf_wrong_network`, `rf_wrong_address`, or `rf_wrong_version`.

## Qualification Matrix

The generator and host-side tests exercise every accepted tuple and each reject
class without radios. Hardware qualification is still required before operating
two pairs together:

| Transmitter | Receiver | Expected result |
| --- | --- | --- |
| N2 GDS-A | N2-A | accepted |
| N2 GDS-B | N2-B | accepted |
| N2 GDS-A | N2-B | rejected: wrong address |
| N2 GDS-B | N2-A | rejected: wrong address |
| C3M GDS | N2-A and N2-B | rejected: wrong network |
| N2 GDS-A/B | C3M spacecraft | rejected: wrong network |

With only one FlatSat pair, safely regression-test the A pair and build all B
artifacts, but defer the cross-pair rows. Do not temporarily flash B on only one
side and interpret the intentionally dead link as full cross-pair proof.

Address filtering cannot prevent same-frequency collisions. Before simultaneous
N2-A/N2-B operations, define an airtime plan such as operator scheduling,
polling, time slots, or separate RF channels.
