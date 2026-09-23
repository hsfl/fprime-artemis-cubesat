# Artemis CubeSat Kit
## System and Software Design Document

**Version 1.0 | Design baseline for implementation | updated 22 September 2026 (HST)**

University of Hawaiʻi at Mānoa
Hawaiʻi Space Flight Laboratory

### A reusable bus for student missions

Artemis provides the common spacecraft services that a student mission needs: power coordination, sensing, magnetic detumbling, thermal management, command and telemetry, fault recovery, and communications. Students develop the mission and payload software against those services instead of rebuilding the bus.

The selected architecture places mission coordination and bus authority on a Teensy 4.1 running F Prime on Zephyr. A Raspberry Pi Zero W running F Prime on Linux handles one logical payload interface, acquisition, processing, and bulk data storage. Yamcs provides the operational ground interface.

This document defines the target design and the contracts needed to implement it. The full Teensy-primary bus is not yet a verified implementation. Historical demonstrations and framework support are identified separately from required target behavior.

**Technical owner:** Artemis flight software lead
**Reviewers:** bus subsystem leads, mission developers, ground software and integration leads
**Status:** version 1 design baseline; open allocations remain; not flight acceptance

The document is a design reference. Assembly, installation, pin-level wiring, and operator procedures remain in the kit manual, interface control documents, and runbooks.

---PAGE---
# Contents and reading paths

{{TOC}}

### How to use this document

Mission developers should start with Sections 1–5, then read the component contracts, mission example, and extension rules. Bus developers should also read the execution, fault, storage, and interface sections before implementing a component. Reviewers should use the verification matrix and open-item register to distinguish selected behavior from work still requiring evidence.

Component names in this design are proposed design names unless explicitly identified as framework components or historical source names. The diagrams show principal connections; the future FPP model will define every instance and typed port.

**Diagram editing:** each figure has an editable diagrams.net file and SVG in `diagrams/`. The Word figures are embedded previews. Edit the diagram source and replace or regenerate its preview; they are not individually editable Word shapes.

---PAGE---
# 1 Purpose and design basis

### The product boundary

The kit is a reusable 1U spacecraft bus with a mission extension point, not a fixed science mission. The textbook explicitly describes reusing the kit bus while students supply payload interfaces and payload software. Its original requirements also call for approachable software and capacity for varied undergraduate payloads. [S1, PDF pp. 103–118, 1661]

The bus baseline uses the existing Artemis hardware family and RFM23BP radio. A mission must fit its electrical, computational, attitude, storage, and communications envelope. Reuse does not imply that every payload or pointing requirement can be met. A capability outside that envelope requires an explicit bus extension and renewed verification.

### Selected design principles

- Students own mission activities in the Teensy `MissionApp`, Pi payload components, mission configuration, and mission tests. The reusable bus supplies the hosted mode logic and protection contracts.
- Reusable bus managers and drivers normally remain unchanged between missions.
- Teensy is the sole authority for spacecraft mode, protective actions, radio access, PDU commands, and Pi supervision.
- One flight-oriented architecture supports simulation and FlatSat configurations through selected drivers.
- Missing measurements are TBDs with closure criteria, not assumed performance.
- Prefer F Prime services, typed ports, generated interfaces, and native state machines over parallel custom frameworks.

### Evidence categories

| Label | Meaning in this document |
| Selected | Agreed target behavior for implementation and review. |
| Historical | A retained implementation or document; not the new bus baseline. |
| Framework | Capability supplied by the referenced F Prime or platform source. |
| TBD | An allocation or mechanism requiring a named closure activity. |
| Verified | Reserved for a stated test on an identified configuration. |

The historical Pi-led demo and minimal Zephyr bridge are reuse sources, not proof of the target architecture. Source inspection, a successful link, hardware operation, and flight qualification are different evidence levels. [S3, S4]

---PAGE---
# 2 System context

{{FIG:01_system|Figure 1. System context and authority boundaries}}

### Two flight deployments and one payload

`ArtemisCore` runs on Teensy/Zephyr. It owns the spacecraft bus and the student mission coordinator. `ArtemisPayload` runs on Pi/Linux. It receives authorized jobs and returns progress, products, and faults. Each deployment has its own executable, component instances, runtime resources, and generated dictionary.

Payload data/control normally connects to Pi through a supported Linux driver for USB, serial, SPI, I²C, or other device-class OS I/O. Host simulation selects a `SimDriver` at the same manager–driver boundary. The selected Pi supply path is PDU-regulated `BUS_5V` through the OBC's U2 load switch to the Pi; Teensy pin 36 drives U2's `RPI_ENABLE`. The PDU's separate `SW_5V_1` output is not connected to the Pi on the inspected OBC v4.24 schematic. This is a schematic allocation, not a verified as-built power path. Payload power remains an electrically suitable switched path under Teensy authority. A configurable logical power name does not make a voltage, current, connector, or harness compatible. The Payload ICD closes that mapping. [S10, I3, O2]

The PDU retains its own firmware and local electrical behavior. It executes validated low-level requests; it is not the mission controller. Structure, power generation, batteries, and thermal hardware are physical system elements even where they have no F Prime component.

### Operations and independence

Yamcs commands reach Teensy through the ground transport and radio path. The ground Teensy/RFM23BP bridge is the baseline transport reference. Pi loss may remove payload capability, but must not remove essential bus commanding, telemetry, or protective control. The exact power/reset topology needed to meet that requirement remains a hardware closure item. [S3, S5, I1]

---PAGE---
# 3 Core deployment and ownership

{{FIG:02_core_topology|Figure 2. Proposed Teensy core topology}}

### One mission state authority and independent protection

The active `MissionApp` owns the spacecraft mode state machine and mission schedule. It hosts bus-owned `Startup`, `Safe`, `Detumble`, and `Nominal` logic; students add mission activities within its admission rules. The bus supplies reusable guards/actions and regression tests, rather than asking each mission to reimplement Safe behavior. No other component owns spacecraft mode.

`FaultManager` qualifies faults and requests protective actions independently of `MissionApp`. Owning services enforce hard limits independent of mission configuration, but hardware action still depends on the driver and PDU. Protection must complete within an allocated, measured latency; issuing a request is not achieved protection. A missed Safe acknowledgment escalates through the selected fatal/reset path. Sections 10–11 define those limits.

### Functional subtopologies

Group cohesive instances and internal wiring as `CoreServices`, `Mission`, `PowerThermal`, `AttitudeNavigation`, `GroundLink`, and `CompanionLink`. Place the replaceable mission instance at the `Mission` extension boundary. Shared UART, SPI, or I²C controllers have one instantiated driver owner per hardware resource; managers use that owner rather than duplicating bus ownership.

FPP subtopologies are composition inside a deployment, not additional processors or fault-isolation containers. Their configuration includes instance IDs, queues, priorities, and exported topology connections. The design names above are proposed Artemis groups; they are not claims that F Prime ships those exact subtopologies. [S2]

---PAGE---
# 4 Application manager and driver boundaries

{{FIG:05_layers|Figure 3. Software layers and replacement boundaries}}

### Three responsibility layers

Applications coordinate behavior across devices. This layer includes student mission activities and reusable bus policies: `FaultManager`, `AdcsManager`, `ThermalManager`, `DeploymentManager`, and `PiSupervisor`. Their retained names describe subsystem responsibility; they do not make them peripheral-protocol managers. [S2]

Device managers own protocols and device lifecycle: `EpsManager` owns the PDU protocol, sensor managers own sensor register protocols, `GpsManager` owns the receiver protocol, and `RadioManager` owns RFM23BP configuration. Bus policies consume observations or request operations through these device managers.

Drivers own generic UART, SPI, I²C, GPIO or Linux device-class I/O. `UartDriver` is the logical design name; the selected Zephyr UART implementation must meet the bounded I/O requirements before reuse. `EpsManager` alone exchanges PDU frames through that driver. It commands the OBC Pi load switch through a GPIO driver under the same power policy. All heater, coil, deployment and Pi/payload power requests use `EpsManager`; payload outputs that reside on the PDU use its protocol.

On Pi, `PayloadApp` → `PayloadManager` → `LinuxDriver` or `SimDriver` preserves the same boundary. Callbacks carry results upward through typed ports. Simulation substitutes physical I/O while preserving the device protocol; a high-level stub must declare reduced protocol-test coverage.

---PAGE---
# 5 Core component responsibilities

The following catalog defines the target responsibilities. It is intentionally smaller than a class inventory. A component may contain private helpers; split it only when ownership, execution, reuse, or testability warrants another interface.

| Component | Owns | Principal interactions |
| MissionApp | Student activities and schedule; hosts bus-owned mode guards/actions | Requests bus services and payload jobs; consumes results. |
| FaultManager | Fault qualification, severity, dwell, retry policy, and protective requests | Consumes health; requests Safe and bounded protection; checks deadlines. |
| EpsManager | Logical output requests, power allowance checks, PDU protocol and OBC Pi switch control | Uses `UartDriver` for PDU outputs and a GPIO driver for `RPI_ENABLE`; reports commanded/unknown state and faults. |
| ThermalManager | Temperature validity, heater policy, and hard thermal limits | Uses sensor managers; requests heater actuation through `EpsManager`. |
| AdcsManager | Magnetic detumble algorithm, attitude observations, and hard actuator limits | Uses sensor managers; requests coil actuation through `EpsManager`. |
| GpsManager | Position and GPS time observations with validity | Owns receiver protocol over generic I/O; reports to TimeCoordinator and telemetry. |
| TimeCoordinator | Spacecraft time source, validity and correlation | Platform clock, GPS/ground input, companion time distribution. |
| DeploymentManager | Separation/inhibit sequence and deployment status | Approved sensing and PDU actuation through `EpsManager`. |
| PiSupervisor | Pi availability, power lifecycle and bounded recovery | EpsManager, CompanionLink, mission job cancellation. |
| CompanionLink | Versioned peer transport and session state | Authorized job/status/time traffic and bounded product traffic. |
| CommsManager | Radio service policy and transfer grants | Configures framework ComQueue traffic service; uses RadioManager. |
| RadioManager | RFM23BP device protocol and readiness | SPI/GPIO drivers and framework communications ports. |

Framework command, event, telemetry, parameter, time, scheduling, and health services support these components. `Svc.Health` responsiveness checks complement device health and fault policy; a responding component does not prove that its sensor, power rail, or RF path works. [S2]

---PAGE---
# 6 Payload deployment

{{FIG:03_payload_topology|Figure 4. Proposed Pi payload topology}}

### Student extension and common services

`PayloadApp` executes authorized activities. `PayloadManager` owns device protocol and lifecycle over `LinuxDriver` or `SimDriver`. Acquisition uses asynchronous manager–driver requests and callbacks; blocking waits require a driver-owned task or bounded transactions.

`ProcessingWorker` processes manager-owned buffers and reports only through `PayloadManager`; it has no driver or command path. A synchronous atomic cancel flag bounds cancellation when polled. Uninterruptible work must time out visibly and reconcile on return; timeout does not prove it stopped.

Reusable `ProductStore` owns file admission, metadata, completeness and reclamation. `ProductTransfer` prepares bounded transfers under Teensy grants. Reuse framework file/data-product services where their contracts fit.

One logical payload may contain several devices but has one mission interface and power lifecycle. Independently powered payloads are a future extension. Pi remains optional for bus survival.

### Authority and failures

Pi may reject requests, report failures or request bus service. It cannot change spacecraft mode, switch PDU outputs or seize the radio. Direct Pi access is for bench/maintenance.

Restart creates a new peer session; stale completions cannot satisfy new jobs. Resume only after bus authorization and device-state reconciliation. Process restart alone does not establish payload health.

---PAGE---
# 7 Logical component contracts

These are design contracts, not final FPP declarations. The owning implementation will define exact types, ports, commands, events, and telemetry while preserving these semantics. Packet bytes and electrical limits belong in the ICDs.

| Contract | Request and result | Key rule |
| Mode contract | Safe request/reason and acknowledgment; current mode; activity admit/revoke; health ping | Bus-owned `MissionAppBus` FPP interface and mode logic are preserved by each mission. |
| Bus activity | Activity ID, required conditions, deadline; admit/reject/revoke | Mandatory bus protection cannot be omitted by mission configuration. |
| Payload power | Named output/lifecycle request; applied state or unknown/error | Check mode, inhibits and allowance; verify actual supported readback. |
| Payload job | Session and job ID, operation, bounded settings; progress and terminal result | Acceptance is distinct from completion; cancellation has an outcome. |
| Health observation | Value, units, source status, timestamp/age | Missing or stale data is not a healthy default. |
| Product | Product/job ID, length, type, integrity and completeness | File existence alone does not mean a complete product. |
| Transfer | Product ID, bounded chunk/offset, grant and receipt | Transmission completion does not authorize reclamation. |
| Time correlation | Source, epoch, validity and bounded uncertainty | Realtime and uptime must not silently share one meaning. |

### Common failure behavior

Every nontrivial asynchronous operation has an identity, timeout, busy/admission behavior, and explicit completion or failure. Define who owns buffers and return them on success, failure, and cancellation. Reject unsupported operations rather than silently approximating them.

For ground operations, use generated command responses for dispatch/handler outcomes and events/telemetry for the longer lifecycle. Distinguish accepted, running, completed, rejected, and failed where applicable. The lifecycle need not become a new universal framework; use existing F Prime mechanisms with consistent names and IDs.

Requested state and achieved state are separate. A PDU GPIO response, for example, may describe applied control pins; it is not automatically a measured output voltage or current. [S5]

---PAGE---
# 8 Execution and framework services

### Scheduling without blocking the bus

Active components have a thread for queued work. Passive components run in the caller's context; guarded handlers serialize access. Queued components need an explicit dispatch context. Synchronous bus transactions consume caller time and require bounded timeouts. Names do not determine execution type. [S2]

### Ports, interrupts, and rate groups

Use synchronous/guarded get ports for short reads of available state. Device transactions, storage and processing use asynchronous requests and callbacks carrying activity identity and result. Keep port calls typed and bounded.

An ISR captures/clears a condition and hands off bounded data through a proven ISR-safe ring or queue. It must not use mutex-backed F Prime queues, block, allocate, log or parse protocols. Artemis calls ports in task context. Overflow drops bounded input and increments a task-visible counter. Harden the referenced Zephyr UART ISR loop and overrun logging before reuse. [S2, S4]

Rate groups perform short, ordered work. Passive children run in the caller; active children receive queued work. Defer long I/O and processing to driver tasks or manager/worker callbacks. Payload workloads must preserve command, fault and watchdog deadlines. [S2]

| Execution class | Design allocation | Closure evidence |
| Bus protection and scheduling | Periodic bounded work, short manager handlers | Worst-case response under sensor/link/payload load. |
| Serial/radio service | Dedicated receive work and bounded queues | Overflow handling, malformed traffic and turnaround tests. |
| Processing | Low-priority Pi worker controlled only by PayloadManager; synchronous atomic cancel flag | Poll interval, timeout, callback outcome and buffer return. |
| Storage and products | Low-priority bounded operations | Full media, failed writes and restart reconciliation. |

### Reuse and resource ownership

Evaluate `Svc.CdhCore` and `Svc.ComCcsds` as framework compositions; verify their contents and configuration against the selected version. Include file/data-product services only where used and affordable. Each deployment has local services and IDs; the Pi dictionary is not automatically merged with the Teensy dictionary.

Allocate queues, stacks and buffers at initialization where practical. Handle pool exhaustion explicitly; avoid unbounded queues or recurring heap growth. Share hardware buses through one driver owner and an explicit transaction policy. Set periods, priorities, queue capacities and watchdog limits from measured workloads, recorded in the resource allocation—not copied from a minimal example.

Pin framework/toolchain/platform versions. On the single-core Pi, verify effective thread priorities and required Linux permissions: POSIX task creation can fall back without requested priority. V07 must pass under the actual service configuration. [S2, S4, O1]

---PAGE---
# 9 Bus modes and transitions

{{FIG:04_modes|Figure 5. Basic bus state machine}}

`MissionApp` hosts the bus-owned native FPP mode definition and guard/action code. Students add activities without replacing Safe entry, recovery or inhibits. Safe signals receive priority over queued mission work; overflow fails closed through the selected fatal path. Initial entry actions defer output-port calls until connections exist. [S2]

| Mode | Purpose and exit |
| Startup | Establish safe defaults, peer/reset status and required observations. Proceed only when inhibits and readiness permit. |
| Safe | Apply the approved protective configuration, retain available command/health service. Eligible recovery returns through Startup after clear dwell. |
| Detumble | Run magnetic rate reduction within power/thermal limits. Enter Nominal after valid measured rates remain below threshold for the required dwell. |
| Nominal | Provide bus services and admit mission activities whose declared conditions pass. |

Faults or an accepted ground Safe command request Safe from any mode. Invalid sensing or detumble timeout requests coil coast and Safe. Safe is reported separately from achieved hardware protection. A missed mode acknowledgment escalates; ground clearance releases a latch only when current recovery guards pass. Thresholds and deadlines remain allocations.

Deployment inhibits are independent conditions, not a second competing mode machine. The flight configuration prevents RF transmission and deployment/actuator actions as allocated by the mission's approved constraints. Simulation supplies explicit synthetic inputs; it does not silently disable flight guards.

---PAGE---
# 10 Fault handling and reboot behavior

### Protect the bus and explain the outcome

Faults are qualified using validity, age, persistence, and severity. A single anomalous observation may require confirmation; a critical indication may require immediate action. The severity map and thresholds are configuration-controlled. `FaultManager` owns cross-subsystem fault qualification and protective requests. Local services also enforce hard device limits. Protective requests do not wait for mission-state acknowledgment, but completion is bounded by the verified driver/PDU path.

| Condition | Selected response | Recovery evidence |
| Critical EPS/thermal fault | Revoke affected activity; request affected outputs off within the protective deadline; request `MissionApp` Safe | Valid clear conditions for dwell; achieved configuration. |
| Detumble timeout or invalid sensor | `FaultManager` requests `AdcsManager`/`EpsManager` coast the coils within the protective deadline; request `MissionApp` Safe and emit an event | Sensor validity and approved retry/admission conditions. |
| Pi loss during job | Cancel authorization; mark job interrupted; bounded Pi recovery | New peer session and fresh readiness; bus stays commandable. |
| Ordinary ground-contact gap | Continue authorized healthy activities | No automatic Safe solely for a normal pass gap. |
| Persistent link malfunction | Separate bounded link recovery policy | Link readiness and command/telemetry proof. |
| Full payload storage | Reclaim eligible confirmed products or reject acquisition | Capacity reservation succeeds before new work. |
| Repeated/serious fault | Record a reset-surviving latch; request Safe and ground clearance | Ground action plus current safety guards. |

### Reset is a state transition

Initialize transient work as cancelled or unknown after reset; never infer that an old actuator request completed. Reconcile observations before resuming. Report reset cause when the platform supplies it, current software version, and unavailable history honestly.

RAM event history may be lost, but protection must survive reset. A minimal reset-surviving record holds the protective latch, consecutive unplanned-reset count, deployment phase and available fatal/reset identity. A latched fault, excessive reset count or invalid record routes Startup to Safe with activities inhibited until ground reconciliation. Clear the count only after a qualified healthy dwell. Select and verify storage under O3 before autonomous flight; this does not require SD logging. [O3, O4]

Hardware protection and watchdog reset remain distinct from software fault policy. Radiation-related software symptoms can be mitigated, but this document does not make commercial hardware radiation hardened or assert flight qualification. The watchdog design and its evidence boundary are defined on the next page.

---PAGE---
# 10.1 Watchdog and liveness

### Teensy has two recovery paths

A late critical-component ping raises a FATAL event. Select and verify the complete path: `Svc.Health` → `EventManager` → Artemis-configured Zephyr `FatalHandler` → platform reboot. Override the CdhCore fatal-handler binding explicitly; neither the POSIX abort nor baremetal spin implementation is an assumed Zephyr reset mechanism. Capture available fatal/reset identity using the minimal reset record, without making reset depend on successful logging. [S2, S4, S9]

Separately, connect `Health.WdogStroke` to a new Artemis hardware-watchdog driver. This detects a stalled Health rate group or fatal handler. In the inspected F Prime source, the stroke occurs at the end of every Health run, even with monitoring disabled; it is not conditional on successful ping replies. HTH-007 in the Health SDD differs from this implementation. The target uses explicit fatal recovery plus rate-group watchdog recovery, not an assumed health gate. [S9]

Boot must configure the hardware watchdog and safe outputs. Allocate ping deadlines and watchdog timeouts from measured startup/run margins. Restrict, validate and telemeter changes to `HLTH_ENABLE`, `HLTH_PING_ENABLE` and `HLTH_CHNG_PING`; flight operation must not silently disable critical monitoring. A historical `CONFIG_WATCHDOG=y` build proves neither driver integration nor reset behavior.

### PDU watchdogs depend on application progress

The target explicitly arms the SAME51 internal WDT and services the external WDI path only while bounded protocol/safety work advances. Current `tasks.c` clears WDT and toggles PB30 every 500 ms independently of `APP_Tasks`; an application stall can leave both feeds running. `initialization.c` has `WDT_ENABLE=CLEAR` and no `WDT_Enable` call in `SYS_Initialize`. Thus the internal-reset fallback described for `SOFTWARE_RESET` is not established. Verify both watchdogs on the populated board. [S5, S9]

PDU reboot clears output enables and charger enable in source. Reset is not automatically a safe power configuration; Section 11 defines detection and reconciliation. No watchdog path is claimed as hardware-proven.

### Pi process and board recovery

A Pi-local service supervisor restarts the F Prime process after exit or loss of its health-run heartbeat. Bind Pi FATAL handling to process termination and restrict health-disable commands. The Teensy `PiSupervisor` independently detects peer heartbeat loss, revokes jobs and attempts bounded reconciliation; if the Pi service cannot recover, it requests an authorized power cycle through `EpsManager`. A dead process cannot receive its own restart command. Pi hardware WDT remains optional.

V16 distinguishes software/fatal reboot, MCU watchdog reset, PDU reset and Pi process restart. Inject each separately; verify identity, safe restart, reboot-loop lockout and continued bus commanding through Pi loss.

---PAGE---
# 11 Power thermal and deployment services

### EPS and Pi supervision

`EpsManager` is the logical power-service owner and owns the PDU protocol. It receives named requests and checks bus permissions and available observations. PDU output requests become protocol transactions through `UartDriver`; Pi power requests drive the OBC U2 load-switch enable through a GPIO driver. The GPIO path does not bypass the same authorization, status and fault policy. All power and PDU actuation requests from `ThermalManager`, `AdcsManager`, `DeploymentManager`, `PiSupervisor`, or `MissionApp` go through `EpsManager`; no other manager contends independently for the PDU UART or Pi enable pin. [S5, S10, I1]

Pi may be continuously powered in Nominal or enabled for scheduled activities. The configured policy must include boot/readiness time, shutdown allowance, and bounded recovery attempts. Normal shutdown requests allow Pi to finish file operations before power removal; critical protection can preempt that grace period and records the resulting interrupted state.

On the inspected PDU v2.2 and OBC v4.24 schematics, the PDU's hardware-enabled 5 V regulator feeds `BUS_5V` through PC/104 H1 pins 2.25/2.26 when battery power is available. OBC U2 switches that bus to `RPI_5V_PWR_IN` at the Pi header when Teensy pin 36 asserts `RPI_ENABLE`. U2 is a protected load switch, not the 5 V regulator. A PDU `SW_5V_1` command does not independently turn this Pi feed on or off. A successful GPIO write is only a commanded enable state; Pi supply voltage, current and readiness need separate observation. The populated board, rail margin under Pi/payload load, off-state discharge/backfeed, and reset defaults remain to be verified. [S10, O2]

The exact PDU output used for mission payload power remains an electrical allocation. A complete power tree must prove Teensy and the essential command path remain powered during Pi/payload shedding and PDU MCU reset. An all-off software command is not inherently a safe spacecraft configuration.

### Thermal service

`ThermalManager` reports temperatures with calibration and validity, evaluates configured limits, and requests available heater control through `EpsManager`. Stale sensing blocks actions that depend on it. Heater cutoff, allowable temperatures and duty limits require hardware/thermal closure; the existence of a heater description does not prove its current command interface. [S1, S6, O5]

### Deployment service

`DeploymentManager` owns separation evidence, inhibit timing, antenna-deployment sequencing and achieved status. It must prevent unintended repeat actuation after a reboot or uncertain completion. Burn-wire requests go through `EpsManager` and use the PDU's supported interlocks and duration limits; software does not substitute for required electrical inhibits.

The textbook's Artemis CDH example specifies a 30-minute deployment countdown. Treat this as historical intent; the flight configuration must use approved mission/launch constraints and define which actions are inhibited, what starts the timer, and reset behavior. These operational values remain mission allocations. [S1, PDF p.1455; S7; I4]

---PAGE---
# 11.1 PDU protection and reset contract

### Protective latency is an end to end requirement

`EpsManager` is active and owns serialized PDU transactions. A priority protective input is serviced ahead of queued normal requests, with a bounded queue and explicit overflow escalation. Priority does not interrupt a handler already executing, shorten wire time, or override a PDU `BUSY` response. Allocate and test the whole detection-to-achieved-state deadline. No unverified immediate OFF capability is assumed.

Pi switch requests use a bounded local GPIO service path; a pending PDU transaction or `BUSY` response must not prevent a required Pi power action. Verify the GPIO action deadline separately from PDU protective latency.

The current 9600-baud protocol rejects `SET_OUTPUT_STATE`, including OFF, while any timed operation is active. Burns/power cycles can also block coil coast. A same-coil coast can cancel a timed coil pulse when that is the active operation. There is no single all-output Safe opcode. Duration-zero coil drive is latched and lacks a PDU-side host-silence timeout. [S5]

The flight target requires a PDU protective abort/Safe operation available during timed operations and a bounded host-loss actuator policy. Its defined safe output set must preserve essential power; “all off” is not the definition. Close this change through I1/O11 before admitting affected flight activities. Until then, configuration must exclude operations whose worst-case protection latency or latched outputs violate the mission limits. Host priority alone does not close this gap.

The PDU simulator reproduces `BUSY`, timed operations, lost replies and reset defaults. V04 covers protection during a burn, power cycle and timed coil pulse; V16 covers Teensy loss while a coil is latched. Avoid uncontrolled retry storms and never report successful protection from a request alone.

### Detect reset and rebuild achieved state

Current `APP_Initialize` calls `disableAllGPIOs`: PDU-switched outputs, burn/coil controls and the LTC4012 charger enable are cleared. The schematic Pi feed uses the PDU's regulated bus and a separate OBC switch, so clearing PDU `SW_5V_1` does not by itself remove Pi power. Whether essential Teensy/radio supplies or the Pi bus are affected by a PDU reset depends on the populated power tree. A Teensy reset can change `RPI_ENABLE`; its boot-pin default and effect on Pi power also require O2 verification. [S5, S10]

`EpsManager` detects PDU restart through uptime discontinuity and fresh reset/status information, marks its prior PDU applied-state cache unknown, and emits a fault. It re-applies only currently authorized PDU outputs, including the approved charger state, through normal checks and readback. Pi enable state is reconciled separately from PDU output state and Pi heartbeat/readiness; a PDU reset alone is not evidence of Pi power loss. Never replay an old burn or acquisition request. `PiSupervisor` treats confirmed loss of its supply as an unplanned power loss; jobs and products reconcile in a new session.

Acceptance includes a PDU watchdog reset during acquisition, charger-state recovery, and proof that essential commanding survives the selected reset/power topology. If that topology cannot meet the requirement, change the hardware allocation before claiming bus independence.

---PAGE---
# 12 Attitude navigation and time

### A standard magnetic detumble service

The target bus provides attitude sensing and magnetic detumbling. `AdcsManager` consumes calibrated magnetic-field/rate observations and requests bounded coil actions. The textbook describes B-dot control using a three-axis magnetometer and torque coils, and includes Artemis coil experiments. This establishes intended capability, not a verified flight controller. [S1, PDF pp.1385–1386,1399–1401]

Implementation must establish sensor axes, coil geometry/polarity, magnetic interference, sample/actuation timing, and available magnetic moment. Coordinate measurements with coil activity so self-generated fields do not invalidate feedback. Power, thermal and actuator limits constrain the controller. Precision pointing is not a baseline promise.

The current PDU interface offers four coil outputs but does not prove three independent spatial axes. Paired outputs share current-setting controls, and only one timed coil operation is tracked at once. The selected control allocation must respect these limits or explicitly change and reverify the manager/driver and PDU design. [S5]

### Navigation and spacecraft time

`GpsManager` supplies position and time with status and age. Receiver support in the intended flight environment remains a verification item. `TimeCoordinator` selects valid GPS-derived time first, ground-set time as fallback, then bounded holdover on the local clock. Absolute schedules wait once uncertainty exceeds the allowed limit. Relative delays use a monotonic clock within the current boot.

ZephyrTime's realtime/uptime wrapper is useful plumbing, not a complete validity or synchronization policy. `TimeCoordinator` provides the deployment time service with explicit `Fw.Time` bases: `TB_PROC_TIME` for boot-relative time and `TB_SC_TIME` for valid spacecraft correlation. Record correlation epoch and uncertainty separately. Pi uses Teensy correlation; its wall clock is not a competing mission authority. Product metadata retains time-source and validity information. A time correction must not cause a job to execute twice; the mission tracks activity identity and execution status. [S4, O6]

---PAGE---
# 13 Core and payload communications

### One explicit interprocessor contract

The Core–Payload ICD defines a versioned link between two independent deployments. Preserve reusable framing mechanics where useful, but do not assume the historical Pi-authority channel map is the correct target architecture. The historical mux is a reference implementation, not a target bandwidth allocation. [S3, I2]

| Message family | Required meaning |
| Peer session | Protocol/software version, boot/session identity, heartbeat and readiness. |
| Job control | Job identity, authorization, bounded settings, deadline, cancellation. |
| Job status | Accepted/running/terminal result, reason and associated identity. |
| Bus request | Named service and parameters; permission plus achieved/failed outcome. |
| Time | Source, epoch/correlation, validity and age/uncertainty. |
| Product traffic | Product identity, bounded lengths/offsets, integrity and transfer grants. |

The link must reject malformed lengths and unsupported versions, bound parser memory and time, and reject stale results from an earlier session. Duplicate requests need an explicit idempotency rule, especially for acquisition and power operations. Receiving a frame is not proof that a job ran.

### Transport versus operation semantics

F Prime GenericHub can serialize logical connections and offers version-dependent command forwarding. Evaluate it with the selected framework and transport. It does not replace the application decisions about job authorization, restart, timeout, cancellation or product ownership. Do not send process-local pointers across a processor boundary. [S2]

Control/status service must retain bounded latency during bulk payload traffic. Use bounded chunks and backpressure; test saturation and Pi restart during transfer. Keep packet layouts, CRC choices, channel IDs, baud/framing and retry numbers in the ICD, with one implementation-owned source of constants.

---PAGE---
# 14 Ground operations and radio service

### Yamcs is the operational baseline

The kit ground system presents a standard bus view: modes, power, temperature, attitude validity, link health, Pi status and faults. Students extend a separate mission/payload view. Unknown, stale or unavailable measurements are displayed as such, not as healthy zeros.

Operational commands enter through Teensy. Bus handlers enforce permissions and validate parameters. Acquisition and other actuating payload operations use the authorized job contract only. Raw forwarded Pi commands are limited to a declared non-actuating allowlist and valid peer session; all others are visibly rejected on Teensy. Device arguments are also checked on Pi. Direct Pi control is bench/maintenance only.

Allocate non-overlapping deployment opcodes/IDs. `CmdSplitter` routes by opcode, not permission; it is not an admission gate. The referenced GenericHub/CCSDS origin path drops forwarded responses. Artemis must report accepted/rejected, progress and terminal outcomes explicitly through its job/status path, tested in Yamcs. Use generated dictionaries; F Prime GDS remains a development/test tool. [S2, S8, I5]

### Traffic and link budgeting

Reserve service for command reception/results, faults and essential bus telemetry. Bulk payload traffic uses the remaining allocation and pauses when necessary. Bound and rate-limit fault reporting too: an event storm must not starve commanding. Define measured latency and capacity targets rather than an arbitrary percentage split.

RF link margin asks whether the signal can be received at the selected rate and conditions. A data budget asks whether products and housekeeping fit available contact time, storage and power. Account for framing, retransmissions, half-duplex turnarounds and interrupted contacts. The textbook motivates both budgets; it does not prove this RFM23BP configuration closes a flight link. [S1, PDF pp.866–870,952,1540–1548]

Mission products remain retained until ground confirms identity and integrity. A successful transmission event is only an intermediate result. Separate ICDs define the receiver, repair and receipt behavior; it must work through the chosen Yamcs operational workflow. [I5]

---PAGE---
# 15 Runtime data and storage

### RAM first on Teensy

The core operates without a mandatory SD logging dependency. Use bounded RAM for current observations, component state, queues and recent events. Optional Teensy SD logging or forwarding to Pi may retain history. If RAM history is lost after reset, ground must not be shown a fabricated complete record.

Firmware storage is mandatory. The reset-surviving record is required before autonomous flight; its storage backend and recoverable update mechanism remain TBD. This distinction avoids requiring a full logging filesystem merely to run the bus, while keeping restart behavior explicit. The textbook identifies SD mass storage at kit level but does not allocate a Teensy logging policy. [S1, PDF pp.1497,1501; O3,O4]

| Data owner | Lifetime and recovery |
| Teensy bus managers | RAM observations become unknown/stale after reset until refreshed. |
| MissionApp | Transient attempts cancel after reset; configured restart policy requires new readiness. |
| MissionApp / DeploymentManager | Protective latch, unplanned-reset count and deployment phase survive reset; invalid record forces Safe. |
| Pi ProductStore | Reconcile files/catalog; distinguish partial, complete and corrupt products. |
| Ground archive | Retain received telemetry, events and product receipts with identity/time validity. |

### Product storage policy

Reserve space before acquisition. Pi stores bounded product data and metadata: product/job identity, payload format version, byte length, acquisition context, time validity, completeness and integrity. Preserve raw bytes when a processing step produces a derivative; the mission defines which products are required for delivery.

Reclaim ground-confirmed products first. If no eligible space remains, block new acquisitions and report the condition. A mission may explicitly select another retention policy, but silent deletion of unconfirmed products is not the default. Partial writes remain partial; failed storage must not generate a successful product-ready result.

Routine scheduled Pi power-off should synchronize durable payload state first. Emergency power removal remains possible, so file/catalog recovery is a tested behavior rather than an assumption.

---PAGE---
# 16 Parameters and software updates

### Use the framework configuration path

FPP defines component parameters and defaults. Generated commands and parameter services support runtime adjustment; generated dictionaries describe the interface for ground tools. Owning components validate ranges and applicability. A dictionary is not a schedule executor or a substitute for a persistent storage backend. [S2]

Prefer native parameter services over a separate mission-configuration framework. Package compatible parameter defaults, mission components, payload schema and dictionaries with each release. Apply changes in allowed states, expose accepted/rejected outcomes, and report active versions. Persistence on Teensy is only claimed after its selected backend passes reset/power-loss tests.

### Recoverable update target

Both Pi software and Teensy firmware are intended to support in-flight update. The textbook supports this intent, but the historical Zephyr bridge does not implement a proven updater or boot rollback. [S1, PDF pp.1553,1614; S4]

The target sequence is: receive a bounded image/package, validate identity/compatibility/integrity, stage it, await explicit ground activation, boot or start the candidate, confirm health, then commit. A failed candidate must recover to a known-good version. Upload completion alone cannot authorize activation. Preserve a usable command path and report version/update state after recovery.

Pi application update and Teensy firmware update need separate mechanisms. Flash layout, boot selection, power-loss behavior, image space and recovery entry remain O4. Do not implement a destructive replacement path until the recovery mechanism is demonstrated. No particular bootloader or dual-slot layout is assumed in this design.

### Initial security boundary

Command authentication, replay protection and authenticated update packages are deferred from the initial baseline. CRC/hash integrity checks detect corruption; they do not authenticate an operator or update source. F Prime v4.3 SDLS interfaces are a future integration path; their default clear-text implementation provides no security. Adding real protection requires a compatible implementation and ground workflow. [S2]

---PAGE---
# 17 Reference mission data flow

{{FIG:06_reference_sequence|Figure 6. Simulated payload mission sequence}}

### Demonstrate the whole path

The reference mission uses a replaceable active `MissionApp` on Teensy and a `SimDriver` behind the Pi payload manager. It demonstrates scheduling, readiness checks, logical payload power control, acquisition, product storage, transfer and ground confirmation. In a no-hardware configuration, the PDU `UartDriver` is also simulated; a successful simulation does not prove physical switching. Any high-level stub must state its reduced coverage.

A deterministic simulated product contains a job identifier and predictable sample data so tests can compare expected bytes and integrity. Its format is an example, not the required format for future payloads. Commands, progress, completion and failure remain visible through Yamcs.

Acceptance requires the matching product to arrive intact, with the correct job and time context, while bus command/health service remains responsive. Ground confirmation makes the product eligible for reclamation; it does not require immediate deletion.

Students replace the mission schedule/logic and payload implementation while retaining power, time, ADCS, communications and other bus contracts. The demonstration therefore teaches the extension boundary through observable behavior, rather than by exposing every internal framework connection.

---PAGE---
# 18 Interrupted activities and failure example

### Default interruption policy

Mark an interrupted acquisition incomplete, emit an event, and require fresh authorization before a new attempt. Missions may explicitly choose restart, resume, skip, or reschedule behavior, including scripted activities. Resume is allowed only when the payload supports it and the current job/product identity can be reconciled. Safety checks always apply.

The bus and Pi track separate facts: Teensy knows whether an activity remains authorized; Pi knows the device operation and product state it has actually observed. A timeout does not prove the payload stopped. Cancellation therefore includes device reconciliation or a protective power action when permitted, not simply clearing a software flag.

### Required reference failure

| Step | Observable behavior |
| Inject payload timeout | Simulator accepts a job but withholds completion. |
| Detect deadline expiry | Owning manager reports failed/timed-out result with job identity. |
| Revoke and cancel | Teensy revokes the activity; Pi aborts/reconciles and releases buffers. |
| Preserve evidence | Mark any partial product; issue event and terminal status for ground. |
| Continue bus service | Mode/health/command path remains available unless a bus-critical fault exists. |
| Admit a new attempt | Fresh readiness and selected mission policy determine the next job. |

Also test Pi reset after acceptance and before completion. Establish a new peer session and reject any late result from the previous session. A new job must not accidentally reuse an old product's completion state.

With RAM-only core logging, an immediate Teensy reset can erase pre-reset detail. Report the available reset cause and unknown history. Optional persistent logs may improve diagnosis but do not change the base bus's documented limitation.

---PAGE---
# 19 Mission extension and repository organization

### A predictable place for each change

The proposed repository organization follows responsibility. It is a target layout, not a claim that these directories already exist in the flight repository.

| Proposed area | Contents and ownership |
| Contracts | Shared FPP types/ports and interface versions; bus maintainers. |
| Bus | Reusable managers, drivers and component tests. |
| Platforms | Zephyr/Linux bindings, shared bus ownership and platform configuration. |
| Subtopologies | Cohesive reusable instance groups and internal connections. |
| Deployments/Core | Teensy top-level instances, mission binding and configuration. |
| Deployments/Payload | Pi top-level instances and payload binding. |
| Missions/Reference | Student-facing MissionApp, simulated payload and expected products. |
| Ground | Yamcs dictionaries/configuration, bus display and payload extensions. |
| Tests | Contract, simulation, integration and regression evidence. |

A student mission delivers its components, configuration, Payload ICD, tests, and compatible ground definitions. It passes shared bus regression tests before an integrated release. Subsystem managers/drivers change only for a justified bus capability or defect, reviewed independently from the mission customization.

Each mission imports the bus-owned `MissionAppBus` FPP interface and hosts the supplied mode definition and guards/actions. Mission substates or private activity logic may extend Nominal behavior, but cannot bypass Safe, recovery, inhibits or admission. The shared mode/fault/limit/watchdog regression suite runs against every mission binding; student tests add coverage.

Use stable logical interfaces and explicit version compatibility. FPP catches many type/wiring errors, but it does not prove that two versions interpret a field, deadline or completion state identically. Record the Core–Payload ICD version and dictionary/build identities in the release manifest.

Keep the top-level topology small enough to show major groups and extension connections. Do not split every single component into a subtopology. Group repeated, cohesive wiring with clear configuration. Maintain one source for interface constants; generated files are outputs, not independent specifications.

The SDD, editable figures, FPP model and component documentation must evolve together. A diagram expresses intent; the built topology and tests establish implementation.

---PAGE---
# 20 Simulation FlatSat and flight configurations

### One architecture with deliberate substitutions

Simulation exercises the same logical contracts as hardware. Select drivers and platform bindings explicitly; do not disguise simulated observations as real sensor data. The flight configuration excludes bench-only bypasses and direct Pi control paths.

| Configuration | Purpose | Evidence boundary |
| Host simulation | Deterministic mission, faults, product and time tests | Logic/contract behavior without device proof. |
| Core target test | Teensy build/boot, clocks, queues and memory | Target execution; not integrated RF/EPS proof. |
| FlatSat | Identified boards/harness with selected real drivers | End-to-end hardware behavior for that configuration. |
| Flight configuration | Approved hardware, parameters, inhibits and release | Requires its own acceptance evidence. |

The simulator must support invalid/stale measurements, missing peers, failed acquisition, time loss, storage exhaustion and delayed results. A success-only stub cannot verify fault policy. The reference mission's deterministic product makes source-to-ground comparison reproducible.

### Resource and compatibility gates

Measure CPU load, stack high-water marks, queue/buffer use, command latency, time error, product throughput and storage growth under the worst supported combination. Establish margins before admitting a new mission. A passing idle test does not establish loaded behavior.

Use the selected F Prime/Zephyr/Linux/toolchain pins and record the build manifest. Historical Pi demo behavior is a regression reference where interfaces are retained, not authority for the new processor allocation. Historical memory/link results for a minimal Zephyr bridge cannot close the full core resource budget. [S3,S4]

Document hardware tests with board revision, power source, wiring, software versions, stimuli, observed outcome and anomalies. USB-powered tests do not prove the full EPS power tree. Keep unit tests, integration tests, HIL and flight qualification labeled separately.

---PAGE---
# 21 Design verification matrix

These design-level checks guide implementation acceptance. They are not completed test records or substitutes for mission-controlled requirements. Allocate measurable limits through the open-item register.

| ID | Design requirement | Required evidence |
| V01 | Bus remains commandable without Pi | Pi absent/reset under load; bus command, health and protection observed. |
| V02 | Students replace mission/payload without bypassing bus | Reference and second mission binding pass unchanged bus contract/regression suite. |
| V03 | MissionApp preserves bus-owned modes and interface | Safe request/ack deadline, queue overflow, ground clearance, dwell and unchanged mode regression on each mission. |
| V04 | PDU protection completes within its allocation | Serialized requests; protective abort during burn, power cycle and coil pulse; BUSY, timeout, overflow and achieved-state readback. |
| V05 | Magnetic detumble meets allocated envelope | Sensor/coil calibration, simulation, magnetic testbed and target timing evidence. |
| V06 | Time validity gates absolute schedules | GPS/ground loss, holdover expiry, clock correction and no duplicate execution. |
| V07 | Bulk traffic cannot starve bus service | Saturation test with bounded command/cancel/fault latency. |
| V08 | Old peer results cannot complete new jobs | Pi reset, duplicate/out-of-order frames and stale completion injection. |
| V09 | Products are complete and confirmed before reclamation | Known bytes, interrupted transfer, bad integrity, missing receipt and full storage. |
| V10 | Interrupted activity is observable and recoverable | Timeout example; terminal event/status, resource release and fresh authorization. |
| V11 | No SD log dependency for core operation | Missing/corrupt optional media; bus boot/control remains functional. |
| V12 | Updates recover from failed candidates | Corrupt package rejection, explicit activation, failed boot and known-good recovery. |
| V13 | Ground admission and outcomes match both deployments | Reject actuating raw Pi commands and invalid sessions; observe job acceptance, progress, terminal status and receipts in Yamcs. |
| V14 | Deployment cannot repeat unintentionally after reset | Reset at each sequence phase; inhibits, timing and physical status reconciled. |
| V15 | Mission release fits available resources | Measured memory/CPU/power/data/time budgets and approved margin. |
| V16 | Distinct recovery paths restore bounded safe operation | Component FATAL reboot; Health-task WDT reset; each PDU watchdog; latched-coil host loss; Pi process restart and OBC-switch power cycle; persistent latch/reset-loop and charger recovery. |

For each test, retain expected result, actual result, configuration and failure evidence. Close hardware-dependent items on the identified target hardware. No V-item is marked passed by this design.

---PAGE---
# 22 Open allocations and interface register

The architectural choices are selected; these items remain implementation or hardware closure work. Owners identify accountable roles, not assigned individuals.

| ID | Open allocation | Owner and closure |
| O1 | Platform pins, core budget and ISR/UART hardening | FSW: build/boot full topology; prove ISR-safe handoff, bounded UART service and resources. |
| O2 | Board/harness, payload rail and essential power tree | Electrical: confirm as-built PDU `BUS_5V` to OBC U2 to Pi and Teensy pin 36 enable; measure Pi voltage/current, startup, off-state/backfeed and brownout under load; prove essential power during Pi/payload shedding and PDU reset. |
| O3 | Required reset-surviving record without SD logs | FSW: choose atomic record storage; latch/reset-count/deployment recovery and corruption tests. |
| O4 | Teensy/Pi update staging and boot recovery | Platform: image/partition feasibility, interrupted activation and rollback tests. |
| O5 | Sensor/heater interfaces and thresholds | Thermal/EPS: populated devices, calibration, actuation and safe limits. |
| O6 | Time accuracy, source validity and holdover | FSW/navigation: drift/sync measurements and scheduling tests. |
| O7 | Detumble geometry, gains and achievable envelope | ADCS: coil/sensor characterization, bounded control and testbed evidence. |
| O8 | Command latency, contact capacity and RF margin | Communications: radio/link/data budget plus saturated end-to-end tests. |
| O9 | Deployment signal, inhibits and reset policy | Systems/integration: approved constraints and phase-by-phase reset proof. |
| O10 | Fatal, watchdog and process recovery bindings | FSW/platform: pin fatal handlers; integrate WDT drivers/PDU health gating/Pi supervisor; verify reset identity and recovery. |
| O11 | PDU abort and host-loss protective behavior | PDU/EPS: update I1/firmware; timed-operation abort, essential safe outputs, bounded coil host-loss test. |

### Interface control documents

| ID | Owning document | Status |
| I1 | PDU Protocol ICD in `artemis-cubesat-pdu-firmware` | Existing v2; protective abort/host-loss extension and board integration pending. |
| I2 | Artemis Core–Payload ICD | TBD document: framing, sessions, requests/results, time and bulk flow. |
| I3 | Mission Payload ICD | Required per mission: connector, power, data, states and products. |
| I4 | Artemis Deployment and Hardware Interface ICD | TBD document: as-built sensing, pins, inhibits and actuator mapping. |
| I5 | Artemis Ground and Product Transfer ICD | TBD document: Yamcs/radio framing, IDs, receipt/repair and product metadata. |

The SDD owns responsibilities and behavior; ICDs own exact wire/electrical definitions. Link new controlled documents here when created. Do not copy conflicting pin tables or packet constants into the SDD.

---PAGE---
# 23 Sources and document maintenance

### Engineering references

[S1] Frances Zhu, A Guide to CubeSat Mission and Bus Design, cloned August 2023 edition. Local PDF and searchable sidecar in the workspace root. PDF page references in this document are viewer pages, not printed pages. Original kit requirements and reusable-bus intent are historical design sources, not current acceptance records.

[S2] NASA/JPL F Prime v4.3.0 local study checkout, `fprime-material/fprime`, commit `7d8f579f159d2f7c2d4984d92828575e37f87fa6`. Read the Application–Manager–Driver, Manager/Worker, common-port, health-checking, rate-group, and hub patterns; state machines; component/port/topology guidance; parameters; GenericHub; and ComCcsdsSdls SDDs. February 2026 FSW Design workshop: `fprime-material/fprime-course-materials/Flight_Software_Workshop-February_2026/06_FSW_Design.pdf`.

[S3] Historical Artemis source, commit `2e30e0f46fc60054bf2eb7cc6917c647b849e2c9`: `docs/SYSTEM_ARCHITECTURE.md`, Pi deployment topology, transport constants and selected components. This is the Pi-led reference; it does not implement this target allocation.

[S4] Historical `FprimeZephyrSatellite` at S3; local `fprime-material/fprime-zephyr`. Minimal bridge proof boundary, ZephyrTime and platform APIs. Filesystem enablement and successful linking do not establish persistent storage, complete bus operation or recoverable updating.

[S5] `artemis-cubesat-pdu-firmware/PDU_PROTOCOL_ICD.md`, `src/pdu_protocol_v2.h`, `src/pdu_packet.c`, and `docs/current_pdu_architecture.md`. Current source owns PDU wire behavior; source execution must be tested on the exact board.

[S6] Workspace `docs/context/` guides and retained April 2026 Artemis manual. Hardware references span revisions and contain incomplete sections; exact-board evidence overrides generic descriptions.

[S7] Live textbook CDH requirements: https://pressbooks-dev.oer.hawaii.edu/epet302/chapter/6-3/ . This preserves the clipped deployment timer table. Live avionics: https://pressbooks-dev.oer.hawaii.edu/epet302/chapter/6-4/ . Live content differs from the 2023 PDF.

[S8] `fprime-material/fprime-yamcs`, commit `5aafc72996c0cb1d290ceba11d46cf826cc831ec`, README and bridge source. Dictionary conversion, event handling and communications integration reference; Artemis end-to-end proof remains required.

[S9] Watchdog source review: `artemis-cubesat-pdu-firmware/src/config/default/tasks.c:65–97,154–174`, `src/config/default/initialization.c:64–69`, and `src/pdu_packet.c:664`; historical `fprime-artemis-cubesat/FprimeZephyrSatellite/fprime/build-fprime-automatic-zephyr/zephyr/.config` (`CONFIG_WATCHDOG=y`); and `fprime-material/fprime/Svc/Health/docs/sdd.md` (HTH-007 differs from unconditional stroke in `HealthComponentImpl.cpp:131–134`). These are source/build observations only; they do not establish health-gated integration, HIL, or flight qualification.

[S10] Power-path schematic review, 22 September 2026 HST: `artemis-hardware/Eagle Designs/PDU/PDUv2.2/PDU_v2.2.sch` and `artemis-hardware/Eagle Designs/OBC/OBC_V4.24/OBCv4.24.sch`. PDU `BUS_5V` reaches OBC U2 through H1 pins 2.25/2.26; Teensy pin 36 enables U2's Pi output. PDU `SW_5V_1` reaches H1 pin 2.13 but not the Pi on OBC v4.24. The manual assigns it to the Pi; as-built and loaded operation remain unverified.

### Maintain this design

Version 1.0 incorporates a source-checked independent Opus review and the 22 September Pi power allocation correction. It retains MissionApp mode ownership, bus-owned mode contracts, recovery and protection requirements. This is a design baseline, not completed flight software. Keep the SDD, editable figures, FPP model and ICDs synchronized. Review dispositions are in `SDD/reviews/`; interview decisions are in `docs/ARTEMIS_SDD_INTERVIEW_DECISIONS.md`.
