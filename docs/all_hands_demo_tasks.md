# All-Hands Demo Readiness Summary

Planning window for this summary:
- March 23, 2026 to May 23, 2026

## What We Are Trying To Achieve

For the live demo, we do not need every spacecraft feature finished.

We need one reliable story:

- the satellite computer turns on,
- ground software connects,
- we can see basic health data,
- we can send a command over radio,
- the satellite responds,
- and we can show one simple mission-like result.

That is the goal we are freezing around.

## What The Codebase Tells Us Today

Right now the software is mostly a communication bridge.

What is already there:
- Raspberry Pi flight software can build and run.
- Flight Teensy and ground Teensy software builds and runs (need to verify UART and radio code).

What is not really there yet:
- Real mission/demo software beyond basic transport and ping handling.
- PDU software path.
- Payload-board software path and payload-board documentation.
- Enough Teensy hardening for live-demo reliability.
- A reliable, measured command-response path over radio.

## Why We Are Blocked Right Now

The main software problem is that commands are going out from the ground side, but we are not reliably getting telemetry or responses back.

The communication chain has several parts:

`Ground laptop -> ground radio bridge -> radio link -> flight radio bridge -> Raspberry Pi flight software -> back again`

Any one of those parts can be failing, and right now the team needs to isolate where the break is.

## What Needs To Happen First

### 1. Freeze the demo scope

- Agree on the exact demo we are promising.
- Stop adding features that do not directly support that demo.

### 2. Debug the communication chain step by step

- Confirm the flight computer is running.
- Confirm bytes reach the flight Teensy.
- Confirm the radio link passes data.
- Confirm the ground Teensy gives valid data to the laptop.
- Confirm the ground software can send one command and get one response.

### 3. Add better visibility

- We need better counters, logs, and test signals so we can see where packets are being lost.
- Right now we do not have enough clear visibility into the failing part of the path.

### 4. Make one command work reliably

- The immediate success target is not “all mission features.”
- The immediate success target is:
  one command in, one valid response out, every time.

## SMART Goals For The End Of The Semester

These are the concrete goals to finish by May 23, 2026.

### 1. Radio command path works reliably

- By April 17, 2026, the team completes 20 command-response trials in a row with no lost commands.
- Success means the operator can send a command from GDS and see a valid response back through the full RF chain.

### 2. Demo flight software exists, not just transport software

- By May 1, 2026, the Raspberry Pi software must support one real demo flow:
  one command-triggered action and one downlink result.
- Success means the operator can trigger that flow from GDS and watch state updates appear.

### 3. Payload role is defined and either real or simulated

- By March 30, 2026, assign a payload owner and deputy.
- By April 17, 2026, publish a simple payload test checklist.
- By May 8, 2026, either integrate a real payload result into the demo or lock a simulated fallback result.

### 4. Teensy software is hardened for operations

- By May 1, 2026, the Teensy programs only need the hardening required for the demo.
- They must support counters, timeout recovery, debug visibility, and stable long-duration operation.
- Success means both run for 40 minutes without locking up in bench testing.

### 5. The demo can be run by more than one person

- By May 15, 2026, a second team member must be able to bring up the system from the operator checklist.
- By May 23, 2026, the team must complete a full rehearsal and a recovery rehearsal on real hardware.

### 6. SatNOGS comm board is either ready or the fallback radio is officially chosen

- By March 30, 2026, assign a lead and deputy for the SatNOGS comm board.
- By April 10, 2026, the team must know whether the board is physically built and ready for bring-up.
- By April 24, 2026, the team must make a go/no-go decision:
  use the SatNOGS comm board, or fall back to the small 50-byte radio.
- By May 8, 2026, the selected RF path must be the one used by the demo stack.

## Other Important Work For Demo Readiness

### Payload board

- The N2 payload board needs to be documented and tested.
- The team needs to decide what exact role it will play in the demo.
- If it is not reliable enough in time, we should define a fallback demonstration path early.

### Flatsat usability

- The setup needs to be repeatable by more than one person.
- Build steps, startup order, ports, and recovery steps all need to be documented clearly.
- The team needs one clear operator checklist, not tribal knowledge.

### PDU and board interfaces

- The team should decide early whether PDU work is actually needed for this semester demo.
- If it is not demo-critical, it should be deferred instead of overloading the SWE team.

### SatNOGS comm board

- Someone needs to physically build it.
- The same small sub-team can handle bring-up and basic verification.
- If it slips, the fallback is the small 50-byte radio already on hand.
- That fallback is weaker, but it can still prove an RF link, so the decision needs to be made early.

### Rehearsal

- We need at least one full rehearsal on the actual hardware for the full presentation duration.
- That includes recovery planning if something fails live.

## Simple Message For The Team

The project is not blocked by a lack of big ideas.
It is blocked by one critical integration problem in the command-and-telemetry chain.

So the team focus should be:

1. freeze the demo goal,
2. fix the radio command/response path,
3. build the minimum flight and payload software needed for the story,
4. document the payload and operations flow,
5. rehearse the exact demo we plan to show.

## Decision To Confirm In The Meeting

We agree that demo acceptance is frozen to:

- flight software runs,
- health data is visible,
- one command works over RF,
- one response comes back,
- one mission-style result is shown,
- and the system stays stable during the live demo.

## Owner Decisions Needed In The Meeting

Before the meeting ends, assign named owners for:

- RF command/telemetry debugging
- Flight software demo logic
- Payload-board development
- SatNOGS comm board build, SWE support, and QA
- Demo operations and rehearsal

For each of those, also assign:

- one lead,
- one deputy,
- and one knowledge-transfer milestone so next year's team can take over.

For a small undergraduate SWE team:
- combine related areas where needed,
- keep PDU work only if it directly supports the demo,
- and choose the fallback radio early if SatNOGS is not moving fast enough.
