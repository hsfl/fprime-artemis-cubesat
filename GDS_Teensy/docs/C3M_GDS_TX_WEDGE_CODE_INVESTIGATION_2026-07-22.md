# C3M GDS TX Wedge Code Investigation

Date: 2026-07-22
Node: 8th-floor EPSCoR C3M ground GDS Teensy and RFM23BP
Branch: `codex/gds-teensy-tx-load-test`
Test image: ground-only `GDS_TX_LOAD_TEST` image

## BLUF

The code explains why each fault appears as exactly three failed packets. It does not show three independent radio failures.

One real event occurs first: RadioHead starts a transmission, but its `PACKET_SENT` interrupt path does not change the software mode from `RHModeTx` to `RHModeIdle` within 500 ms. The next two load-test attempts see that the mode is still `RHModeTx`. They reject the send before a new packet starts. The third timeout result causes an SDN reset and full radio initialization.

Therefore:

- One underlying TX wedge produces three counted failures.
- The three-failure recovery policy adds two guaranteed failed attempts after the first wedge.
- SDN recovery works. Every observed recovery returned the radio to `READY` and later packets transmitted.
- The first wedge is a loss of synchronization between RadioHead and the radio TX-complete state.
- The captured register values are not credible radio state. Many unrelated registers read as `0x08`. This means that SPI or radio control is not reliable when the fault is captured.
- No deterministic code path was found that causes the first wedge. The same image can send 1,000 packets clean and can later wedge while the board remains stationary. This makes a pure repeatable algorithm error unlikely.
- The code can be improved: recover on the first real 500 ms timeout. Waiting for three results cannot prove that the radio recovered because attempts two and three do not start a transmission.

The most accurate root-cause statement is:

> The immediate software failure is a missing TX-complete state transition in RadioHead. The three-failure pattern is caused by our recovery policy. The available data does not prove whether the first missing transition comes from a missed interrupt, invalid SPI/control state, or a radio-side power, RF, or thermal event.

## Test evidence from today

All tests used the same ground-only load-test path. The load test sends directly through `Rf23Driver::send()`. It does not wait for a satellite, an ACK, F Prime, or the normal relay path.

| Test | Power | Result | Real first timeouts | SDN recoveries |
|---|---:|---:|---:|---:|
| Initial smoke test | 30 dBm | 125 successes, 0 failures | 0 | 0 |
| First long test | 30 dBm | 997 successes, 3 failures | 1 | 1 |
| Long repeat | 30 dBm | 997 successes, 3 failures | 1 | 1 |
| Power comparison | 28 dBm | 1,000 successes, 0 failures | 0 | 0 |
| Power comparison repeat | 28 dBm | 985 successes, 15 failures | 5 | 5 |
| Stationary test | 30 dBm | 985 successes, 15 failures | 5 | 5 |
| Stationary repeat | 30 dBm | 985 successes, 15 failures | 5 | 5 |

`failures / 3` gives the number of underlying 500 ms TX-completion timeouts in these logs. The other two failures in each group are pre-send rejections.

The 28 dBm results are important. One test was clean and the next test had five wedges. A two dB power change did not control the fault. Movement also did not control it because the stationary tests still failed.

## Exact failure sequence

The repeated log sequence is:

```text
RF23BP TX completion timeout
RF23BP pre-send TX wedge
RF23BP pre-send TX wedge
RF23BP ready
```

The code produces this sequence as follows:

1. `RH_RF22::send()` loads the FIFO and starts TX.
2. `waitPacketSent(500)` waits for `_mode` to change from `RHModeTx`.
3. RadioHead changes `_mode` only when `handleInterrupt()` reads the `IPKSENT` flag.
4. The first wait expires. The send returns `TX_TIMEOUT`.
5. The code does not clear TX mode after this first timeout.
6. The next attempt sees `radio.mode() == RHModeTx`. It returns `TX_TIMEOUT` without calling `radio.send()`.
7. The following attempt does the same thing.
8. The third timeout result calls `failSafeOffLocalTx()`.
9. `serviceRecovery()` pulses SDN, initializes SPI1 and RadioHead, and returns the driver to `READY`.

The relevant source is:

- [`artemis_rf23bp.hpp`](../firmware/gds_teensy/src/artemis_rf23bp.hpp): the pre-send TX-mode check and bounded 500 ms wait.
- [`rf23_driver.cpp`](../firmware/gds_teensy/src/rf23_driver.cpp): `TX_TIMEOUTS_BEFORE_RECOVERY = 3` and the SDN recovery request.
- [`gds_tx_load_test.hpp`](../firmware/gds_teensy/src/gds_tx_load_test.hpp): every non-success result is counted as one failed attempt.
- [`link_protocol.hpp`](../firmware/gds_teensy/src/link_protocol.hpp): the 500 ms completion limit.

## What the three failures mean

The current counters combine two different events:

| Counter event | What happened |
|---|---|
| First `TX_TIMEOUT` | A packet was started. RadioHead did not observe completion within 500 ms. |
| Second `TX_TIMEOUT` | No packet was started. The pre-send guard found the old TX mode. |
| Third `TX_TIMEOUT` | No packet was started. The same guard fired, then SDN recovery started. |

This is a code-observability problem. `rf_tx_timeouts=15` sounds like 15 packets independently timed out. The logs show five real TX-completion timeouts and ten derived pre-send failures.

## The RadioHead dependency

The active RadioHead library is the Teensy 1.62.0 library under the local Arduino installation.

Its operation is simple:

- `RH_RF22::send()` calls `startTransmit()`.
- `startTransmit()` calls `setModeTx()`.
- `RHGenericDriver::waitPacketSent(timeout)` only checks whether `_mode` is still `RHModeTx`.
- `RH_RF22::handleInterrupt()` reads interrupt status registers `0x03` and `0x04`.
- Only an `IPKSENT` bit causes `_mode = RHModeIdle`.

The 500 ms wait is not too short for this test. The test packet is 49 bytes and the selected modem rate is 125 kbps. Normal completion takes far less than 500 ms. Hundreds of successful sends with the same timeout also disprove a normal-duration problem.

## Fault snapshot interpretation

Every shown wedge captured this pattern:

```text
nirq=1 rh_mode=3
reg00=00 reg01=08 reg02=08 reg05=08 reg06=08
reg07=08 reg08=08 reg26=08 irq03=08 irq04=08
```

`rh_mode=3` is `RHModeTx`. This confirms that RadioHead did not leave TX mode.

`nirq=1` means that the interrupt line was not asserted when the snapshot ran. This does not prove that an interrupt never occurred. It can also mean that the edge was missed or that a status read cleared it earlier.

The register values must not be treated as valid device state. Device type register `0x00` is `0x00`, and many unrelated registers return the same `0x08` value. A correctly operating RFM23BP does not have this register map. The snapshot therefore shows that SPI/control data is invalid at the time of the fault.

The snapshot reads status registers `0x03` and `0x04` last because these reads clear interrupt flags. This diagnostic cannot cause the initial 500 ms timeout because it runs only after the timeout. It can change the post-fault state, so it is not suitable for proving whether `IPKSENT` was present before the snapshot.

## Baseline compared with the current test branch

The previous hardened implementation at commit `b09195e` recovered immediately after a TX timeout. It forced idle, cleared both FIFOs, cleared interrupt status, and returned to RX.

The current working tree intentionally changed this behavior:

- It does not call `recoverTransmitPath()` after the first timeout.
- It keeps the radio in the wedged TX state.
- It waits for three `TX_TIMEOUT` results.
- It then uses the stronger SDN reset and full initialization.

This change does not create the first TX-completion timeout. It does create the visible groups of three failures.

Other current changes do not fit the observed first fault:

- The watchdog is absent from the test image. It cannot cause these current failures.
- The normal relay poll is skipped during the RF load test. ACK and relay code cannot cause the first timeout.
- The satellite is not required. A remote packet or missing ACK cannot cause the local completion wait to fail.
- The SDN delays are 100 ms high and 100 ms low on the GDS copy. Recovery always succeeded in the shown logs.
- The radio initialization reattaches the same interrupt handler and later transmissions work. This does not look like an accumulated handler allocation failure.

## Code issues found

### 1. Confirmed: the recovery threshold creates two unnecessary failures

After the first timeout, the radio remains in `RHModeTx`. The next two send calls cannot make progress. A threshold of three send results is not useful in this state.

Recommended behavior: do the SDN reset after the first bounded completion timeout. If the project still wants a threshold, count three independent timeouts only after each attempt has returned to a valid ready state.

### 2. Confirmed: counters hide the difference between a real timeout and a pre-send rejection

Both cases increment `rfTxTimeouts`, `rfTxDrops`, and `rfTxTerminalFailures`. Add separate counters such as:

- `rfTxCompletionTimeouts`
- `rfTxPreSendWedged`
- `rfSdnRecoveries`

This makes one hardware-facing event visible as one event.

### 3. Confirmed: the three shared helper copies are not identical

The focused recovery test currently fails because the GDS helper uses 100 ms SDN delays while the shared and satellite copies use 50 ms. The GDS image builds, and this mismatch does not explain a runtime wedge after hundreds of packets. It is still a source-control defect because the test expects these helpers to remain identical.

### 4. Possible: a TX-complete interrupt can be lost or cannot be read

This is the direct boundary of the code evidence. RadioHead depends on one falling-edge interrupt and a valid SPI read of the interrupt flags. If either fails, `_mode` remains TX forever. The bounded wait detects this condition correctly.

The current diagnostics cannot distinguish these cases:

- The radio did not generate `IPKSENT`.
- NIRQ asserted, but the Teensy missed the falling edge.
- The ISR ran, but its SPI read was invalid.
- A prior or concurrent status read cleared the flag.
- The radio or SPI interface entered an invalid state during TX.

### 5. Not supported: the 500 ms timeout causes false failures

The timeout is much longer than a normal 49-byte transmission at 125 kbps. Clean 1,000-packet tests use the same value. Increasing it can delay recovery but is unlikely to prevent this fault.

## Best next code-only diagnostic

Do not add more serial prints inside the ISR. RadioHead warns that printing there can cause crashes.

For one diagnostic image, add volatile in-memory ISR telemetry and print it only from the main loop:

- ISR entry count
- last interrupt status bytes
- `IPKSENT` count
- `_txGood` before and after each send
- NIRQ level at the instant the 500 ms wait expires
- separate completion-timeout and pre-send-wedge counters

This can split the next fault into useful cases:

| Observation | Meaning |
|---|---|
| ISR count increases and `IPKSENT` is set | Software mode update or memory-state defect. |
| ISR count increases but status is invalid | SPI read failed inside the ISR. |
| NIRQ goes low but ISR count does not increase | Interrupt edge or attachment problem. |
| NIRQ stays high and no `IPKSENT` exists | Radio did not report packet completion or lost its state. |

The production-safe recovery should still pulse SDN on the first 500 ms timeout. Diagnostics must not delay that recovery.

## Verification performed

The current GDS load-test image builds successfully with Teensy core 1.62.0.

```text
FLASH: code 66352, data 14300, headers 8432
RAM1: variables 39712, code 63784, padding 1752
```

Focused unit tests produced five passes and one failure. The only failure is the expected helper-copy mismatch: the GDS SDN delays are 100 ms, while the other two copies are 50 ms.

## Follow-along commands

From the repository root:

```bash
# See the three-result recovery policy.
nl -ba GDS_Teensy/firmware/gds_teensy/src/rf23_driver.cpp | sed -n '216,252p'

# See the real completion timeout and the derived pre-send rejection.
nl -ba GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp | sed -n '345,395p'

# See how every result consumes one load-test attempt.
nl -ba GDS_Teensy/firmware/gds_teensy/src/gds_tx_load_test.hpp | sed -n '234,260p'

# Compare the current timeout path with the prior immediate recovery.
git diff b09195e -- GDS_Teensy/firmware/gds_teensy/src/artemis_rf23bp.hpp

# Run focused source tests.
python3 -m unittest \
  GDS_Teensy/tools/tests/test_rf_recovery_hardening.py \
  GDS_Teensy/tools/tests/test_rf_tx_retry.py

# Build the exact load-test image.
cd GDS_Teensy
./tools/arduino-cli/build_tx_load_test.sh
```

## Final conclusion

The code has one confirmed defect in its response to a wedge: it waits for three timeout results even though only the first attempt can transmit. This explains the groups of three and should be simplified to immediate SDN recovery.

The code does not contain evidence of a deterministic trigger for the first wedge. The first trigger occurs below the wrapper at the RadioHead interrupt and SPI boundary. The invalid register snapshots and the change from clean tests to intermittent failures with the same stationary node point to a radio/control-state failure that software detects but cannot yet classify. ISR telemetry is the smallest software-only experiment that can classify it further.
