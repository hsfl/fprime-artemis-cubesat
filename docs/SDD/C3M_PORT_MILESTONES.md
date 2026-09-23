# EPSCoR C3M port: scope and milestone decisions

Decision record — 22 September 2026 (HST). Planning only; this file does not authorize implementation or claim that any milestone has passed. Source mission code is the main repository's `epscorc3m` branch. The target architecture is the Artemis SDD in [`source/Artemis_SDD.md`](source/Artemis_SDD.md).

## Selected sequence

1. **PDU sequencing first.** Test startup, command/status sequencing, Pi power enable and shutdown/recovery behavior on the intended bench hardware before the real-camera end-to-end milestone. Record actual rail, switch, heartbeat and reset observations. The SDD's schematic allocation is PDU-regulated `BUS_5V` feeding the OBC U2 Pi load switch, enabled by Teensy pin 36; `SW_5V_1` is not the Pi switch on the inspected OBC v4.24 schematic. As-built and loaded behavior remain unverified (SDD O2). This early sequencing test does not close the SDD's timed-operation protective-abort and host-loss requirements (O11).
2. **Simulated Lepton path.** Use a `SimDriver` and a fixed sample Lepton picture to exercise Teensy-authorized capture, Pi processing/product storage, downlink, and ground identity/integrity verification. Preserve the distinction between simulated evidence and real hardware evidence.
3. **First real-payload milestone.** From **fprime-gds**, command one real Lepton capture through the Teensy authority path. The Raspberry Pi Zero W acquires and processes it, then the product is received and verified on the ground. Confirm the matching job/product identity and integrity, plus continued Teensy commandability. Dennis reports that the current Lepton connects through the Pi Zero W micro-USB port; confirm the exact camera/adapter, enumeration and power behavior in the Payload ICD rather than assuming the connector proves the electrical envelope.
4. **Operational ground migration.** After command/control and payload downlink are confirmed in fprime-gds, integrate Yamcs and verify command outcomes, product receipt and integrity there. GDS is the development/milestone interface; Yamcs remains the SDD operational baseline.
5. **Later payload capabilities.** After the Yamcs path works, address the Lepton preview stream and Boson. Add a Boson `SimDriver`/sample product before the real Boson path if feasible. Real Boson is last. Lepton and Boson remain selectable under one logical payload, with only one camera active at a time and distinct product schemas.

## Porting constraints

- Reuse the current C3M capture, product and ground packet formats and behavior where they fit. Preserve working packet/repair behavior while adding the SDD's versioned Core–Payload peer sessions, authorized job IDs, deadlines, cancellation, stale-result rejection and status. Add ground confirmation of product identity/integrity before reclamation. Record the resulting wire contracts in I2 and I5; compatibility is a design/test goal, not a claim that the old protocol already meets the SDD.
- Retain useful C3M component behavior and camera backends, but move bus mode, PDU, radio and command authority to Teensy/Zephyr. Pi/Linux keeps camera I/O, processing and bulk product storage. The old Pi-led topology cannot be copied unchanged into the two-deployment architecture.
- Keep the first real-camera milestone to one capture and verified downlink. Preview is a separate best-effort path and is not part of that milestone.

## Items to resolve during detailed planning

- Define the exact PDU sequencing stimuli, safe output set, required measurements and pass/fail limits from the populated board and harness. Separately close protective abort/host-loss behavior before claiming SDD safety acceptance (I1, O2, O11, V04, V16).
- Define how the existing C3M packets are carried through Teensy-owned radio service and exposed to fprime-gds, then Yamcs, including ground receipt and repair (I5, V09, V13).
- Specify Lepton and Boson sample/product metadata, dimensions, pixel format, power/current limits and camera selection behavior in the mission Payload ICD (I3). A Boson simulator is presently a planned first step, not a verified driver.
- Measure command latency, radio capacity and Teensy memory under product transfer before choosing a cache size or changing packet layout (O1, O8, V07, V15).

No implementation, build, flash or hardware test was performed for this decision record.
