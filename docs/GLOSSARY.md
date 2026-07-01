# Glossary

Acronyms and terms used across this repository, for students and new team members. When a term has a deeper write-up, the linked doc is noted.

## Mission / program

- **Neutron 2 (N2)** — the target spacecraft architecture and subsystem model this project is building toward. See [`SYSTEM_ARCHITECTURE.md`](SYSTEM_ARCHITECTURE.md).
- **Artemis CubeSat** — the prototype hardware kit used to demonstrate the Neutron 2 architecture during the current demo phase. Not the final flight build. See the [Artemis CubeSat Kit](https://sites.google.com/hawaii.edu/artemiscubesatkit).
- **FlatSat** — a spacecraft integrated and tested "flat" on a bench (boards laid out and wired, not in the flight chassis) so it is easy to probe and debug.
- **FSR** — Flight/Functional System Review-style demo milestone the shortened end-to-end story targets.
- **MVP** — Minimum Viable Product; the smallest implementation that supports the live demo story.
- **HIL** — Hardware-In-the-Loop; testing with the real Teensies, radios, and Pi in the loop (as opposed to laptop-only emulation). See [`NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`](NEUTRON2_RF_MVP_DEMO_RUNBOOK.md).
- **D2S2** — orbit / pass-timing simulator ([dawndusk.space](https://dawndusk.space/)) that tells the flight software when the spacecraft is approaching its orbit/contact window, driving Base ↔ Science Collection transitions. See [`SYSTEM_ARCHITECTURE.md` › D2S2](SYSTEM_ARCHITECTURE.md#d2s2-orbit--pass-timing-simulator).

## Subsystems

- **OBC** — On-Board Computer. Here: the Artemis OBC board carrying a Raspberry Pi Zero W + Teensy 4.1.
- **EPS** — Electrical Power System. Battery, power conditioning, and rail control.
- **PDU** — Power Distribution Unit. The board that switches individual power rails on/off. Uses the PDU v2 protocol; see the [PDU ICD](../external/artemis-pdu/PDU_PROTOCOL_ICD.md).
- **PLD** — Payload. The science instrument; here a Neutron 2 payload **simulator** now, the loaned Neutron 2 **development payload board** later.
- **COMMS** — Communications/radio subsystem. RFM23BP for the MVP; SatNOGS-style board later.
- **ADCS** — Attitude Determination and Control System. Simulated via D2S2 for the demo.
- **GPS** — Global Positioning System receiver; position/time source.
- **TCS** — Thermal Control System. Heaters/sensors and battery-heater context.
- **S&M** — Structures & Mechanisms. Chassis and antenna deployment.
- **SOH** — State Of Health. The compact health/status telemetry downlinked during a pass.

## Hardware

- **RFM23BP** — the low-cost, ~50-byte-packet, half-duplex 433 MHz COTS packet radio used for the MVP RF link. Essentially a digital walkie-talkie. See the [RFM23BP constraint](SYSTEM_ARCHITECTURE.md#the-rf-link-constraint-how-fprime-gds-talks-over-a-walkie-talkie).
- **Teensy 4.1** — microcontroller used as the bridge between the Pi UART and the RFM23BP radio (satellite side) and between RF and laptop USB (ground side).
- **Raspberry Pi Zero W** — single-board computer that hosts the F´ flight-software deployment on the satellite.
- **SatNOGS** — open-source satellite ground-station/radio ecosystem; the longer-term in-house comms-board path for real data downlink.
- **COTS** — Commercial Off-The-Shelf; a part bought as-is rather than custom-built.

## Flight software (F´ / this repo)

- **F´ / F Prime / FPP** — the NASA/JPL flight-software framework used here; FPP is its modeling language (`.fpp` files). See [`FPRIME_GROUND_INTERFACES_PRIMER.md`](FPRIME_GROUND_INTERFACES_PRIMER.md).
- **Manager → Service → Adapter (HAL)** — the three-tier component pattern: managers decide what happens, services hold the hardware-independent subsystem contract, adapters are the hardware/protocol glue (the **HAL**, Hardware Abstraction Layer). See [`SYSTEM_ARCHITECTURE.md` › Flight Software Architecture](SYSTEM_ARCHITECTURE.md#flight-software-architecture-manager--service--adapter-hal).
- **Component** — an F´ module with typed ports; the unit of flight software. *Active* = has its own thread/queue; *passive* = runs in a caller's context.
- **Port** — a typed connection point between components (the F´ equivalent of a function-call interface).
- **Topology** — the wiring of component instances and port connections; defined in `Top/topology.fpp`.
- **Command / Event / Telemetry / Parameter** — the four ground-facing interfaces. Command = ground→spacecraft action; Event = timestamped log line; Telemetry (channel) = a periodic measured value; Parameter = a persistent setting. See [`FPRIME_GROUND_INTERFACES_PRIMER.md`](FPRIME_GROUND_INTERFACES_PRIMER.md).
- **Opcode** — the numeric identifier the command dispatcher uses to route a command to its handler.
- **Rate group** — a set of components ticked at a fixed frequency (here 1 / 0.5 / 0.25 Hz). See [`TIME_AND_SCHEDULING.md`](TIME_AND_SCHEDULING.md).
- **C&DH** — Command & Data Handling; the F´ core that dispatches commands and routes events/telemetry (`CdhCore` in the topology).
- **UartChannelMux** — the Pi-side component that multiplexes one physical UART into virtual channels 0/1/2.
- **Adapter** — see Manager → Service → Adapter above; the only tier that changes when hardware is swapped.

## Ground / protocol

- **fprime-gds** — the F´ Ground Data System: the laptop tool for sending commands and viewing events/telemetry over the link.
- **Yamcs** — a mission-control software stack; the longer-term ground presentation target beyond the `fprime-gds` MVP.
- **CCSDS** — the international spacecraft data standard. F´ frames telemetry/commands as CCSDS Space Packets inside Space Data Link transfer frames.
- **TM / TC** — Telemetry (downlink) and Telecommand (uplink) frames.
- **APID** — Application Process Identifier; the CCSDS tag that identifies a packet stream. GDS tracks a per-APID sequence count; gaps show as sequence warnings.
- **Frame / packet / segment** — a CCSDS *frame* (here 128 bytes) carries *packets*; over RF each frame is split into ~49-byte RF *segments* and reassembled. See [Transport Architecture](SYSTEM_ARCHITECTURE.md#transport-architecture-one-uart-three-channels).
- **RPC** — Remote Procedure Call; here, channel-2 request/response between a Pi-side adapter and a board on the satellite Teensy's local bus (e.g. PDU). Consumed on the satellite, not forwarded over RF.
- **CRC** — Cyclic Redundancy Check; the per-frame integrity check. A failed CRC makes GDS print `Checksum validation failed` and drop the frame.
- **ICD** — Interface Control Document; the authoritative wire-format spec for a hardware interface (e.g. the PDU ICD).

## Build / platform

- **WSL2** — Windows Subsystem for Linux; the supported path for F´ development on Windows. See [`STUDENT_WINDOWS_LAPTOP_SETUP.md`](STUDENT_WINDOWS_LAPTOP_SETUP.md).
- **Cross-compile** — building the Pi Zero W (ARMv6 hard-float) binary on a faster host via Docker. See [`CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md`](CROSS_COMPILE_PI_ZERO_W_STUDENT_GUIDE.md).
- **ARMv6 / armv6hf** — the Pi Zero W CPU architecture (hard-float); a common cross-compile landmine because many toolchains default to newer ARM.
- **venv** — the Python virtual environment under `ArtemisRpiTeensy_N2/fprime-venv` that must be activated before any `fprime-util` / `fprime-gds` command.
