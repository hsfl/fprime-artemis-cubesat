# Demo Readiness Task Board

## Objective

Primary objective: make the Neutron 2 flatsat demo reliable enough to show basic command, control, and proof of mission concept over the RF link.

Current highest-risk software issue: commands sent from GDS are not producing reliable telemetry or responses back through the full chain:

`Laptop GDS -> Ground Teensy -> RF -> Flight Teensy -> Raspberry Pi/F' -> back`

Semester planning window for this board:
- Start: March 23, 2026
- End: May 23, 2026

## Current Repo Reality

The codebase today is still mostly transport plumbing, not full demo software.

What exists now:
- Flight F' deployment with `TeensyLink` and `PingResponder`
- Raspberry Pi UART driver wiring
- Flight Teensy RF/UART relay firmware
- Ground Teensy RF/USB relay firmware

What is clearly missing in the active code:
- No PDU flight component or PDU interface implementation
- No payload flight component or payload-board software in this repo
- No GPS or IMU component path into F'
- No mission/demo state-machine logic for Base Mode, Data Collection, or Science TX
- No deterministic GDS uplink packet extraction
- No true transport status query from F' into the Teensy bridge
- No non-relay Teensy application superloop beyond `g_relay.poll()`

## Frozen Demo Acceptance Criteria

The team agrees to freeze demo success to the following:

1. The flight Raspberry Pi boots and runs the F' deployment without crashing.
2. The operator can connect GDS and display basic status-of-health data.
3. The operator can send at least one command over the RF link and receive a visible response.
4. The system can demonstrate one short mission-style action.
5. The system can downlink one clear proof artifact:
   a status update, payload result, or simulated science data.
6. The system can stay stable for the length of the live demo window.

Anything not directly improving these outcomes is lower priority until the chain is stable.

## Semester-End SMART Goals

All goals below must be complete by May 23, 2026.

### Goal 1: End-to-end command/telemetry chain works over RF

Specific:
- Restore a reliable command and response path across:
  `Laptop -> Ground Teensy -> RF -> Flight Teensy -> RPi/F' -> back`

Measurable:
- Complete 20 consecutive command-response trials with zero lost commands.
- Show at least one command acknowledgment, one event, and one telemetry update in GDS.
- Record bridge counters before and after the test.

Achievable:
- Uses the current transport architecture already present in the repo.

Relevant:
- This is the main blocker to the demo.

Time-bound:
- Due April 17, 2026.

### Goal 2: Flight F' software supports one simple demo flow

Specific:
- Extend the flight software beyond `TeensyLink` and `PingResponder` to support one actual demo scenario.
- Implement the smallest possible demo controller or mission-state path that supports:
  idle or base mode, one command-triggered action, and one visible result.

Measurable:
- Operator can trigger the demo flow from GDS.
- Flight software emits telemetry/events for mode transitions and result completion.
- Demo flow succeeds in three back-to-back runs on hardware.

Achievable:
- Scope is limited to one demo path, not a full mission stack.

Relevant:
- The current flight app has transport but almost no mission behavior.

Time-bound:
- Due May 1, 2026.

### Goal 3: Payload contribution is defined and demonstrated, or explicitly simulated

Specific:
- Assign one person to payload-board development and one backup owner.
- Define what the payload board contributes to the demo:
  real payload data, payload state change, or simulated result if hardware is not ready.
- Produce payload-board test notes and a repeatable bring-up or fallback procedure.

Measurable:
- Named owner and backup recorded.
- Payload interface note exists.
- At least one payload bench test is run and documented.
- Payload output is either integrated into the demo or an approved fallback is defined.

Achievable:
- Can use a fallback simulated result if the real payload is not stable enough.

Relevant:
- The live demo needs a mission-like proof point, and the repo currently contains no payload-board implementation.

Time-bound:
- Owner assigned by March 30, 2026.
- Interface/test note by April 17, 2026.
- Demo integration or fallback locked by May 8, 2026.

### Goal 4: Teensy firmware is hardened enough for a live demo

Specific:
- Keep the Teensy firmware scope small.
- Add only the hardening needed for the demo:
  counters, timeout recovery, debug visibility, and stable long-duration operation.

Measurable:
- Both Teensy apps have documented responsibilities.
- Both Teensy apps expose debug counters without disrupting normal GDS traffic.
- Both Teensy apps run continuously for 40 minutes without lockup during bench test.

Achievable:
- Builds on the current `g_relay.poll()` firmware.

Relevant:
- Current Teensy code is still bare relay logic and needs operational hardening for a live demo.

Time-bound:
- Due May 1, 2026.

### Goal 5: The full demo is rehearsable by people other than the primary developer

Specific:
- Produce a known-good build, operator checklist, smoke test, and rehearsal checklist for the actual demo stack.

Measurable:
- A second operator can follow the operator checklist and bring up the system.
- One full 30 to 40 minute rehearsal completes on hardware.
- All critical failures have recovery steps documented.

Achievable:
- Uses the existing build and procedure docs already in the repo.

Relevant:
- A live audience demo cannot depend on one person remembering tribal knowledge.

Time-bound:
- Due May 15, 2026.

### Goal 6: SatNOGS comm board is either ready quickly or the fallback radio is locked early

Specific:
- Assign a small SatNOGS sub-team:
  one lead and one deputy.
- Decide quickly whether the SatNOGS board is realistic for this semester demo.
- Verify whether the SatNOGS board can replace the current small 50-byte radio path for the demo.
- If it cannot be made reliable in time, formally lock the small 50-byte radio as the fallback RF path.

Measurable:
- Board assembly status is documented.
- Power-on and communication bring-up checklist exists.
- At least one basic bring-up test is completed on hardware.
- Team makes a documented go/no-go decision between SatNOGS comm board and the small radio fallback.

Achievable:
- Allows either successful SatNOGS adoption or a deliberate fallback before demo week.

Relevant:
- This is an urgent demo risk and affects which RF path the team will present live.

Time-bound:
- Lead and deputy assigned by March 30, 2026.
- Physical build status and bring-up checklist by April 10, 2026.
- Go/no-go decision by April 24, 2026.
- Selected RF path fully integrated by May 8, 2026.

## Priority Levels

`P0` Demo-critical. Must be done first.

`P1` Important for demo readiness and team execution.

`P2` Useful later, but not required before the demonstration.

## P0 Demo-Critical Tasks

### P0.1 Freeze and publish the demo path

- Finalize one hard success path for the demonstration.
- Define the exact operator actions, expected responses, and what counts as a pass.
- Cut or defer extra mission features that are not required for the live audience demo.

Deliverable:
- Final demo checklist approved by team leads.

### P0.2 Debug the end-to-end chain in stages

- Validate the chain as separate stages instead of debugging everything at once.
- Stage A: Raspberry Pi flight app runs and stays alive.
- Stage B: Raspberry Pi UART traffic reaches the flight Teensy.
- Stage C: Flight Teensy RF transmit reaches the ground Teensy.
- Stage D: Ground Teensy outputs valid bytes to the laptop.
- Stage E: GDS sends one command and receives one valid response.

Deliverable:
- Pass/fail matrix with observed behavior and failure owner for each stage.

SMART target:
- By April 3, 2026, capture one recorded pass/fail result for each stage A-E.
- By April 10, 2026, identify the first failing stage with logs/counters attached.

### P0.3 Add better observability and debug signals

- Add real bridge status visibility on the Raspberry Pi side.
- Expose actual Teensy/RF counters that help identify where packets are being lost.
- Add a known test-message path so the team can verify byte integrity without full mission traffic.
- Make it possible to inspect counters on both Teensy nodes without breaking the GDS stream.

Deliverable:
- Counter readout or debug mode that identifies whether failures are on UART, RF, framing, or reassembly.

SMART target:
- By April 10, 2026, both Teensy bridges and the RPi side must expose enough counters to distinguish:
  UART failure, RF send failure, reassembly failure, timeout, and command-path failure.

### P0.4 Fix the uplink command path

- Replace or harden the current simple burst-based USB-to-RF uplink behavior.
- Implement deterministic packet-boundary extraction so GDS commands are forwarded as complete packets.
- Verify that one uplink command turns into one valid command on the flight side.

Deliverable:
- Reliable single-command uplink over the radio chain.

SMART target:
- By April 17, 2026, achieve 20 consecutive successful uplink command trials with zero packet-boundary errors.

### P0.5 Verify the downlink response path

- Confirm the flight Teensy strips only the custom UART wrapper and preserves the payload.
- Confirm the ground Teensy reassembles RF segments correctly and emits the expected bytes to GDS.
- Confirm GDS framing and dictionary configuration match the bytes produced by the chain.

Deliverable:
- Reliable return path for command response, events, and basic telemetry.

SMART target:
- By April 17, 2026, show one command response, one event, and one telemetry value returning through GDS over RF.

### P0.6 Decide on minimum RF reliability hardening

- Measure command success rate over repeated trials.
- If needed, add the smallest possible retry or ACK behavior for demo-critical transactions.
- Avoid broad protocol redesign unless data shows it is necessary.

Deliverable:
- Chosen reliability strategy for the demo path, with measured justification.

SMART target:
- By April 24, 2026, decide whether ACK/retry is required based on recorded trial data, not guesswork.

### P0.7 Build the demo-day smoke test

- Write one short bring-up and verification procedure for demo day.
- Include power-on order, port checks, process launch order, first command, expected counters, and reset steps.
- Make the procedure usable by someone other than the primary developer.

Deliverable:
- Demo-day smoke test checklist and operator checklist.

SMART target:
- By May 15, 2026, a second team member must complete the smoke test without assistance.

## P1 Demo-Readiness Tasks

### P1.1 Make F' usable on flatsat by the team

- Clean up the operator checklist so another engineer can build, launch, and verify the setup.
- Lock a known-good build set for Raspberry Pi, flight Teensy, and ground Teensy.
- Record exact port names, startup behavior, and expected outputs.

Deliverable:
- Team-usable flatsat software operator checklist.

SMART target:
- By May 8, 2026, another engineer can build, launch, and verify the stack using the documented procedure only.

### P1.2 Document and test the N2 payload board

- Record interfaces, power behavior, expected states, and command/test procedure.
- Define what the payload contributes to the live demo.
- If the payload is not stable enough, define a simulated-data fallback now instead of during demo week.

Deliverable:
- Payload board test note and demo role definition.

SMART target:
- By April 17, 2026, publish the payload-board interface and test checklist.
- By May 8, 2026, complete one bench test and choose between live payload integration and demo fallback.

### P1.3 Develop minimum mission-side software beyond the transport link

- Add one flight-software component or small set of components for demo logic.
- Implement a simple command-triggered flow and one visible result.
- Connect emitted events and telemetry to GDS.

Deliverable:
- Demo controller or mission-state path in F' with visible telemetry and event output.

SMART target:
- By May 1, 2026, the demo controller runs on hardware and completes the simple planned flow three times in a row.

### P1.4 Develop minimum PDU and board-side interfaces

- Decide whether any PDU or board-side interface is truly required before the demo.
- If yes, implement only one demo-critical slice.
- If no, document the deferral and keep the demo focused on RF command/response plus payload result.

Deliverable:
- Either:
  one minimum board-interface software path with one verified command or status signal,
  or a documented decision to defer PDU work until after the demo.

SMART target:
- By April 10, 2026, decide whether PDU work stays in scope.
- By May 8, 2026, either one board-side interface is operational or PDU work is formally deferred.

### P1.5 Prepare demo operations

- Write a presenter script and operator script.
- Define the screen layout and sequence for a 30 to 40 minute live session.
- Define fallback actions if the RF link or payload behavior becomes unreliable.

Deliverable:
- Demo operations package.

SMART target:
- By May 15, 2026, the presenter and operator can execute the planned flow from script.

### P1.6 Run reliability and rehearsal tests

- Repeat command and telemetry cycles many times.
- Test cold boot, reconnect, and recovery cases.
- Run a full-duration rehearsal on the actual hardware stack.

Deliverable:
- Rehearsal record with issues found and resolved.

SMART target:
- By May 23, 2026, complete one full-duration rehearsal and one failure-recovery rehearsal.

### P1.7 Build and verify the SatNOGS comm board path

- Physically assemble the SatNOGS comm board needed for the demo path.
- Define only the software or firmware support required for basic bring-up and demo use.
- Run basic checks for power, communication, and demo-use viability.
- Make an early fallback decision if the board is not stable enough.

Deliverable:
- SatNOGS comm board build status, QA checklist, bring-up result, and fallback decision.

SMART target:
- By April 10, 2026, the board build and bring-up checklist are complete.
- By April 24, 2026, the team records a go/no-go decision.
- By May 8, 2026, either the SatNOGS path is integrated or the small 50-byte radio is locked as the official demo RF path.

## P2 Post-Demo or Lower-Priority Tasks

- Full GPS, IMU, and PDU proxy integration.
- Broader mission-service multiplexing.
- Large architecture cleanup not tied to demo success.
- Additional feature work beyond the frozen demo path.

## Suggested Ownership Areas

- Flight software and F' integration:
  Raspberry Pi app, command/telemetry path, GDS configuration, demo controller, mission states.
- Embedded transport:
  Flight Teensy, ground Teensy, UART framing, RF segmentation, counters, superloop hardening.
- PDU and board interfaces:
  PDU path definition, power/health signals, demo-critical control interfaces.
- Payload and mission proof:
  N2 payload board behavior, test evidence, demo artifact.
- Operations:
  Bring-up operator checklist, smoke test, rehearsal flow, presenter support.

## Lead And Deputy Assignments

Each workstream must have:
- one lead who owns delivery,
- one deputy who shadows the work and can take over next year,
- one knowledge-transfer checkpoint before the end of the semester.

Use this section to assign names during planning.

### Workstream Assignment Matrix

| Workstream | Lead | Deputy | Knowledge Transfer Requirement | Due Date |
| --- | --- | --- | --- | --- |
| RF command and telemetry debugging | TBA | TBA | Deputy can run staged HIL debug procedure alone | April 17, 2026 |
| Flight software demo logic | TBA | TBA | Deputy can explain component topology and demo flow | May 1, 2026 |
| Teensy transport firmware | TBA | TBA | Deputy can build, flash, and debug counters on hardware | May 1, 2026 |
| Payload-board development and test | TBA | TBA | Deputy can run payload bench test and explain demo role | May 8, 2026 |
| SatNOGS comm board | TBA | TBA | Deputy can explain build status, perform bring-up, and execute checklist | April 24, 2026 |
| Operator checklist and rehearsal | TBA | TBA | Deputy can lead bring-up and run the demo checklist | May 15, 2026 |

### Knowledge Transfer Deliverables

- Every lead must produce a short handoff note for the deputy.
- Every deputy must perform at least one independent bring-up, test, or rehearsal in their area.
- No workstream is considered complete unless the deputy can execute the required task without the lead present.

Recommended team shape for a small undergraduate SWE team:
- 1 person may own both RF debugging and Teensy transport firmware.
- 1 person may own both flight software demo logic and operator checklist updates.
- Payload and SatNOGS work should stay separate only if enough people are available.
- If the team is too small, defer PDU work unless it directly enables the demo.

## Immediate Next Actions

1. Approve the frozen demo acceptance criteria in this file.
2. Assign named owners by March 30, 2026 for:
   transport debug, flight software, payload board, SatNOGS comm board, and demo operations.
3. Assign a deputy for each lead at the same time the lead is assigned.
4. Run the staged HIL debug matrix and record exactly where the first failure occurs.
5. Decide by April 10 whether PDU work is actually needed for the demo.
6. Decide the minimum payload and RF hardware slice for the semester demo.
7. Make an early SatNOGS comm board go/no-go call so the fallback radio can be used without last-minute chaos.
8. Do not expand scope until one command and one response work reliably over RF.
