# Radio-Agnostic Comms and Payload Downlink Plan

Date: 2026-06-09
Status: design plan with core channelized mux + channel 1 payload path now implemented; HIL validation still pending
Audience: future agents and students working on the Neutron 2 / Artemis F Prime stack

## BLUF

Payload downlink "outside the GDS GUI" is really a **multiplexing problem**, not a protocol problem. The EPSCOR demo already proved the RFM23BP can move a ~40 KB product with indexed packets + CRC + retry bitmap. The hard part is that payload bytes, the GDS CCSDS stream, and satellite-local subsystem RPC must share **one Pi UART**:

- `/dev/serial0` is owned exclusively by the F Prime process (`Drv.LinuxUartDriver`).
- The ground Teensy GDS data port is owned exclusively by `fprime-gds`.

Recommended solution: **tagged virtual channels over the existing bridge**, with RF forwarding only for the channels that need it. Channel 0 stays byte-identical CCSDS (the GDS GUI cannot break). Channel 1 carries EPSCOR-style payload packets over RF, surfaced on a third ground-Teensy USB serial port to a small Python receiver. Channel 2 carries satellite-Teensy-local subsystem RPC such as EPS/PDU and is not forwarded over RF.

Equally important: every piece of this design is placed at a **swappable seam**, because the strategic goal of this repo is not the RFM23BP. The mission demo is the bounded objective ("left/right bounds"); the lasting deliverable is an F Prime architecture where future students can swap the radio (256-byte UART radio, SatNOGS board) or the payload without re-architecting.

## Mission Framing: Bounded Demo, Lasting Foundations

Two goals coexist in this repo and they must not be confused:

| Goal | Scope | Rule |
| --- | --- | --- |
| N2 FlatSat demo | Base Mode -> SOH -> scheduled collect -> science product -> downlink -> ground review | Smallest implementation that makes the story credible. Frozen path protected by `tools/demo_rf_mvp_smoke.sh`. |
| F Prime foundations | Component/topology architecture future students inherit | Every radio- or payload-specific decision must be isolated behind a named seam so it can be replaced without touching the rest. |

Practical test for any change: *"If we swapped the RFM23BP for a 256-byte-MTU UART radio tomorrow, how many files would this change touch?"* If the answer includes mission logic, the design is wrong.

## Current State (updated 2026-06-26)

- F Prime deployment (`ArtemisRpiTeensy_N2`) runs on Pi Zero W, cross-compiled, systemd-managed.
- End-to-end RF command path proven: `fprime-gds` -> ground Teensy -> RFM23BP -> satellite Teensy -> Pi `/dev/serial0` -> `missionManager.PING` pong.
- GDS decodes valid 128-byte CCSDS TM frames after per-segment RF ACK was added; residual APID sequence warnings = occasional dropped packets under sustained downlink.
- Channel 1 payload traffic also uses per-segment RF ACK/retry. This is required
  because payload-level retry only works after the receiver gets enough header
  state to know what is missing.
- Demo path is frozen per `docs/RF_MVP_DEMO_RUNBOOK.md`.
- `REQUEST_SCIENCE_DOWNLINK` now starts the file-backed channel 1 `PayloadDownlinkManager` path and reports completion through `CommsManager` after the payload manager completes.
- The channel 1 path transfers real staged payload bytes and reconstructs them
  with `tools/payload_receiver.py`, but is not a stock GDS `#Downlink` file
  transfer.
- `docs/PAYLOAD_DOWNLINK_PROTOCOL_ADVICE.md` already concluded: do not push the ~40,368-byte product through stock `Svc.FileDownlink` over this link. This plan is the concrete architecture for its "Option 1".

## The Core Problem: One Medium, Two Traffic Classes

```text
fprime-gds (Mac)          payload receiver (Mac)
      |                          |
      +------ ground Teensy -----+        <- one USB device, multiple serial endpoints
                  |
              RFM23BP RF hop                <- one radio, 49-byte packets, lossy
                  |
           satellite Teensy
                  |
            Pi /dev/serial0                 <- one UART, owned by one process (F Prime)
                  |
        F Prime deployment (Pi)             <- file lives here
```

Any payload downlink that bypasses the GDS still has to traverse the same UART and the same radio as the GDS CCSDS stream. EPS/PDU control has to traverse the same Pi UART but terminates at the satellite Teensy. So the design question is: how do these traffic classes share the link without corrupting each other?

## Alternatives Considered and Rejected

| Option | Why rejected |
| --- | --- |
| Stock `Svc.FileDownlink` through the CCSDS stream | ~40 KB -> ~316 TM frames -> ~948 RF packets; one lost RF packet corrupts an entire CCSDS frame; link still shows APID gaps. Right end-state, wrong now. See graduation criteria below. |
| Payload chunks as F Prime telemetry channels | Same fragile CCSDS path plus dictionary overhead per chunk; telemetry is not a bulk-data plane. |
| Time-division mode switching ("Teensy, enter payload mode") | Stateful link modes desynchronize on a lossy link: lose one mode-switch packet and both sides disagree about what the byte stream means, corrupting the GDS stream. Avoid shared mode state entirely. |
| Second physical radio / second Pi UART for payload | No hardware budget on the Artemis OBC for the demo; also dodges the architecture lesson instead of teaching it. |

## Recommended Design: Stateless Virtual Channels

Principle: **every frame at every hop self-identifies its channel**. No mode state, no desync, and the GDS-facing byte stream stays pure CCSDS.

### Layer 1 — RF hop (zero header growth)

Second segment magic byte in the Teensy link protocol
(`ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp` and the GDS_Teensy mirror):

- `0xA5` = channel 0, CCSDS tunnel (existing)
- `0xA6` = channel 1, payload protocol (new)

Same 5-byte segment header, same 44 useful data bytes. Receivers route by magic. Channel 2 is intentionally absent from RF; it is local to the satellite Teensy.

Reliability split per channel:

- **Channel 0 keeps per-segment ACK/retry.** That is what made GDS decode cleanly; do not touch it.
- **Channel 1 uses no per-segment ACK.** Blast all packets, then recover at the file layer via retry bitmap (the proven EPSCOR approach). Avoids ~80 ms x ~918 packets of stop-and-wait, and puts reliability where it belongs for bulk data.
- **Channel 2 is not forwarded over RF.** It is bounded request/response traffic between the Pi and satellite Teensy only, currently used for EPS/PDU RPC.

### Layer 2 — Pi <-> Teensy UART (the one contract change)

Today the satellite Teensy treats Pi UART bytes as an opaque stream chunked into 128-byte TM frames; it cannot tell payload bytes from CCSDS bytes. Fix: extend the existing (already-implemented, currently fallback-only) `0xD4 0xC3 + len + crc16` wrapper with **one channel byte**, and wrap *both* streams on this hop:

- channel 0 frame = CCSDS/GDS bytes for the stock F Prime communications path
- channel 1 frame = exactly one payload-protocol packet
- channel 2 frame = one local satellite-Teensy subsystem RPC packet, currently EPS/PDU

Side benefit: explicit framing replaces the fragile "count to 128 bytes and hope" chunker and the stale-partial-drop hack, eliminating the 7-byte-fragment bug class from the 2026-04-24 debug sessions.

Uplink is symmetric for RF channels: GDS command bytes arrive as channel 0; ground-helper retry bitmaps arrive as channel 1. Channel 2 requests originate on the Pi and terminate at the satellite Teensy.

Update `ArtemisTeensy_N2_Baremetal/docs/uart_contract_mvp.md` and `GDS_Teensy/docs/transport_contract.md` in the same PR as the firmware change (per AGENTS.md pitfall list).

### Layer 3 — F Prime side (Pi)

Two new components, inserted at the stock Communication Adapter seam visible in
`ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/topology.fpp` (the `comStub <-> comDriver` connections):

**`UartChannelMux`** (passive): wraps/unwraps the channel framing.

```text
ComCcsds.comStub.drvSendOut        -> uartMux.ccsdsSendIn    (wrap ch0)
payloadDownlinkManager.packetOut   -> uartMux.payloadSendIn  (wrap ch1)
epsAdapterArtemis.teensyRequestOut -> uartMux.localSendIn    (wrap ch2)
uartMux.drvSendOut                 -> comDriver.$send

comDriver.$recv                    -> uartMux.drvRecvIn      (unwrap, route by channel)
uartMux.ccsdsRecvOut               -> ComCcsds.comStub.drvReceiveIn
uartMux.payloadRecvOut             -> payloadDownlinkManager.packetIn
uartMux.localRecvOut               -> epsAdapterArtemis.teensyResponseIn
```

**`PayloadDownlinkManager`** (active): owns the EPSCOR-style file protocol.

- Commands: `START_PAYLOAD_DOWNLINK(productId, byteCount)`, `ABORT_PAYLOAD_DOWNLINK`, `GET_PAYLOAD_STATUS`
- Packet types: header (magic, transfer ID, file length, total packets, file CRC16); data (transfer ID, 2-byte index, payload bytes, CRC16); end; retry-request (missing-packet bitmap); retry data
- Telemetry (small, status only — never file bytes): `PayloadState`, `TotalPackets`, `PacketsSent`, `RetryRound`, `PacketsMissing`, `LastError`
- Reads arbitrary staged bytes from `NEUTRON_PAYLOAD_DOWNLINK_FILE`, `/tmp/neutron_payload_captures/latest_payload.bin`, or the latest simulator CSV in `/tmp/neutron_payload_captures`.
- Pacing: emits N data packets per rate-group tick (configurable) so Teensy relay queues (depth 32) never overflow. Tune on the bench.

Judges watch progress in the GDS GUI (the status telemetry) while the actual bytes flow on channel 1. The GUI is part of the show and cannot break, because it never sees a payload byte.

### Layer 4 — Ground side

- Build ground Teensy with `usb=serial3` (Triple Serial):
  - `Serial` = GDS CCSDS stream (unchanged)
  - `SerialUSB1` = debug counters (unchanged)
  - `SerialUSB2` = payload channel (new)
- New `tools/payload_receiver.py` on the laptop: tracks received packet indices, sends retry-bitmap requests (uplink ch1), reconstructs the file, verifies whole-file CRC, exports/displays the product.

Note: `usb=serial3` re-enumerates the ground Teensy USB ports. Update the hardware map in `docs/RF_MVP_DEMO_RUNBOOK.md` and the `run_gds_uart.sh` defaults in the same change.

### Throughput sanity check

~40,368 bytes / 44 useful bytes = ~918 data packets, no per-packet ACK. At a conservative ~15 packets/s effective rate: 60–90 s plus a retry round or two. Acceptable for a live demo with a visible progress display.

### Operational rule

While a transfer is active, quiesce non-essential telemetry (most periodic telemetry is already throttled for the RF MVP). Do not enforce a hard mute: channel tags make interleaving *safe*; quiescing only makes the transfer *fast*.

## The Seam Map: What Swaps and What Survives

This is the section that matters for the "foundations for future students" goal. Each seam below is a named boundary with a contract. When hardware changes, you replace what is left of the seam and keep what is right of it.

```text
[Mission logic]   [Payload plane]          [Link adaptation]        [Radio hardware]
MissionManager    PayloadDownlinkManager   UartChannelMux           comDriver (LinuxUartDriver)
ScienceManager    payload_receiver.py      channel framing          Teensy bridge firmware
StorageService                             RF segment format        RFM23BP / RadioHead
   |                  |                        |                        |
   +---- Seam D ------+------ Seam C ----------+-------- Seam B -------+--- Seam A
```

### Seam A — `Drv.ByteStreamDriver` (stock F Prime)

`ComCcsds.comStub <-> comDriver` is the framework's Communication Adapter Interface
(`lib/fprime/docs/reference/communication-adapter-interface.md`). Swapping the physical transport means swapping `comDriver` and nothing else:

- RFM23BP via Teensy bridge: `Drv.LinuxUartDriver` on `/dev/serial0` (current)
- 256-byte UART radio, Pi-direct: still `Drv.LinuxUartDriver`, different device/baud
- SatNOGS board: whatever driver its interface needs (UART/SPI/IP); if IP, `Drv.TcpClient` and the Teensy bridge disappears entirely

### Seam B — Link MTU and channel framing (`UartChannelMux` + Teensy link protocol)

All radio-packet-size knowledge lives here and **only** here:

- Teensy side: `link_protocol.hpp` constants (`RF_PACKET_MAX_LEN = 49`, `RF_SEGMENT_MAX_DATA = 44`, magics, ACK timing)
- F Prime side: one config header/FPP constants module (proposed `Components/LinkCfg/`) defining `LINK_MTU_BYTES`, `PAYLOAD_PACKET_DATA_BYTES`, channel IDs

Design rule: **no component above the mux may know the RF MTU.** `PayloadDownlinkManager` takes its packet data size from `LinkCfg`, not from a literal. When a 256-byte radio arrives, you change `LinkCfg` + Teensy constants (or delete the Teensy layer), and packet counts/telemetry adjust automatically.

### Seam C — Payload data plane (`PayloadDownlinkManager` protocol)

The transfer protocol (header/data/end/retry-bitmap) is radio-agnostic by construction: it assumes only "lossy datagrams of size `PAYLOAD_PACKET_DATA_BYTES`". A bigger radio means fewer, larger packets — same protocol, same ground receiver, one constant changed.

**Graduation criteria (the strategic end state):** the custom payload protocol is a tactical bridge, not the destination. When a future link provides (a) MTU comfortably above one CCSDS TM frame, and (b) sustained loss rate low enough that GDS shows no APID sequence gaps under load, retire channel 1 for files and switch the payload plane to stock `Svc.FileDownlink` (already wired in the topology via `FileHandling`). The commands (`START_PAYLOAD_DOWNLINK` etc.) and operator story stay; only the bulk transport behind them changes. Write that future change as: re-point `PayloadDownlinkManager` from its own packetizer to `FileDownlink.SendFile`.

### Seam D — Payload hardware (adapter/service pattern, already in place)

`PayloadAdapter_NeutronSim` -> `PayloadService` -> `ScienceManager` -> `StorageService` already follows the repo's adapter/service convention (same as EPS/GPS/ADCS). Swapping the local simulator for a future payload = new `PayloadAdapter_X` implementing the same service-facing ports. The downlink plane never sees the payload type; it sees a `ScienceProductDescriptor` with product ID, size, source kind, source path, and CRC. `PayloadAdapter_N1Legacy` is reference-only and is not built by default.

### Radio swap scenarios summarized

| Future radio | Seam A (driver) | Seam B (MTU/framing) | Seam C (payload plane) | Teensy bridge |
| --- | --- | --- | --- | --- |
| RFM23BP (today) | LinuxUartDriver `/dev/serial0` | 49 B packets, ch0/ch1 magics | custom protocol, 44 B/packet | required, both sides |
| 256-byte UART radio | LinuxUartDriver, new device | `LinkCfg` MTU -> ~250 B/packet | same protocol, ~5x fewer packets; candidate for graduation to FileDownlink | possibly removed (Pi-direct) |
| SatNOGS board | new driver (UART/SPI/TCP) | per its packet model; if reliable stream, mux may become pass-through | graduate to stock `Svc.FileDownlink` | removed |

## Phased Implementation (every phase gated by the smoke test)

The frozen demo path is protected: every phase ends by re-running
`ArtemisRpiTeensy_N2/tools/demo_rf_mvp_smoke.sh` and confirming PASS.

1. **Bench link channelization.** Extend the UART wrapper (+channel byte) and add the dual RF magic on both Teensy firmwares. Validate on the desk with both Teensys and a host script injecting ch0/ch1 frames — no Pi involved. Update both UART/transport contract docs in the same PR.
2. **F Prime mux + manager with simulated product.** Add `LinkCfg`, `UartChannelMux`, `PayloadDownlinkManager`. Generate a deterministic 40,368-byte test-pattern file on the Pi. Verify GDS pings clean while ch1 blasts.
3. **Ground receiver + retry.** `tools/payload_receiver.py`, retry-bitmap loop, whole-file CRC pass, file export. Test under deliberately induced loss (pull/attenuate antenna).
4. **Demo-story integration.** `SCHEDULE_COLLECTION` -> ScienceManager -> StorageService stages a real or simulated product file -> operator sends `START_PAYLOAD_DOWNLINK` in GDS -> receiver displays the product. This completes agents_notes Primary TODO #9 (with the bulk path on ch1 instead of FileDownlink, per the advice doc).
5. **Polish (optional).** GDS custom dashboard panel for the progress telemetry. Later, when graduation criteria are met on a better link, swap the bulk path to stock `Svc.FileDownlink`.

## Companion Effort: Skills Shared in the Repo

Agent skills should live in-repo at `.claude/skills/` (auto-discovered by Claude Code per checkout) instead of only in any one person's `~/.claude/skills/`:

- Move `fprime-swe`, `fprime-docs-search` (re-pointed local-docs-first per AGENTS.md), and `fprime-cross-compilation` (with Pi Zero W ARMv6 landmine) into `.claude/skills/`.
- Add project skills distilled from (and pointing to, not duplicating) the canonical docs:
  - `artemis-rf-link` — link layer model, hardware map, upload-by-`usb:` rule, debug counter glossary, checksum/APID triage tree
  - `artemis-demo-ops` — GDS launch (`space-packet-space-data-link`!), smoke test, Pi service ops, port landmines
- List the skills in AGENTS.md so non-Claude agents discover them too.
- After this plan is implemented, add `artemis-payload-downlink` documenting the channel protocol and receiver usage.

## Key References

- `docs/PAYLOAD_DOWNLINK_PROTOCOL_ADVICE.md` — option analysis this plan implements (Option 1)
- `docs/RF_MVP_DEMO_RUNBOOK.md` — frozen known-good path + smoke test
- `docs/agents_notes.md` — session history; 2026-04-24 entries explain why ch0 keeps per-segment ACK
- `docs/archive/rf_refactor.md` — RF reliability constraints (RadioHead 50-byte cap, segment math)
- `external/epscorc3m/` — proven payload/RF protocol precedent from the current
  Artemis baremetal reference
- `ArtemisRpiTeensy_N2/lib/fprime/docs/reference/communication-adapter-interface.md` — Seam A contract
- `ArtemisRpiTeensy_N2/lib/fprime/docs/user-manual/design-patterns/` — adapter/manager/subtopology patterns used here
