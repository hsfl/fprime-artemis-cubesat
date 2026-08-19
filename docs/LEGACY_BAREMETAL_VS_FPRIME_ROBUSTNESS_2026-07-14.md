# Legacy Baremetal vs F Prime: Where Robustness Actually Lives — 2026-07-14

## Who this is for

Future team members asking some version of:

> "The old EPSCoR C3M baremetal demo worked reliably to 1 mile.
> The F Prime refactor struggled at 40 feet outdoors.
> Was the refactor a mistake? Is baremetal just more robust?"

Short answer: the refactor was not a mistake, and baremetal is not
categorically more robust. Read on.

## BLUF

- Today, the legacy baremetal (`external/epscorc3m`) has higher **demonstrated
  link robustness**. The F Prime stack has higher **robustness ceiling** and,
  more importantly, robustness you can **prove, inspect, and keep**.
- The outdoor reliability gap is **not** a property of "baremetal vs F Prime."
  It exists because specific operational contracts encoded in the legacy
  monolith did not survive the port. Those contracts are portable; the
  hardening plan
  ([C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md](C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md))
  ports them.
- The hardened F Prime architecture should close the observed reliability gap
  while retaining its advantages in modularity, observability, and testing.
  The legacy path can still remain simpler and more packet-efficient.

## The concrete proof that the gap is contracts, not framework

The clearest example, verified in source on 2026-07-14:

- The legacy code explicitly bounds every **post-send completion** wait:
  `waitPacketSent(RADIO_WAIT_PACKET_SENT_MS)`, `waitPacketSent(100)`,
  `waitPacketSent(50)` throughout
  `external/epscorc3m/SatellitePayload/satellite_teensy/satellite_teensy.ino`
  and the legacy ground station.
- The current Teensy bridge calls `sendPacket()` with the TX-completion
  timeout defaulting to `0`, which is an **unbounded**
  `radio.waitPacketSent()` — see
  `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/rf23_driver.cpp`
  and `GDS_Teensy/firmware/gds_teensy/src/rf23_driver.cpp` calling into
  `artemis_rf23bp.hpp::sendPacket()`. A missing TX-done interrupt therefore
  rides straight into the 12-second hardware watchdog reset.

This comparison is not proof that the legacy radio path handles every stuck-TX
case. RadioHead's `send()` begins with its own unbounded wait for any previous
transmit, and the legacy timeout path does not fully recover a permanently
stuck RadioHead TX state. The legacy code nevertheless establishes a useful
500 ms post-send deadline that the refactor lost.

Note what this example is **not**: the current Teensy bridge is not F Prime
code. It is baremetal too. The robustness did not leak out because we adopted
a framework. It leaked out because hard-won field lessons were encoded inside
two large, tightly coupled flight and ground sketches, where a rewrite could
not see them as explicit requirements.

Other legacy operational contracts that did not fully survive the port
(details and code evidence in the hardening plan):

- retain the source image and ground packet bitmap long enough to service
  selective repair, rather than discarding all state after local transmit
- phased half-duplex use of the shared channel
- recovery driven by persistent received-packet state, with each missing map
  rebuilt from that state after a phased turnaround
- `45` science bytes per RF packet (about `856` packets per product) versus
  the current `35` (about `1,100` packets), i.e. lower airtime exposure

This is the core hazard of tribal-knowledge robustness: it is invisible until
you lose it.

## Why "proven to 1 mile" is a weaker claim than it sounds

The 1-mile result is proof of *that binary, that geometry, that day* — not of
the design.

The legacy system's robustness is real but **brittle**:

- a multi-thousand-line loop with no component boundaries
- no automated regression harness, so source changes still require extensive
  physical requalification
- useful RF telemetry such as RSSI, CRC failures, and RadioHead counters, but
  no clean separation of RF health from USB, process, and reset health
- behavior tuned by field iteration and stored nowhere except the code itself

That is exactly why it was not portable across missions despite reusing the
same Artemis CubeSat bus: modifying it for Neutron 2 (Lepton vs Neutron 2 HAL
sim, different payload, different ground stack) risked silently un-tuning the
exact behaviors that made it work, with no way to detect the regression
before the next field test.

## What the F Prime refactor actually bought

- **A path to verifiable robustness.** Component and state-machine test seams
  that the hardening plan can exercise. The current `tools/validate_local.sh`
  proves nominal flow; WP6 will add the deterministic failure matrix needed
  for recovery regression protection.
- **A path to observable failure.** F Prime events and telemetry provide the
  mechanism, while WP5 must still add separate RF, USB, process, and reset
  indicators.
- **CCSDS framing and a real ground path.** `fprime-gds` now, Yamcs later,
  with standard space-packet semantics above project-specific RF/UART framing,
  dictionaries, and endpoints.
- **Portability.** The App-Man-Drv split (see
  [SYSTEM_ARCHITECTURE.md](SYSTEM_ARCHITECTURE.md)) is why swapping Lepton
  for a Neutron 2 HAL simulation is primarily a driver/topology and ground
  decoder change instead of a fork of the mission application logic.

## What F Prime did NOT buy (the honest theoretical answer)

F Prime does not provide automatic robustness across boundaries it does not
own. Its component model does not automatically guarantee behavior across the
system's external UART, RF, USB, and process boundaries:

```text
F Prime -> Pi UART -> satellite Teensy -> RF
        -> ground Teensy -> USB -> ground application
```

Every one of those boundaries needs an **explicit contract** — bounded waits,
honest partial-write handling, persistent ground state, ground-verified
completion. The legacy code had crude-but-real versions of several of these
because it was written *by* the field failures. The refactor has to encode
them deliberately.

## The framing to carry forward

- Robustness lives in **operational contracts**, not in the choice of
  framework or the absence of one.
- The two large legacy flight and ground sketches stored many of those
  contracts as tightly coupled implementation knowledge.
  The F Prime architecture can store them as tested, observable, documented
  components — but only after someone extracts and reinstalls them.
- The hardening plan is precisely that extraction: the missing second half of
  the refactor. WP1–WP3 address bounded RF TX, honest USB writes, persistent
  ground state, verified completion, and repair semantics. WP4–WP8 are still
  required for half-duplex behavior, observability, failure testing, HIL, and
  the unresolved outdoor RF-geometry question.

When evaluating any future rewrite or port in this project, ask first:
**"Which operational contracts does the old system encode implicitly, and
where does the new system write them down?"** If the answer is "nowhere,"
the port is not done.

## Related documents

- [C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md](C3M_RF_RELIABILITY_HARDENING_PLAN_2026-07-14.md)
  — the work plan that ports the legacy contracts into the F Prime system
- [SYSTEM_ARCHITECTURE.md](SYSTEM_ARCHITECTURE.md) — Neutron 2 target
  architecture and prototype crosswalk
- `external/epscorc3m/` — the legacy baremetal reference; use it to mine
  operational contracts, not as a target architecture
