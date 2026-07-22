# C3M GDS Full-Day RF Load-Test Findings

Date: 2026-07-22
Location: EPSCoR C3M, 8th floor
Node: Ground GDS Teensy 4.1 and RFM23BP
Branch: `codex/gds-teensy-tx-load-test`
Git base at time of report: `9b3683f`
Primary test: 49-byte ground-only RF packets, 100 ms interval, usually 30 dBm

## BLUF

The evidence points to a hardware, RF, power, thermal, or signal-integrity trigger at the local ground RFM23BP. The software detects the failure and recovers from it, but the software does not explain why the first TX-complete event disappears.

The strongest findings are:

- The same firmware and test settings produced both clean and faulty runs.
- A full electrical disconnect was followed by a clean `1000/1000` run at 30 dBm.
- SDN reset recovered every observed wedge, but later wedges could still recur without a full power removal.
- An aluminum sheet near the RF path correlated with the worst recent run: seven real wedges. Removing it without resetting the Teensy reduced the next run to two wedges.
- The aluminum sheet was probably not simple over-the-air interference. A conductor near an antenna can change antenna impedance and reflected power. At 30 dBm, this can stress the PA or disturb the radio and nearby digital signals.
- Every fault snapshot showed RadioHead stuck in TX mode and invalid-looking SPI register reads.
- The bench radio receives 5 V through a direct jumper from the Teensy USB-derived supply. USB cable, connector, VUSB, and ground impedance can therefore disturb the radio supply and the SPI voltage reference during a 550 mA TX transient.
- The watchdog was not present. The satellite, ACK path, F Prime, and normal relay path were not required for these failures.
- Setting the radio to IDLE before SDN made the recovery sequence cleaner. It did not prevent the first wedge.

The root mechanism is not proven. The best current ranking is:

1. Antenna/load mismatch, reflected power, or RF coupling into the module or control wiring.
2. PA heating or another cumulative temperature-dependent condition.
3. RFM23BP VCC droop, ground bounce, or inadequate transient current at 30 dBm.
4. SPI or NIRQ signal-integrity failure during high-power TX.
5. Module, connector, antenna, coax, or solder-joint intermittency.
6. A rare software interrupt race. This remains possible, but the changing failure rate with the same code makes it less likely.

## Test setup

The load image isolates the local ground transmitter:

- It sends fixed 49-byte packets directly through `Rf23Driver::send()`.
- It does not require a satellite receiver.
- It does not wait for an RF ACK.
- It skips the normal relay path during the RF load test.
- It uses a bounded 500 ms local TX-completion wait.
- It requests an SDN reset and full radio initialization after three timeout results.

The normal 30 dBm configuration is in [`artemis_rf23bp.hpp`](../firmware/gds_teensy/src/artemis_rf23bp.hpp). The load loop is in [`gds_tx_load_test.hpp`](../firmware/gds_teensy/src/gds_tx_load_test.hpp).

## Full-day results

The table contains the durable result from each July 22 log. “Real wedges” counts only lines that said `RF23BP TX completion timeout`. It does not count the two later pre-send rejections caused by the same wedge.

| Approx. time | Condition | Power | Successful sends | Reported failures | Real wedges | SDN recoveries |
|---|---|---:|---:|---:|---:|---:|
| 10:24 | Initial smoke | 30 dBm | 125 | 0 | 0 | 0 |
| 10:30 | First long run | 30 dBm | 997 | 3 | 1 | 1 |
| 10:34 | Immediate repeat | 30 dBm | 997 | 3 | 1 | 1 |
| 11:17 | Power comparison | 28 dBm | 1000 | 0 | 0 | 0 |
| 11:24 | 28 dBm repeat | 28 dBm | 985 | 15 | 5 | 5 |
| 11:28 | Stationary node | 30 dBm | 985 | 15 | 5 | 5 |
| 11:38 | Stationary repeat | 30 dBm | 985 | 15 | 5 | 5 |
| 11:58 | IDLE-before-SDN image | 30 dBm | 997 | 3 | 1 | 1 |
| 12:01 | Aluminum sheet present; operator observation | 30 dBm | 979 | 21 | 7 | 7 |
| 12:04 | Sheet removed; no Teensy reset | 30 dBm | 994 | 6 | 2 | 2 |
| 12:08 | Full electrical disconnect and reconnect | 30 dBm | 1000 | 0 | 0 | 0 |

The ten 1,000-attempt runs produced:

- `9,919` successful sends.
- `81` reported failures.
- `27` real TX-completion wedges.
- `54` derived pre-send rejections.
- `27` successful SDN recoveries.

The test image counts success plus failure results until it reaches the requested target. Therefore, `985/1000` means 985 successful sends and 15 failed attempts. It does not mean that the test attempted 1,015 sends.

## Why the failures always appear in groups of three

The group of three is a software counting artifact. It is not evidence of three independent radio failures.

The sequence is:

```text
RF23BP TX completion timeout      # one packet was started
RF23BP pre-send TX wedge          # no new packet was started
RF23BP pre-send TX wedge          # no new packet was started
RF23BP ready                      # SDN/POR recovery completed
```

The first event is the real wedge:

1. RadioHead starts TX and sets its software state to `RHModeTx`.
2. RadioHead waits for the packet-sent interrupt.
3. The interrupt path does not change the state back to IDLE within 500 ms.
4. The bounded wait returns `TX_TIMEOUT`.

The next two attempts cannot transmit. The pre-send guard sees that RadioHead is still in `RHModeTx` and rejects each attempt. On the third timeout result, [`rf23_driver.cpp`](../firmware/gds_teensy/src/rf23_driver.cpp) requests SDN recovery.

Thus:

```text
reported failures = real wedges x 3
```

This relation held for every faulty run today.

## What the fault snapshot proves

Every observed fault used the same snapshot shape:

```text
nirq=1 rh_mode=3
reg00=00 reg01=08 reg02=08 reg05=08 reg06=08
reg07=08 reg08=08 reg26=08 irq03=08 irq04=08
```

This proves:

- `rh_mode=3`: RadioHead remained in TX mode.
- `nirq=1`: NIRQ was not asserted when the main-loop snapshot ran.
- The register readback was not credible. Many unrelated registers returned the same `0x08` value, while the device-type register returned `0x00`.

A healthy initialization snapshot looked different and stable:

```text
identity_stable=1 nirq=1 rh_mode=4
reg00=08 reg01=06 reg02=21 reg05=B7 reg06=40
reg07=05 reg08=00 reg30=8D reg6D=0F
```

The faulty snapshot therefore points to an SPI/control failure or a radio state in which SPI readback is no longer valid. It does not look like a normal radio register state.

The snapshot does not prove whether:

- the RFM23BP failed to generate packet-sent;
- NIRQ asserted but the Teensy missed the falling edge;
- the ISR ran but SPI failed inside it;
- the radio, PA, supply, or ground entered an invalid state during TX.

## Why the evidence favors hardware

### Same code, different results

The same load path, packet size, interval, modem, timeout, and power setting produced results from `1000/1000` to `979/1000`.

The only deliberate firmware change during the later phase was the addition of:

```text
set RadioHead IDLE -> wait 1 ms -> assert SDN
```

This code runs only after the third timeout result. It cannot create the first TX-completion timeout. The later image still had a real wedge on its first long run.

### A full electrical restart cleared the observed condition

After the operator unplugged the GDS, allowed it to discharge, and reconnected it, the next 30 dBm test completed `1000/1000` with no recovery.

This is stronger than an SDN reset:

- SDN resets radio register state but leaves the external VCC rail present.
- A full disconnect removes the rail and allows stored charge to decay.
- A full disconnect can also provide cooling time.

The clean run does not tell us whether rail removal or cooling was responsible. It shows that the problem is not a deterministic packet-number or software-loop failure.

### SDN recovery works but does not permanently remove the condition

All 27 real wedges recovered through SDN and reinitialization. The radio resumed sending each time. Some later runs then wedged again.

This means:

- The software containment path works.
- The module is not permanently dead after a wedge.
- Register reset alone does not always remove the condition that increases the chance of another wedge.

That condition can be thermal, RF, power, grounding, analog bias, or an intermittent physical connection.

### The aluminum sheet is a meaningful RF clue

The aluminum-sheet run had seven real wedges. The next run, with the sheet removed and without a Teensy reset, had two.

This is a correlation, not proof. There was only one run in each condition. However, the direction is physically plausible.

A conductive sheet near the antenna can:

- detune the antenna;
- change its impedance and standing-wave ratio;
- reflect RF power toward the PA;
- change near-field coupling into SPI, NIRQ, SDN, power, and ground wiring;
- create hot spots or stronger local fields around the board.

This is different from ordinary receiver interference. The failure is local TX completion, so path loss to a remote receiver is irrelevant. Antenna loading and reflected energy can still affect the local transmitter.

RadioHead’s own RFM23BP documentation warns that +30 dBm needs about 550 mA and a proper 50-ohm RF load. It specifically warns that an unsuitable antenna or supply can cause transmitter hangs and PA overheating.

### Room occupancy is a secondary variable

The operator reported that other people had left for lunch during the two tests before the final power cycle, and that the room was empty except for the operator during the final test.

People can change RF absorption and multipath. This should be recorded, but it is not the leading explanation because the test does not require a receiver. Occupancy could matter only if it changed antenna loading or local RF coupling. The full electrical reset and the aluminum sheet are stronger variables.

### USB power can indirectly corrupt SPI

The USB data lines do not share the RFM23BP SPI1 pins. Normal USB D+ and D- traffic should not directly change MOSI, MISO, SCLK, or chip select.

The USB power and ground path is different. It is a credible common cause because the installed ground-node radio receives 5 V through a direct jumper from the Teensy USB-derived supply.

The approximate simultaneous load at 30 dBm is:

- Teensy 4.1 at 600 MHz: about 100 mA.
- RFM23BP at +30 dBm: about 550 mA.
- Combined minimum before other board loads: about 650 mA.

This current must pass through the computer or hub, USB cable, Micro-USB connector, Teensy VUSB path, jumper, and common ground return. A capable USB source can provide this current, so this calculation does not prove an overload. It does show that cable resistance, connector contact, source current limiting, and ground impedance are relevant at every TX start.

A short VCC drop or ground shift can cause either of these effects:

1. The radio's internal digital supply or state machine becomes unstable.
2. Teensy and radio no longer agree on the voltage reference for SPI, NIRQ, or SDN.
3. CS, SCLK, MOSI, or MISO crosses a logic threshold at the wrong time.
4. The diagnostic read returns repeated or invalid values even if the firmware generated the correct transaction.

This mechanism fits the observed fault snapshots better than ordinary USB software traffic. The snapshots contained repeated invalid-looking register values while RadioHead remained in TX.

PJRC documents that USB power enters the Teensy 4.1 at VUSB and that VUSB is normally connected to VIN. The same source feeds the Teensy's 3.3 V regulator. PJRC also states that Teensy 4.1 signal pins accept only 0 to 3.3 V and are not 5 V tolerant. See [PJRC Teensy 4.1 power and digital-pin documentation](https://www.pjrc.com/store/teensy41.html).

The Artemis manual confirms the system assumptions:

- Teensy VIN normally connects to the regulated `5V_BUS`.
- The RFM23BP uses SPI1 and requires a common ground.
- The RFM23BP draws about 550 mA at +30 dBm.
- The radio accepts 3.3 to 6 V, with 5 V used for full-power operation.

See [`Artemis User's Manual - April 2026.txt`](../../docs/Artemis%20User's%20Manual%20-%20April%202026.txt), especially the Teensy power table and RFM23BP section.

The OBC v4.24 schematic also separates USB data from SPI1. It connects Teensy pins 26, 27, 38, and 39 to the radio SPI nets. The designed RFM23BP VCC path goes through R8 to `SW_3V3_2`. The direct 5 V jumper on this bench node bypasses that designed radio-power path. The short U3-VCC/R8 net in the schematic has no external bulk reservoir capacitor shown; the module can still have onboard decoupling.

Do not connect an independent 5 V supply to Teensy VIN while normal USB VUSB remains joined to VIN. PJRC warns that this can backfeed the computer. Cut or otherwise isolate the documented VUSB-VIN connection before a dual-connected test, then retain one solid common ground.

The decisive test is an oscilloscope capture at the module, not at the USB connector:

1. Measure RFM23BP pad-13 VCC relative to the nearest RFM23BP ground pad.
2. Use a short probe spring ground.
3. Trigger on the start of TX or the amplifier-control transition.
4. Capture VCC, CS, SCLK, MISO, and NIRQ around the first wedge.
5. Compare a short direct USB cable, a powered USB hub, and an isolated regulated radio supply one variable at a time.

Current conclusion: USB data traffic is a low-probability direct cause. USB-derived power, cable impedance, connector condition, and the shared ground reference are high-priority physical suspects.

## What today ruled out or weakened

### Watchdog implementation

The load image has no watchdog. The fault still occurred. The watchdog can aggravate a separately powered radio during an MCU reset, but it is not the cause of today’s ground-only wedges.

### Missing satellite ACK

`waitPacketSent(500)` waits for local transmitter completion. It does not wait for a packet from the satellite. The satellite can be absent and this local completion event should still occur.

### F Prime, GDS, and relay traffic

The load command runs directly on the GDS Teensy and isolates the normal relay poll. F Prime commands, GDS framing, UART queues, RF ACK/retry, and satellite behavior cannot cause the first local completion timeout in this test.

### A 500 ms timeout that is too short

The packet is 49 bytes at 125 kbps. A normal transmission completes in far less than 500 ms. Thousands of packets completed with the same timeout. Increasing the timeout would only wait longer after the radio has already failed to report completion.

### Board movement

Two stationary 30 dBm tests each produced five real wedges. Movement is not required.

### TX power alone

One 28 dBm run was perfect. Its immediate repeat had five real wedges. The data does not show a stable monotonic difference between 28 and 30 dBm. Power may affect probability, but today’s uncontrolled sequence cannot quantify it.

### Stale TX mode before SDN

The new recovery explicitly commands IDLE, waits 1 ms, and then asserts SDN. Wedges still occurred. A stale RadioHead TX state is the visible symptom, not the initial trigger.

## Ranked root-cause analysis

### 1. RF load, reflected power, or RF coupling

Confidence: medium-high.

Reasons:

- The worst recent run occurred with an aluminum sheet near the RF path.
- Removing the sheet reduced the next run’s wedge count without an MCU reset.
- RFM23BP high-power guidance specifically warns about 50-ohm matching, transmitter hangs, and PA overheating.
- The failure occurs during local high-power TX.

Missing proof:

- No directional-coupler, return-loss, or dummy-load measurement.
- No controlled repeated sheet/no-sheet experiment.

### 2. Thermal accumulation

Confidence: medium-high.

Reasons:

- Testing continued for hours.
- Runs often became worse after earlier clean runs.
- A full disconnect and rest preceded a clean run.
- +30 dBm uses the PA at its maximum setting.

Missing proof:

- No PA or module temperature log.
- The full disconnect changed both temperature and electrical state.

### 3. Supply transient or ground-reference failure

Confidence: medium-high.

Reasons:

- The module requires about 550 mA at +30 dBm.
- The direct jumper makes the USB cable, VUSB path, and USB ground part of the radio's high-current supply and signal reference.
- Teensy plus radio draw about 650 mA before other board loads during +30 dBm TX.
- Fault-time SPI reads are invalid and repetitive.
- High-current TX can cause VCC droop, ground bounce, or logic-reference movement.
- Full power removal clears the observed condition.

Missing proof:

- No oscilloscope capture at the RFM23BP VCC and ground pins during a failing TX.
- No current waveform or local decoupling measurement.
- No controlled comparison between USB sources or an isolated regulated radio supply.

### 4. NIRQ or SPI signal integrity

Confidence: medium.

Reasons:

- RadioHead remains in TX when the packet-sent ISR path does not complete.
- Fault-time SPI register reads are not credible.
- High-power RF can couple into interrupt and SPI wiring.

Missing proof:

- No logic-analyzer or oscilloscope capture of NIRQ, CS, SCLK, MOSI, and MISO at the first timeout.

### 5. Module, antenna, coax, or solder intermittency

Confidence: medium.

Reasons:

- The failure rate changes without code changes.
- A replaced module does not rule out connectors, antenna load, solder joints, power wiring, or board-level return paths.

Missing proof:

- No one-variable swap with a known-good antenna, coax, dummy load, module, or board.

### 6. Software interrupt race

Confidence: low-medium.

Reasons it remains possible:

- RadioHead relies on one interrupt edge and destructive status-register reads.
- The current diagnostics run after the timeout and cannot reconstruct the ISR event exactly.

Reasons it is lower ranked:

- The same deterministic code can run 1,000 sends cleanly.
- Failure probability changes with physical conditions and a full electrical restart.
- Fault-time SPI state is invalid, not merely a valid `IPKSENT` flag that software ignored.

## Software assessment

The current software is useful containment, not root-cause repair.

What works:

- TX wait is bounded at 500 ms.
- A wedged TX cannot block the Teensy forever.
- Recovery controls the amplifier, commands IDLE, asserts SDN, waits through POR, initializes SPI and RadioHead, and returns to RX.
- Every observed SDN recovery succeeded.

What should remain simple:

- Keep watchdogs removed for this demo code.
- Keep one bounded TX wait.
- Keep one SDN/POR initialization sequence.
- Keep the RadioHead configuration close to the known library path.

What should be corrected later:

- Recover after the first real 500 ms completion timeout. Attempts two and three cannot transmit while RadioHead remains in TX.
- Count real completion timeouts separately from pre-send TX-mode rejections.
- Add small in-memory ISR diagnostics only if more software evidence is needed. Do not print from the ISR.

These changes would improve recovery and evidence. They would not fix a failing RF load, PA, supply, ground, or module.

## Recommended next bench test

Do not run more uncontrolled all-day soaks. Use a short controlled matrix.

### Instruments

- Proper 50-ohm dummy load rated for at least 1 W, or a verified 50-ohm antenna and coax.
- Oscilloscope with a short spring ground at the RFM23BP VCC and ground pins.
- Temperature probe or thermal camera on the PA transistor and module.
- If available, a directional coupler or antenna analyzer for reflected power or return loss.
- Logic analyzer or scope channels for NIRQ, CS, SCLK, and MISO.

### Controlled sequence

1. Fully power off and allow the board to return to room temperature.
2. Fix antenna, coax, board, and cable positions. Remove nearby metal.
3. Record starting PA temperature and module VCC.
4. Run five 1,000-packet tests at 30 dBm with a fixed cool-down interval.
5. Repeat five tests at 28 dBm in an alternating order, not all after the 30 dBm tests.
6. Repeat with a verified dummy load.
7. Only then repeat a measured sheet-distance experiment if RF loading is being studied.
8. On the first wedge, capture VCC, ground, NIRQ, CS, SCLK, and MISO.

Log for every run:

- exact start time;
- power setting;
- packet interval;
- antenna or dummy-load identity;
- antenna and metal-object geometry;
- starting and ending temperature;
- room occupancy;
- USB supply and cable;
- successful sends, real wedges, pre-send rejections, and SDN recoveries.

### Important discriminator

If the radio wedges into a known 50-ohm dummy load while VCC, ground, SPI, and NIRQ remain valid, investigate the module and RadioHead interrupt path.

If VCC or ground moves, SPI becomes corrupt, temperature rises sharply, or the problem follows antenna geometry, the trigger is hardware/RF rather than the TX wrapper.

## Demo recommendation

For the UNP flight-selection demo:

- Keep the simple bounded TX and SDN recovery.
- Cold-start the GDS before the demonstration.
- Keep metal away from the antenna and board.
- Use a verified antenna/coax connection and stable 5 V source.
- Provide airflow and avoid unnecessary continuous 30 dBm soaks before the demo.
- If link margin permits, use less than 30 dBm. Today’s data does not prove that 28 dBm alone fixes the issue.
- Do not spend more demo software time trying to hide an unmeasured RF, power, or thermal defect.

The current software is adequate to contain an intermittent wedge. Hardware measurement is now the shortest path to the actual root cause.

## Evidence files

The raw logs were captured under `/tmp` during the test session:

```text
c3m-8th-floor-gds-rf-125x-30dbm-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-repeat-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-28dbm-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-28dbm-repeat-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-stationary-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-stationary-repeat2-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-idle-sdn-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-idle-sdn-repeat-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-no-interference-no-reset-2026-07-22.log
c3m-8th-floor-gds-rf-1000x-30dbm-electrical-restart-2026-07-22.log
```

`/tmp` is not durable storage. This report preserves the important counters and observations.

## Final conclusion

Today’s tests do not support a deterministic firmware root cause. They support an intermittent local transmitter failure whose probability changes with physical and accumulated conditions.

The software-level failure is clear: RadioHead remains in TX because the packet-sent interrupt path does not complete. The physical reason is still open. The most likely causes are RF load/reflected power, PA heating, VCC/ground disturbance, or RF-induced corruption of SPI/NIRQ.

The clean run after a full electrical disconnect is the most important new result. The high wedge count with the aluminum sheet is the second most important. Together, they justify moving the investigation from more firmware rewrites to controlled RF, power, temperature, and signal measurements.
