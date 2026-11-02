# Neutron 2 accepted software baseline

Prepared October 14, 2026 on `neutron2-develop`. This checkpoint packages the October 7–9 host software work after review. Its starting HEAD was `33aef0a`; the frozen `neutron_2` / `v1.0.0-mvp-demo` baseline remains at `abc8bcd`. Publication to the remote is a separate step.

## Milestone scope revision — October 18; local TCP acceptance October 20

The [October 27 PDU deployment record](evidence/n2-pdu-deployment-2026-10-27/README.md)
adds a successful ARMv6 Pi Zero W / Pi 5 Yamcs bench test with real PDU protocol
2, firmware 2.2.2, nine named rail readbacks, and 3V3_1 ON/GET/OFF/GET; all
nine readbacks were OFF at the end. This does not establish measured voltage,
payload readiness, the CDR collection sequence, RF or flight qualification.

The acceptance record below remains dated software evidence. Dennis revised CDR to real PLD + OBC + PDU payload power on/off, excluding COMMS; the selected CDR command/telemetry path is Pi Zero W F′ stock ComCcsds/TCP and FileDownlink over LAN to Pi 5/Yamcs, with no network mux and independent UART channel-2 PDU RPC. The [October 20 native loopback acceptance](evidence/n2-cdr-tcp-2026-10-20/README.md) passed; Pi and hardware acceptance remain separate. Follow the [canonical CDR plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) for current implementation status. FSR adds the full SatNOGS/SDR/Yamcs chain around payload + PDU and the previously planned modes/scheduling. Follow [current intent](../planning/DEVELOPMENT_INTENT.md) for milestone allocation; older full-RF CDR references below describe the prior plan.

## Included behavior

- Opt-in `NEUTRON2_LOCAL_YAMCS=ON` native configuration, matching dictionary conversion, loopback Yamcs stack, and reproducible probes.
- Synthetic literal pass-through; serialized payload admission; correlated acceptance, failure, timeout, stale/duplicate callback rejection, and recovery.
- Raw simulator capture, storage, channel-1 reconstruction, independent Yamcs readback and integrity checks, plus synthetic pulse regression views.
- Token-correlated VERIFIED commands with payload, storage, sender, ground receipt, and final completion stages. Sender completion alone cannot produce delivery success.
- Immutable raw products and metadata, restart recovery, older-product selection, persistent identity allocation, and a 16-product catalog that rejects new captures when full.
- Operator walkthroughs, SDD reference/gap plan, and a [portable acceptance snapshot](evidence/n2-software-2026-10-09/README.md).

The default local-Yamcs option is OFF. The software test UART sizing differs from the RF configuration; never deploy the host artifact as a hardware build.

## Team setup and review

Use the [local Yamcs runbook](../development/YAMCS_LOCAL_TESTING.md) for pinned submodules, Python environments, dependencies, native build and launch. macOS arm64 was exercised; Windows development uses WSL and has not been validated for this local stack. Bootstrap installs dependencies and is an explicit setup step.

From the repository root after setup:

```sh
./tools/yamcs/build_native.sh
./tools/yamcs/run_pulses.sh
# In another terminal:
./tools/yamcs/pulse_cycle.sh
./tools/yamcs/stop.sh
```

The [completion/storage procedure](COMPLETION_STORAGE.md) covers VERIFIED command tokens, isolated storage roots, restart tests, and expected failure behavior. Keep tokens unique across sessions. Stored products remain after delivery; ACK-based reclamation is deferred. Use a fresh isolated `N2_STORAGE_ROOT` for testing instead of deleting an existing catalog.

## Evidence and remaining gates

October 9 acceptance passed all six completion/storage scenarios and A/C/B/pulse regressions. Before the packaging correction below, all 20 recorded implementation files matched the original source fingerprint. These historical runs used the recorded executable/dictionary and source HEAD plus dirty state. Current packaging validation is recorded below separately from the historical acceptance.

FSW owns acquiring, retaining, and downlinking opaque raw files. The payload team's JAR owns actual science interpretation. Remaining CDR integration gates include real payload command/file transport, measured payload power and readiness, retained raw-product inspection and the complete operator sequence. The October 27 Pi runtime and one-rail PDU checks are bounded by the record above. FSR adds SatNOGS MCU/SPI, SDR frame handoff and full RF/ground receipt, with the previously planned nominal modes, scheduling and integrated PDU power behavior. See [development intent](../planning/DEVELOPMENT_INTENT.md).

The two pre-existing Teensy Arduino build-script repairs, internal Claude review, preliminary screenshots, local environments, generated builds, and complete runtime/data trees were excluded from the October 14 checkpoint. The October 19 documentation checkpoint separately retains the historical review and preliminary screenshots; it does not promote them to new acceptance evidence. The SDD PDF and extracted sidecar are reference inputs, not claims of implemented flight readiness.

## Packaging correction — October 14

Review found that a direct downlink request rejected while another transfer was active emitted the next transfer ID without consuming it. CommsApp recorded that rejection ID as its watermark and could ignore the next accepted transfer as stale, leaving subsequent requests busy. The sender now allocates every reported request ID from a separate sequence while retaining the in-flight transfer identity. Targeted sender and CommsApp regressions cover rejection, active completion, subsequent delivery, and continued availability.

This correction changes the sender source and tests after the October 9 checkpoint. The archived source/build fingerprints remain historical evidence; current validation below covers the packaged correction.

## Packaging validation

October 14 checks passed: native OFF build, final local-Yamcs ON arm64 build, six component suites / 34 GTest cases, 21 Python tests plus five subtests, Python/shell syntax, and independent fix review. Fresh Yamcs pass-through, all 31 lifecycle checks, synthetic pulse/raw delivery, and completion/storage before and after FSW restart passed on the corrected implementation. The six original October 9 fault scenarios remain historical acceptance; only before/after-restart completion was replayed during packaging.

See [current validation and source/build hashes](evidence/n2-packaging-2026-10-14/README.md). The current dictionary matches the October 9 dictionary; the executable reflects the sender correction. The current source fingerprint covers 72 implementation/test/tool/configuration files. No target deployment, hardware/RF operation, or push is part of this checkpoint.
