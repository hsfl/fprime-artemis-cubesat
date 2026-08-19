# C3M MVP Downlink Protocol

## Status And Scope

**Active MVP contract.** The executable wire format remains the existing `N2`
channel-1 protocol. This document does not introduce new wire magic or require a
dual decoder.

This is a proof-of-concept demo using RFM23BP radios. It demonstrates that the
C3M F Prime application can boot, expose basic spacecraft-bus telemetry,
capture a Lepton image, store it, downlink it, reconstruct it on the ground,
and repeat that cycle reliably while both sides remain powered. It is not a
flight-radio protocol or a complete Neutron 2 mission CONOPS.

## Demo Contract

One powered session supports any number of **sequential** capture/downlink
cycles:

```text
POWER ON SATELLITE AND GROUND
  -> READY / basic SOH visible
  -> CAPTURE one Lepton frame
  -> STORE a new uniquely identified product
  -> DOWNLINK that product
  -> VERIFY whole-product CRC on ground
  -> SAVE and DISPLAY the ground-reconstructed image
  -> READY
  -> repeat until the operator powers both systems off
```

The operator may capture another image only after the current capture has been
stored. Bulk downlinks are also serialized. Every cycle must use a fresh
product identity and a separate ground artifact. Packets, retry state, partial
files, and terminal status from one product must never mutate another product.

## Current N2 Wire Contract

The existing packet magic remains `N2` (`0x4E 0x32`). The existing packet types
remain:

| Type | Direction | Purpose |
| --- | --- | --- |
| `HEADER` | satellite to ground | transfer ID, product ID, byte count, packet count, data size, whole-product CRC-16 |
| `DATA` | satellite to ground | indexed payload bytes plus packet CRC-16 |
| `END` | satellite to ground | packet count and whole-product CRC-16 |
| `RETRY_REQUEST` | ground to satellite | bounded bitmap of missing packet indexes |

No change is proposed to N2 packet layouts, the 35-byte data field, the
44-byte RF segment payload, or the current transport constants.

## Simple Transfer Behavior

```text
SATELLITE                             GROUND

HEADER -> DATA burst(s) -> END  ---> receive by packet index
                  quiet          <--- bounded RETRY_REQUEST if needed
repair DATA burst(s) -> END      ---> fill missing positions
                  quiet          <--- another request only if needed
                                     CRC valid -> save/display COMPLETE
                                     bounded failure -> retain PARTIAL/ERROR
```

Rules:

1. Satellite sends bounded bursts and returns control regularly; it must not
   block a timing-critical F Prime rate group for a full transfer.
2. Ground sends a retry request only after receiving `END` or after a defined
   receive-quiet timeout.
3. Satellite merges duplicate or overlapping retry requests and never erases
   unfinished repair work.
4. Satellite repeats `END` after each repair burst/round so ground can evaluate
   the transfer again.
5. Retry-bitmap length, packet indexes, repair rounds, and total wait time are
   bounded and validated.
6. Routine channel-0 traffic stays quiet during payload bursts. Commands,
   command responses, warnings/errors, reset evidence, and transport faults
   remain available.
7. The ground receiver is the source of truth for percentage complete. F Prime
   retains low-rate status telemetry and the explicit `GET_PAYLOAD_STATUS`
   fallback; it does not broadcast automatic ten-percent progress events.

This is pragmatic half-duplex operation: **payload burst, quiet ground window,
retry if needed**. It does not require lease IDs, phase-control packet types, or
a distributed ownership protocol.

## Completion And Failure Meanings

- `LOCAL_TRANSMIT_DONE`: flight accepted the N2 `END` packet at its local UART
  boundary. This is useful flight status, but it is not proof of RF delivery.
- `COMPLETE`: the ground receiver has every indexed packet and its reconstructed
  bytes match the whole-product CRC from the N2 header/end marker.
- `PARTIAL`: the receiver retains the bytes and packet positions it received,
  plus the missing-packet map and a failure reason.
- `ERROR`: a bounded local or receiver failure prevented useful continuation.
- `ABORTED`: the operator explicitly abandoned the current attempt.

The operator GUI must never label `LOCAL_TRANSMIT_DONE` as ground-complete. A
failed transfer must stop cleanly enough that the operator can retry the same
stored product or capture/downlink a new one without restarting F Prime, GDS,
the payload receiver, or either Teensy.

N2 does not provide a final ground-to-flight CRC acknowledgement. For this MVP,
ground CRC is the authoritative demo result while flight reports local transmit
state. Adding a new final-ACK wire exchange is deferred unless HIL proves it is
needed for a concrete demo failure.

## Repeated-Cycle Isolation

For every accepted capture/downlink cycle:

- Storage creates or selects one immutable product with a new product ID/path.
- A duplicate request for the active product is idempotent; a different active
  descriptor returns busy rather than resetting the transfer.
- Receiver state is keyed by the N2 transfer/product metadata available on the
  wire and stored in a separate run directory.
- Successful completion clears only the active in-memory transfer state; it
  does not delete previous ground artifacts.
- An abandoned or expired partial remains inspectable and cannot consume retry
  packets meant for the next transfer.
- Stale or conflicting headers/control packets are rejected or quarantined.
- Returning to `READY` means a new cycle can begin without process or hardware
  restart.

Transfer-ID wrap and flight-process restart are not silently called safe. The
MVP acceptance run is bounded well below the U8 transfer-ID limit. If F Prime
restarts, the operator starts a fresh receiver session and records the old one
as partial rather than splicing packets across the restart.

## Ground Receiver Requirements

The normal ground path must:

- consume the emulated or physical channel-1 byte stream, not the source `.fdp`
  file directly;
- persist the header, received-packet bitmap, and data bytes;
- reopen a re-enumerated serial device by stable identity;
- reload a live transfer after receiver/web-app restart;
- issue bounded retry requests through the same path used during HIL;
- reconstruct, CRC-check, decode, save, and display the ground copy;
- keep complete and partial history for multiple transfers in one session.

Local emulation passes only when the receiver-created ground artifact is
verified. Decoding the satellite/source file is not end-to-end proof.

## Minimum Reliability Mechanisms

Keep the already implemented or planned MVP mechanisms:

- finite RF transmit-completion timeout below the Teensy watchdog;
- one bounded retry and explicit radio/FIFO recovery;
- actual USB zero/partial-write accounting with retained unwritten suffixes;
- independent channel-0 and channel-1 USB queues;
- bounded F Prime payload work per invocation;
- duplicate-start protection and additive selective repair;
- persistent ground packet map and honest partial artifacts;
- sparse channel-0 lifecycle/fault status;
- bounded transfer/repair timeout followed by a clean terminal state.

Do not add for the RFM23BP MVP:

- `C2` wire magic or an incompatible v2 decoder;
- boot epochs or durable flight transfer checkpoints;
- CRC-32 plus final-confirm/final-ACK/tombstone exchanges;
- request, confirmation, or lease identifiers;
- formal phase-control packets or ownership leases;
- spaceflight retention/expiry policy;
- automatic flight restart-resume across epochs.

The detailed July 14 mission-grade proposal remains recoverable in Git history
at commits `87eca99` and `72fcb24`. Those ideas are deferred design research,
not current implementation requirements.

## Local Acceptance

Without restarting the satellite emulator, F Prime deployment, receiver, or
ground emulator:

1. Boot and observe basic SOH.
2. Complete at least three sequential capture/downlink/display cycles.
3. Verify three unique stored products and three separate ground artifacts.
4. Compare each ground artifact's CRC/hash/content with its source only after
   reconstruction.
5. Drop a packet during a focused transfer; recover it through N2 retry.
6. Restart or disconnect the receiver during another transfer; resume or save
   an honest partial.
7. After any failed/abandoned transfer, complete a fresh capture/downlink cycle.
8. Confirm no stale retry affects the next transfer, no product is mixed, and
   no F Prime process restart or queue assertion occurs.

Local proof means the software paths and failure states behave deterministically.
It does not prove USB re-enumeration, RFM23BP timing, antenna behavior, watchdog
recovery, or physical goodput. Those remain HIL evidence.

## Change Rule

Prefer the smallest change that directly improves the repeated demo. Do not
change the N2 wire format, RF PHY, payload pacing, and state semantics in the
same patch. A new protocol is justified only by a reproduced requirement that
cannot be met by bounded N2 behavior.
