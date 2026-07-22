# Neutron 2 GDS Watchdog and Radio TX Stall Investigation

**Date:** 2026-07-20

**System:** Neutron 2 demo ground station, Teensy 4.1, RFM23BP radio, and F Prime GDS

**Purpose:** Support the UNP Flight Selection Review demo

**Status:** Investigation complete for the demo decision

## Decision

Do not use the Teensy hardware watchdog in the UNP demo firmware. The current
demo firmware removes it completely.

Do not spend more demo development time on this watchdog implementation.

Keep the bounded radio timeout, fault counters, and RFM23BP SDN recovery.

Use bounded RadioHead initialization, a 500 ms TX wait, and SDN recovery only.

## Summary

The GDS radio can transmit correctly with the tagged demo firmware. The tagged load test completed 520 of 520 transmissions at 30 dBm.

The TX stall has a strong relation to the watchdog implementation. The location of the watchdog feed changes the failure rate.

The RFM23BP also shows a power-on-reset condition during failed transmissions. RadioHead then stays in its software TX state.

The watchdog reset affects the Teensy. It does not remove power from the RFM23BP.

This reset difference can leave the Teensy and the radio in different states.

The team has investigated this critical issue since Tuesday, 2026-07-14. More watchdog work now creates schedule risk for the demo.

This hardware will not be the final satellite hardware. The final satellite needs a new and qualified reset design.

## Demo Intent

The present system is demo equipment. It must show the mission sequence during the UNP Flight Selection Review.

The demo must do these tasks:

1. Start the system in a known state.
2. Show live spacecraft health data.
3. Send an operator command.
4. Start data collection after a short delay.
5. Send science data to the ground station.
6. Show the science data in the ground tools.

An automatic Teensy reset is not necessary for this sequence. An operator can reset the ground equipment if a separate failure occurs.

## Investigation Scope

The investigation compared these software states:

- The `v1.0.0-mvp-demo` tagged firmware.
- The `codex/gds-teensy-tx-load-test` development branch.
- Selected commits between the tag and the development branch.
- Watchdog-enabled and watchdog-disabled test images.
- Images with different watchdog feed locations.

The investigation used the same Neutron 2 ground Teensy and replacement RFM23BP module.

The investigation also used a maximum RF power setting of 30 dBm and maximum 49-byte radio packets.

## Software Difference

The RadioHead library did not change between the tagged firmware and the development branch.

The development branch added these functions:

- A 12-second Teensy hardware watchdog.
- Watchdog feeds in the UART and RF paths.
- Watchdog feeds immediately before RF send operations.
- Bounded TX-completion waits.
- RF timeout retries.
- RFM23BP SDN reset and reinitialization.
- RF network and address checks.
- Additional fault registers and counters.

The first watchdog commit was `8862e2d`. This commit added watchdog feeds to the RF ACK and retry paths.

## Test Results

| Test | Result | Meaning |
|---|---:|---|
| Tagged direct RF load test | 520 of 520 passed | The replacement radio can transmit at 30 dBm. |
| Tagged production relay test | 125 of 125 local TX attempts passed | The tagged relay path can complete repeated transmissions. |
| Watchdog disabled after an SDN reset | 125 of 125 passed | The radio stayed stable without the hardware watchdog. |
| Watchdog enabled with a pre-send feed | Stall after approximately 15 attempts | The watchdog feed location increased the failure rate. |
| Watchdog enabled with only the pre-send feed removed | 125 of 125 passed | The pre-send feed was a repeatable failure trigger. |
| Current development branch smoke test | 0 of 10 passed | The current branch repeatedly entered local TX timeout recovery. |
| Physical power-cycle boot | First bounded init attempt reached READY | The failure is not a permanent startup failure. |
| First TX after physical power cycle | 1 of 1 timed out at 28 dBm | A stale radio state alone does not explain the failure. |
| Three consecutive bounded timeouts | One SDN reset and reinit completed | Software contained the fault and returned the radio to READY. |

These results show a strong relation between the watchdog implementation and the TX stall.

These results do not show that a watchdog feed uses significant radio power. A watchdog feed only writes two internal MCU registers.

The feed can change MCU, interrupt, and radio timing. This timing change can increase the probability of the radio fault.

## Captured Failure State

The current branch captured this failure state:

```text
nirq=0
rh_mode=3
reg05=00
reg06=03
reg07=01
irq03=20
irq04=03
```

The value `rh_mode=3` means that RadioHead still reports TX mode.

The value `irq04=03` contains the RFM23BP `IPOR` and `ICHIPRDY` bits. These bits show a radio power-on-reset event.

The values in registers `0x05`, `0x06`, and `0x07` also changed to reset values.

The software and hardware states no longer agree after this event.

RadioHead waits for a packet-sent interrupt. The reset radio cannot send that interrupt for the old TX operation.

The old unbounded wait stalls forever. The later bounded wait reports the fault.

## Root Cause Assessment

### Confirmed failure mechanism

1. The ground Teensy starts an RF transmission.
2. The RFM23BP resets during the TX operation.
3. The radio loses its active configuration.
4. RadioHead keeps its software TX state.
5. RadioHead does not receive the expected packet-sent interrupt.
6. An unbounded TX wait stalls the firmware.
7. A Teensy watchdog reset does not guarantee a radio reset.

### Likely watchdog contribution

The watchdog implementation is a repeatable timing amplifier.

The pre-send watchdog feed changes the timing immediately before the radio TX operation.

This change increases the observed failure rate. The test results support this conclusion.

The watchdog can also reset the Teensy during an RF or SPI operation. The RFM23BP can remain powered after that reset.

### Electrical trigger

The exact electrical trigger is not confirmed. The investigation did not include an oscilloscope capture at the radio pins.

Possible triggers include:

- RFM23BP supply voltage drop during the high-power TX current pulse.
- Ground movement between the Teensy and the radio.
- An unwanted pulse on the SDN signal.
- RF energy in a control or power signal.

The physical radio, power, and ground path remain active suspects. A true physical
power cycle later produced a clean READY state, but the first 28 dBm TX still caused
the same POR signature. This makes a persistent radio state insufficient as the
sole cause.

## Final Same-Hardware A/B Test

The team removed the watchdog and restored the tagged RadioHead initialization and TX sequence.
The updated code kept one bounded 500 ms TX wait and SDN recovery after a failure.

The updated image failed on its first +30 dBm TX operation. The team stopped the test early. The
fault snapshot showed reset register values and the POR and CHIPRDY flags.

The same image also failed on its first +28 dBm TX operation.

The team then flashed the tagged diagnostic image without changing the board, cable, power, or
radio. This image had passed 197 TX operations before one failure during an earlier test. It now
passed only 5 TX operations before the first failure. This is not a true physical A/B comparison:
the RFM23BP remained powered across the firmware uploads and could retain a bad state.

After a true physical power cycle, the same image initialized normally but its first 28 dBm TX
again produced POR and CHIPRDY. This shows that the current frequent reset is not caused only by
the development branch and is not only a persistent-state problem. The remaining trigger is in
the live radio, power, ground, wiring, or RF environment.

The software still needs the following controls:

- No Teensy watchdog.
- One TX attempt for each packet.
- One bounded TX-completion wait.
- Up to three SDN-reset RadioHead initialization attempts, each with a 100 ms CHIPRDY limit.
- SDN reset and reinitialization after three consecutive TX timeouts.
- No immediate high-power retry.

## Why More Watchdog Work Is Not Worth the Time

The watchdog does not support a required UNP demo function.

The tagged firmware gave a stable software baseline during earlier tests. The same tagged image now
fails on the current hardware state, so it cannot make the present radio path reliable by itself.

The bounded radio timeout and SDN recovery give useful fault control without an MCU watchdog reset.

Additional watchdog work needs more design, code, hardware tests, and reset tests. This work can delay the UNP demo preparation.

The issue has already been critical since 2026-07-14. Continued work gives a low return for the remaining demo schedule.

The final satellite will not use this demo hardware. The final watchdog design must use the final power and reset architecture.

A flight watchdog also needs qualification tests. These tests must include reset timing, power faults, radio state, and command recovery.

This work belongs in the final satellite development plan. It does not belong in the UNP demo critical path.

## Recommended Demo Configuration

Use this configuration for the UNP demo:

- Use the tagged Neutron 2 radio behavior as the baseline.
- Disable the Teensy hardware watchdog by default.
- Keep every RF TX wait bounded.
- Keep the 500 ms TX-completion timeout.
- On startup, allow at most three RFM23BP SDN-reset initialization attempts.
- Reset and reinitialize the RFM23BP only after three consecutive TX timeouts.
- Reset the radio early after any known MCU reset.
- Keep the RF fault counters and debug output.
- Run the ground-only RF load test before the review.
- Do not add new radio hardening features before the review.

## Stop-Work Rule

Stop watchdog development for the UNP demo after the team disables the watchdog in the demo build.

Restart watchdog development only if the UNP demo has a requirement for automatic MCU reset.

Do not reuse this watchdog implementation as flight code without a new design review and hardware qualification.

## Final Conclusion

The watchdog implementation has a strong and repeatable relation to the RFM23BP TX stall.

The pre-send feed can amplify the timing condition. A later watchdog reset can also preserve a bad radio state.

The RFM23BP reset event is real. A physical power cycle did not prevent it: the first subsequent
28 dBm TX produced the same POR signature. The exact electrical trigger needs voltage, ground,
SDN, and NIRQ measurement during TX.

The UNP demo does not need these tests. The final satellite will use different hardware and needs a new watchdog design.

The correct program decision is to disable the demo watchdog and stop additional development now.
