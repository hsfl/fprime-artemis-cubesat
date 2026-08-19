# C3M Ground RFM23BP TX-Wedge Root Cause Analysis

**Date:** 2026-07-16

**System:** EPSCoR C3M HIL, F Prime GDS, ground Teensy 4.1, RFM23BP 433 MHz radio

**Status:** Proximate root cause confirmed; exact electrical mechanism requires scoped bench testing

## BLUF

The intermittent GDS-side link death is a **local ground RFM23BP transmit/control-path wedge**. The ground RadioHead driver remains in transmit mode, the RFM23BP does not assert transmit-complete, and its SPI register interface returns invalid/repetitive values. A real SDN shutdown and reinitialization restores the radio without resetting the Teensy USB device or restarting GDS.

This is **not primarily an RF-range, missing-ACK, GDS-process, USB, or satellite failure**. The same ground TX fault reproduced while the satellite was powered completely off.

The most likely underlying physical mechanism is disturbance of the ground radio's power, ground, or SPI interface during a +30 dBm PA transmit event. Candidate causes include supply droop/ringing, insufficient local decoupling, a 5 V jumper or R8 backfeed condition, ground bounce, or RF coupling into the digital control lines. This electrical mechanism is not yet proven and must be separated with scope/logic-analyzer testing.

## Operator-Observed Symptom

- Downlink telemetry could remain strong for an extended period.
- After several ground-originated commands or payload-channel requests, the ground link appeared to die.
- Restarting or power-cycling the ground side previously restored operation.
- With the hardened firmware, the link now dies briefly and recovers autonomously through SDN reinitialization.
- The failure appeared correlated with the number of messages transmitted, not simply elapsed time.

## Test Conditions

### Bench control

- Ground and satellite monopoles were approximately 20 inches apart.
- Twelve unique F Prime PING commands completed exactly once with consecutive Pong counts.
- The ground radio remained `READY + NONE`.
- No terminal TX failures occurred during this initial smoke test.
- GDS, the payload receiver, and the Pi F Prime process remained alive.

### Long-range test

- The operator carried the ground station outside the room and used the Yagi antenna.
- Downlink and commanding worked at range, but the ground link intermittently died and then recovered.
- The hardened ground firmware accumulated repeated local TX failures and SDN recoveries.

### Ground-only isolation test

- The satellite and its Wi-Fi/Pi were powered off.
- The ground Teensy, RFM23BP, GDS, and payload UI remained powered.
- Local ground TX-completion failures continued to reproduce.
- A final F Prime PING token `47999` was injected while the satellite was off. It could not execute remotely, but it reproduced the local ground TX wedge and produced the captured register snapshot.

This ground-only reproduction excludes satellite receive behavior, remote ACK generation, and path loss as prerequisites for the local TX-completion failure.

## Captured Evidence

### Counters after long-range testing

```text
rf_tx_pkt=47
rf_tx_msg=47
payload_rf_tx_msg=42
rf_tx_drops=21
rf_tx_timeouts=42
rf_recoveries=42
rf_tx_terminal_failures=21
rf_ack_rx=5
rf_retries=3
rf_ack_timeouts=3
rf_state=READY
rf_fault=NONE
rf_init_attempts=22
rf_init_failures=0
rf_sdn_recoveries=21
usb0_backpressure=0
usb0_recoveries=0
usb0_discards=0
usb1_backpressure=0
usb1_recoveries=0
usb1_discards=0
```

Interpretation:

- Every terminal failure contained two bounded TX-completion timeouts: the initial attempt and one immediate retry.
- Every terminal failure triggered one successful SDN recovery.
- All radio reinitializations succeeded.
- Ordinary RF ACK loss was rare relative to local TX-completion failure.
- USB transport counters remained clean.
- With 47 successful messages and 21 terminally failed messages, the observed message-level terminal failure rate was approximately 31% during this sample.

### Ground-only progression

While the satellite remained off, passive ground-side payload traffic raised the counters from:

```text
terminal failures: 21 -> 24
TX timeouts:       42 -> 48
SDN recoveries:    21 -> 24
```

The final isolated PING raised them to:

```text
rf_tx_drops=25
rf_tx_timeouts=50
rf_recoveries=50
rf_tx_terminal_failures=25
rf_init_attempts=26
rf_init_failures=0
rf_sdn_recoveries=25
```

### First-fault register snapshot

```text
[GDS_Teensy] RF_FAULT
cause=2
captured_ms=821187
nirq=1
rh_mode=3
reg00=00
reg01=08
reg02=08
reg05=08
reg06=08
reg07=08
reg08=08
reg26=08
irq03=08
irq04=08
```

Important decoding:

- `cause=2` is `TX_TIMEOUT`.
- `rh_mode=3` is RadioHead `RHModeTx`; the software still believed the radio was transmitting after the 500 ms completion deadline.
- `nirq=1` means the active-low RFM23BP interrupt pin was not asserted.
- The packet-sent flag is bit `0x04`; the captured interrupt value did not contain it.
- RFM23BP device register `0x00` should identify the transceiver as `0x08`, but the snapshot returned `0x00`.
- Nearly every following register returned the same `0x08` value despite having different expected contents.

The direct GPIO and RadioHead mode evidence prove that TX completion was not observed. The invalid/repetitive register values strongly indicate that the RFM23BP SPI/control interface was no longer returning trustworthy data at the time of failure.

## Root Cause Assessment

### Confirmed proximate root cause

The ground RFM23BP enters a local transmit/control-path fault:

1. Ground software starts a valid RF transmission.
2. RadioHead enters `RHModeTx`.
3. The RFM23BP never presents a usable packet-sent completion interrupt.
4. The SPI/register interface is invalid or corrupted when sampled.
5. The bounded 500 ms wait expires.
6. One FIFO/mode recovery retry also expires.
7. Ground firmware asserts real RFM23BP SDN, discards uncertain queued uplinks, and performs a fresh POR/init.
8. Initialization succeeds and the radio returns to `READY` without resetting Teensy USB or GDS.

Relevant implementation:

- [`GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp`](../GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp)
- [`GDS_Teensy/firmware/gds_teensy/src/rf23_driver.cpp`](../GDS_Teensy/firmware/gds_teensy/src/rf23_driver.cpp)
- [`GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.cpp`](../GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.cpp)
- [`GDS_Teensy/firmware/gds_teensy/gds_teensy.ino`](../GDS_Teensy/firmware/gds_teensy/gds_teensy.ino)

### Most likely underlying mechanism

The leading hypothesis is **ground-radio electrical integrity during +30 dBm transmit**:

- PA current transient causes RFM23BP VCC droop or ringing.
- Ground bounce or poor return integrity disturbs CS, SCK, MISO, MOSI, nIRQ, or SDN.
- RF energy couples into short digital/control nets or an inadequately grounded radio assembly.
- The direct USB 5 V radio jumper or populated R8 creates an unintended backfeed/rail interaction.
- The specific ground RFM23BP module may be marginal or damaged.

Distance itself is not the cause. More transmitted messages simply create more high-power PA activations and therefore more opportunities to trigger the local fault.

### Lower-probability alternatives

- **MCU interrupt-edge loss only:** less likely because nIRQ was high and the register interface was invalid, rather than showing a clean latched packet-sent condition.
- **Timeout too short:** excluded. The configured completion timeout is 500 ms, far longer than the airtime of a maximum 49-byte RF packet at 125 kbps.
- **Peer ACK loss:** excluded as the local TX completion fault reproduced with the peer powered off; packet-sent completion is local to the transmitter.
- **GDS or USB failure:** excluded for the captured failures because GDS remained running and all USB fault/recovery counters stayed zero.
- **Radio reinitialization weakness:** excluded for this run because every SDN/POR reinitialization succeeded with zero init failures.

## Why the New Recovery Works

The recovery firmware does not reset the Teensy or GDS. It:

1. Bounds the RadioHead TX-completion wait.
2. Captures first-fault state before changing the radio.
3. Tries one bounded FIFO/mode recovery.
4. Treats a second timeout as an uncertain local TX failure.
5. Purges remaining uncertain uplink work without destroying already completed downlink queues.
6. Asserts RFM23BP SDN.
7. Retries radio initialization at bounded intervals until `READY`.

This is the correct containment behavior even though the underlying electrical fault remains.

## Required Next Bench Tests

Run these in order and record pass/fail counts for at least 50 one-at-a-time transmissions per condition.

1. **28 dBm versus 30 dBm A/B test**
   - Hold packet size, interval, antenna, geometry, and supply constant.
   - If the failure rate collapses at 28 dBm, PA current/EMI becomes the primary confirmed mechanism.

2. **Scope the radio supply at the module**
   - Measure RFM23BP VCC directly at its VCC/GND pins with a short ground spring.
   - Trigger on TX enable and capture droop, ringing, and recovery.
   - Also observe the 5 V jumper source and any switched 3.3 V rail connected through R8.

3. **Capture the control bus**
   - Monitor CS, SCK, MOSI, MISO, nIRQ, and SDN through a failure.
   - Determine whether the RFM23BP stops responding, CS/clock integrity collapses, or nIRQ never transitions.

4. **Verify board power topology**
   - Confirm the physical RF-plane revision and R8 population/continuity.
   - Prove the direct 5 V jumper does not backfeed `SW_3V3_2`.
   - Inspect common-ground integrity and module-local bulk/ceramic decoupling.

5. **Swap one variable at a time**
   - Known-good USB cable/source.
   - Powered hub versus direct Mac USB.
   - Known-good ground RFM23BP module.
   - Same firmware and exact transmit script for every comparison.

## Recommended Software Follow-Up

- Keep the current SDN recovery and bounded waits.
- Preserve the 500 ms timeout; increasing it would only delay recovery.
- Add a healthy pre-TX identity sample and an immediate post-fault identity retry so future logs distinguish transient SPI corruption from a radio core lockup.
- Persist the most recent fault snapshot until explicitly cleared, rather than printing it only once to a live debug stream.
- Add per-channel terminal TX failure counters so CCSDS and payload-channel triggers can be compared directly.
- Add a repeatable ground-only TX soak command that does not require the satellite or Pi.

## Acceptance Criteria for Closing the Incident

The incident should not be closed solely because automatic SDN recovery works. Closure requires:

- At least 100 consecutive ground transmissions at the selected production power with zero terminal TX failures.
- Clean VCC and SPI/nIRQ captures during the full transmit soak.
- Verified R8/backfeed and radio power topology on the actual ground board.
- A repeat long-range Yagi test with no GDS/USB restart and no terminal TX failures.
- If a fault is intentionally induced, exactly one bounded containment/recovery sequence and successful post-recovery commanding without stale command replay.

## Team Conclusion

The hardening work converted an operator-reset failure into an autonomous, observable recovery. The remaining engineering problem is below GDS and above the RF propagation path: the ground RFM23BP loses valid TX-completion/control behavior during some +30 dBm transmissions. The next work should focus on the ground radio's power and SPI integrity, not on GDS restart logic or longer RF timeouts.
