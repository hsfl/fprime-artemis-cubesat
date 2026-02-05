# F´ Artemis CubeSat Build-First Implementation Notes

## 1. Purpose

This document is the execution plan for standing up the **generic F´ foundation** for Artemis CubeSat with a strict scope:

- Keep Teensy baremetal for RF23BP + local hardware/sensors
- Run F´ on Raspberry Pi for spacecraft software framework
- Use **one UART channel** between Teensy and RPi
- Prioritize **build/integration success**, not test authoring

This is written for parallel swarm execution.


## 2. Locked Decisions (Do Not Re-open During MVP)

1. **F´ framing stays standard** for F´ traffic.
2. Teensy does not parse F´ command/event/telemetry semantics.
3. Teensy acts as RF/UART transport adapter + local hardware owner.
4. RPi owns F´ deployment, topology, commands/events/channels/params.
5. **Only one UART link** is used between Teensy and RPi.
6. Current sprint goal is build + bring-up; skip unit/integration test writing for now.


## 3. Target Architecture (MVP)

## 3.1 Teensy Baremetal Responsibilities

- RF23BP RX/TX handling
- UART transport to RPi
- Packet relay (RF <-> UART)
- Local sensor read capability retained (not fully integrated in MVP path)
- Local counters and fault stats

## 3.2 RPi F´ Responsibilities

- Host F´ deployment
- Use core services/subtopologies for command/data handling
- Own mission-side components
- Handle MVP ping request/response path through F´ stack

## 3.3 F´ Building Blocks on RPi

- `Svc/Subtopologies/CdhCore`
- `Svc/Subtopologies/ComFprime`
- Custom component(s):
  - `TeensyLinkComponent` (required for MVP)
  - `PingResponderComponent` (required for MVP)
  - service proxies later (GPS/IMU/PDU) after MVP


## 4. One-UART-Channel Strategy

Because only one UART is allowed:

- Use one byte stream between Teensy and RPi
- Carry standard F´ packet stream over that link
- For MVP, **do not add service RPC multiplexing yet**

Post-MVP (optional), if Teensy local services must share same UART:

- Add a small transport multiplexer at link level:
  - channel `FPRIME_STREAM`
  - channel `TEENSY_SERVICE`
- Keep F´ packet framing unchanged inside `FPRIME_STREAM`


## 5. MVP Scope Boundaries

In scope:

- F´ project bootstrap and build
- RPi deployment wiring
- Teensy relay baseline
- End-to-end ping path design and integration tasks

Out of scope for now:

- Full sensor proxy implementation
- PDU proxy implementation
- New unit test suites
- Full flight feature parity with current payload app


## 6. Deliverable Definition

Primary deliverables for this phase:

1. F´ project directory committed and buildable on host
2. Deployment that includes CdhCore + ComFprime + MVP custom components
3. Teensy relay firmware path prepared for RF<->UART forwarding
4. Build/run instructions documented for developers

MVP done (this phase):

- `fprime-util generate` succeeds
- `fprime-util build` succeeds
- deployment binary runs on RPi/host integration environment
- wiring for ping flow exists in code/topology


## 7. Repository Layout Plan

Recommended additions:

- `fprime-artemis/`:
  - F´ project root (new)
  - deployment(s)
  - components
- `docs/`:
  - `swarm_task_board.md` (optional summary index)
  - `uart_link_contract_v1.md` (MVP transport notes)

Teensy code:

- Keep existing Teensy codebase as baseline
- Add relay-focused branch/module changes incrementally


## 8. Parallel Swarm Task Board

Each task below is designed to be assigned independently where possible.

## Task T0 - Architecture Lock + Interface Notes

Owner:

- systems lead

Goal:

- Freeze MVP architecture choices so coding tasks do not diverge.

Inputs:

- this document

Outputs:

- finalized `FPRIME_ARTEMIS_CUBESAT.md`
- `docs/uart_link_contract_v1.md` (brief, no overdesign)

Done when:

- component names, data path, and UART policy are fixed


## Task T1 - F´ Bootstrap Workspace

Owner:

- F´ setup agent

Goal:

- Create clean F´ project scaffold under `fprime-artemis/`.

Commands (reference):

```bash
cd /Users/sozodennis/Developer/epscor-c3m-payload
python -m venv .venv
. .venv/bin/activate
pip install fprime-bootstrap
fprime-bootstrap project
```

Notes:

- Use project-local virtual environment
- Prefer `python`/`pip` command style

Outputs:

- bootstrapped F´ project with working toolchain environment

Done when:

- agent can run `fprime-util --help` from project environment


## Task T2 - Deployment Skeleton + Subtopologies

Owner:

- F´ topology/build agent

Goal:

- Create deployment that wires:
  - `CdhCore`
  - `ComFprime`
  - placeholder MVP custom components

Outputs:

- deployment folder + FPP topology + CMake wiring

Done when:

- `fprime-util generate` completes for deployment


## Task T3 - MVP Custom Components Scaffolding

Owner:

- F´ components agent

Goal:

- Create component definitions and implementation stubs:
  - `TeensyLinkComponent`
  - `PingResponderComponent`

Notes:

- Keep behavior minimal
- Do not attempt full feature completion in this task

Outputs:

- FPP + C++ impl scaffolds compiling in deployment

Done when:

- `fprime-util build` succeeds with both components linked


## Task T4 - Teensy Relay Minimal Path

Owner:

- embedded/Teensy agent

Goal:

- Implement minimal relay behavior for MVP:
  - RF receive -> UART forward
  - UART receive -> RF forward

Constraints:

- one UART channel only
- non-blocking loops preferred
- preserve current RF configuration unless explicitly changed

Outputs:

- Teensy relay code patch set + short bring-up notes

Done when:

- firmware compiles for Teensy target in existing environment


## Task T5 - RPi Link Bring-up

Owner:

- integration agent

Goal:

- Wire deployment runtime config so `TeensyLinkComponent` opens UART and runs.

Outputs:

- runtime settings notes (port, baud, startup behavior)
- minimal log output showing component startup path

Done when:

- deployment starts without component init failures


## Task T6 - Build Reliability Pass

Owner:

- build/release agent

Goal:

- Ensure clean reproducible build steps for the team.

Scope:

- host build required
- cross/native RPi build if environment is available
- no new test development required

Outputs:

- `docs/build_runbook.md` with exact commands

Done when:

- another engineer can follow runbook and reach successful build


## 9. Execution Order and Parallelism

Run first:

- T0, T1

Run in parallel after T1:

- T2, T4

Run after T2:

- T3

Run after T3 + T4:

- T5

Run last:

- T6


## 10. Build-First Acceptance Checklist (No New Tests)

- F´ environment activates cleanly
- deployment graph compiles with custom MVP components
- Teensy firmware builds with relay changes
- documented command sequence exists for rebuild
- no unresolved build/link errors


## 11. Post-MVP Backlog (Not This Sprint)

- `GpsProxyComponent`
- `ImuProxyComponent`
- `PduProxyComponent`
- Single-UART service multiplexing (only if needed)
- Formal test suites and HIL automation


## 12. Risks and Controls

Risk:

- overbuilding protocol before build is stable

Control:

- keep MVP transport minimal and compile-first

Risk:

- mixing service protocol with F´ stream too early on single UART

Control:

- defer service channel multiplexing until MVP link is stable

Risk:

- component sprawl before topology is stable

Control:

- only `TeensyLinkComponent` + `PingResponderComponent` for MVP


## 13. References (Official F´)

- Development practice:
  - https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/development-practice/
- Ground interface:
  - https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/ground-interface/
- Svc Framer:
  - https://fprime.jpl.nasa.gov/latest/docs/Svc/Framer/
- Svc Deframer:
  - https://fprime.jpl.nasa.gov/latest/docs/Svc/Deframer/
- CdhCore subtopology:
  - https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/CdhCore
- ComFprime subtopology:
  - https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/ComFprime/
- Install/bootstrap:
  - https://fprime.jpl.nasa.gov/latest/docs/getting-started/installing-fprime/
- Cross-compilation:
  - https://fprime.jpl.nasa.gov/latest/docs/tutorials/cross-compilation/
