# Neutron 2 Radio Architecture Summary

## BLUF

Neutron 2 can use the same RFM23BP hardware, RadioHead library, Artemis RF
segmentation, and ground station for two spacecraft (`N2-A` and `N2-B`). Give
both spacecraft the Neutron 2 mission-network ID, but give each spacecraft a
unique RF node address and CCSDS spacecraft identity. Start with one
ground-selected active spacecraft at a time. Supporting simultaneous downlink
later requires source-aware ground routing and coordinated airtime.

## The Existing Four-Byte RadioHead Header

Every RadioHead RF packet already carries four CRC-protected header bytes:

```text
TO | FROM | ID | FLAGS
```

Before mission isolation was enabled, this project left them at RadioHead's
defaults:

```text
TO    = 0xFF  broadcast
FROM  = 0xFF  unspecified/broadcast sender
ID    = 0x00
FLAGS = 0x00
```

The bytes were still transmitted and protected by the radio CRC, but the
application assigned them no useful identity. Compatible nearby Artemis links
therefore looked the same at the RadioHead layer.

The link now assigns them as follows:

- `TO`: intended RF node.
- `FROM`: transmitting RF node.
- `ID`: mission/network identity.
- `FLAGS`: link-protocol version.

The receiver rejects a wrong network, direction, or version before ACK
handling, RF reassembly, UART/USB forwarding, F Prime GDS, or payload decode.
This adds no on-air bytes and does not reduce the existing 49-byte RF packet or
44-byte Artemis segment payload.

## Current C3M Mapping

```text
Mission network: 0xC3
Ground node:     0xA1
Satellite node:  0xA2
Protocol version: 1
```

Neutron 2 network ID `0xD2` is reserved in `config/rf_networks.json`.

## Recommended Neutron 2 A/B Mapping

| Node | Network ID | RF address | Role |
| --- | --- | --- | --- |
| N2 ground station | `0xD2` | `0xA1` | Shared ground radio |
| N2-A | `0xD2` | `0xA2` | Spacecraft A |
| N2-B | `0xD2` | `0xA3` | Spacecraft B |

Example command headers:

```text
Ground -> N2-A: TO=0xA2 FROM=0xA1 ID=0xD2 FLAGS=1
Ground -> N2-B: TO=0xA3 FROM=0xA1 ID=0xD2 FLAGS=1
```

Each satellite accepts only commands addressed to its own node. Downlink uses
the ground address as `TO` and the spacecraft's unique address as `FROM`.

## One Ground Station Serving Two Spacecraft

### Recommended first phase: active-target operation

Use one physical ground Teensy and one GDS operations flow, but select either
`N2-A` or `N2-B` as the active target. The ground Teensy applies the selected
destination address to outgoing commands. Both spacecraft use the same source
code and RF library; their generated or provisioned identity differs.

This is the simplest operator model and avoids mixing two spacecraft with the
same command, telemetry, and payload definitions in one undifferentiated GDS
stream.

### Later phase: simultaneous operations

If both spacecraft must downlink at the same time, the ground bridge must retain
the RadioHead `FROM` identity and route each spacecraft into a distinct ground
stream or GDS session. N2-A and N2-B should also have unique CCSDS spacecraft
IDs, product identities, and ground-storage namespaces. RF identity alone is
not sufficient once the Ground Teensy strips the RadioHead header.

Simultaneous operations also require an airtime plan, such as scheduled
transmit windows, polling, TDMA-like time slots, or separate RF channels. The
identity header can reject another spacecraft's intact packet, but it cannot
prevent two same-frequency transmissions from colliding physically.

## Identity Layers

Keep spacecraft identity consistent at multiple layers:

| Layer | Identity purpose |
| --- | --- |
| RadioHead network ID | Separates Neutron 2 from C3M or other nearby missions |
| RadioHead node address | Separates ground, N2-A, and N2-B |
| CCSDS spacecraft ID | Identifies the spacecraft after RF headers are removed |
| Payload/product metadata | Prevents files and science products from being mixed |
| Ground routing/session | Keeps operator displays and commanding unambiguous |

Recommended per-spacecraft configuration:

```text
mission_network = neutron2
spacecraft_id   = N2-A or N2-B
rf_address      = unique node address
ccsds_scid      = unique spacecraft ID
```

Prefer one shared codebase with generated build profiles or provisioned
identity, not separate hand-maintained forks.

## Conference and Nearby-Link Scope

This architecture protects against accidental cross-talk when C3M, N2-A, or
N2-B operate in nearby conference booths or rooms. Walls, booth orientation,
and lack of line of sight reduce reception probability, but they are not a
reliable isolation mechanism for a high-power RFM23BP link.

This mechanism provides traffic isolation only. It does not provide:

- encryption;
- cryptographic authentication;
- protection from intentional spoofing;
- protection from RF interference or packet collisions.

## Validation Status

The C3M identity implementation has passed generated-contract checks,
host-executed header-classification tests, both Teensy firmware builds, the
native F Prime build and component tests, and the laptop-only C3M mission demo.
No firmware was flashed. Same-mission and cross-mission HIL qualification is
deferred to a future bench session.
