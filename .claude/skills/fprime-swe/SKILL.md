---
name: fprime-swe
description: Use when developing or documenting F Prime (F') flight software in this checkout, including components, ports, topologies, builds, tests, and embedded targets.
---

# F Prime (F') Flight Software Development

## This Checkout

Consult this checkout's `AGENTS.md` for the task-relevant architecture, workflow, and validation instructions. Follow the owning project's toolchain and runbooks for the selected deployment.

For the Pi-hosted `ArtemisRpiTeensy_N2` deployment, the project root is `ArtemisRpiTeensy_N2/` and components are under its `Components/` directory. Its documented virtual environment is `ArtemisRpiTeensy_N2/fprime-venv/`; check that it is valid for this checkout before reuse. Consult the checkout's instructions for the right build profile and generator options.

## Overview

F' is NASA JPL's open-source flight software framework for embedded systems (CubeSats, Mars Ingenuity). Component-based architecture with code generation tools.

**Project and environment:**
- Locate the selected F′ project and deployment roots, then follow their instructions and existing toolchain. Projects can use different directory layouts and environment names.
- For a read-only question, inspect the available source and documentation without bootstrapping or creating an environment. For build or test work, reuse the project's configured environment and invoke its chosen Python interpreter and tooling.
- Bootstrap only when creating a new project is in scope. For an existing project, use its documented setup when the authorized work requires it.
- If generating a component, follow the selected project's F′ version and component layout. Generate the build cache first when the applicable generator requires it.

## Quick Reference - CLI Commands

| Command | Purpose |
|---------|---------|
| `fprime-bootstrap project` | Create new F' project (once, interactive input required) |
| `fprime-util new --component` | Scaffold a component in the selected project's component directory |
| `fprime-util new --port` | Create custom port type |
| `fprime-util new --deployment` | Create deployment |
| `fprime-util impl` | Generate C++ implementation stubs |
| `fprime-util impl --ut` | Generate unit test stubs |
| `fprime-util generate` | Run CMake configuration |
| `fprime-util build` | Compile (add `-j8` for parallel) |
| `fprime-util build --ut` | Build unit tests |
| `fprime-util check --ut` | Run unit tests |
| `fprime-gds` | Launch Ground Data System web UI |

## Core Concepts

### Components
Modular units containing state/logic. Define in `.fpp` files:
- **Active**: Own thread for async port handling
- **Passive**: Called from others' contexts

Components declare: ports, commands, events, telemetry channels, parameters.

### Ports
Typed interfaces for component-to-component communication:
- **Output port** → calls → **Input port** (same type required)
- **Sync**: Direct function call on caller's thread
- **Async**: Queued, handled by callee's thread
- **Guarded**: Mutex-protected for thread safety

### Topology
Assembly of component instances and port connections = one deployment executable.

### Data Constructs
- **Commands**: Operations triggered by ground/sequencer (opcode, mnemonic, args)
- **Events**: Log messages with severity (DEBUG, WARNING_HI, FATAL)
- **Telemetry Channels**: Periodic state data for monitoring
- **Parameters**: Persistent config values (stored via PrmDb)

## Example Project Structure

```
my-satellite/
├── MySatellite/           # Components, ports, deployment
│   ├── Components/
│   ├── Ports/
│   └── Deployment/
├── lib/fprime/            # Framework submodule
├── fprime-venv/           # Example Python environment
├── settings.ini           # Build config (toolchain, paths)
└── CMakeLists.txt
```

## Development Workflow

### 1. Create Component
For a project using this layout, create the component from its component directory:
```bash
cd Components
fprime-util new --component
# Prompts: name, namespace, kind, cmd/tlm/evt/param support, then:
# - Add component to Components/CMakeLists.txt? (yes/no)
# - Generate implementation files? (yes/no)
```

### 2. Edit FPP Definition
Component `.fpp` declares ports, commands, events, channels, parameters.

### 3. Generate & Implement
```bash
fprime-util impl              # Creates ComponentImpl.hpp/cpp stubs
```

Implement handlers in `.cpp`:
- `handle_Cmd_<NAME>()` for commands
- Port handler functions for input ports

Use auto-generated helpers:
```cpp
log_ACTIVITY_HI_EventName(arg1, arg2);  // Emit event
tlmWrite_ChannelName(value);            // Update telemetry
paramGet_ParamName();                   // Get parameter
```

### 4. Build & Test

Use the selected deployment's documented build profile and test commands. For the Pi-hosted deployment in this checkout, consult `AGENTS.md` and the relevant build or test runbook before regenerating a cache or building. Read-only work does not require these commands.

### 5. Run with GDS
```bash
fprime-gds  # Opens web UI at http://127.0.0.1:5000
```

For current N2 Yamcs operations, use `docs/operations/QUICKSTART.md` and `docs/development/YAMCS_LOCAL_TESTING.md`. The GDS/RFM23BP UART procedure is historical comparison material. Port 5000 can conflict with macOS services.
- **Commanding tab**: Send commands
- **Events tab**: View logs
- **Channels tab**: Monitor telemetry

## Cross-Compilation (Raspberry Pi)

Choose the target toolchain for the device's CPU architecture, ABI, and operating system. `aarch64-linux` below is an example for a compatible 64-bit target. A 32-bit `arm-hf-linux` toolchain is not automatically compatible with every 32-bit ARM CPU. Pi Zero W requires an ARMv6-compatible hard-float toolchain; follow the selected project's documented toolchain and verify the binary's attributes. Consult the `fprime-cross-compilation` skill and project runbook for the applicable build and ground connection.

```bash
export ARM_TOOLS_PATH=/opt/toolchains  # Point to ARM GCC
fprime-util generate aarch64-linux     # 64-bit Pi
fprime-util build aarch64-linux
```

For the Pi Zero W deployment in this checkout, use the project-local `fprime-cross-compilation` skill and its linked runbook for the ARMv6 toolchain and current workflow. Other deployments may target a different processor and toolchain.

## Framework Components to Reuse

Don't reinvent - use built-in `Svc`/`Drv` components:
- `CmdDispatcher`: Route commands to components
- `CmdSequencer`: Execute command sequences from files
- `EventManager`: Aggregate and forward events
- `TlmChan`: Collect and downlink telemetry
- `PrmDb`: Persistent parameter storage
- `Health`: Ping/watchdog monitoring
- `FileDownlink`/`FileUplink`: File transfer
- `Time`: Timekeeping

## Topology Assembly

1. **FPP topology file**: Declare instances and connections
   ```
   instance comm = Comm()
   instance radio = Radio()
   connections { comm.uplinkOut -> radio.uplinkIn }
   ```

2. **Topology.cpp**: Instantiate objects, call `connect()`, initialize:
   - `component.init(...)` for each
   - Register commands with CmdDispatcher
   - Add to Health ping list
   - `component.start()` for active components

## Common Mistakes

| Mistake | Fix |
|---------|-----|
| Project tooling is unavailable | Use the selected project's documented environment and deployment root |
| Component generated in the wrong location | Follow the selected project's component layout |
| Generator prompts fail due to EOF (piped input) | Answer all prompts including the final "add to CMake" and "generate impl" questions, or run interactively in a tty | 
| Build cache error before generator | Run `fprime-util generate` first |
| Build cache already exists and `generate` fails | Follow the selected project's instructions for inspecting or regenerating the cache |
| Partial component folder left after a failed run | Inspect it, preserve pre-existing work, and follow the project's recovery guidance |
| CMake doesn't see new files | Run `fprime-util generate` again |
| Port type mismatch | Only same-type ports can connect |
| Missing port connection | Check topology for unconnected required ports |
| Cross-compile fails on macOS | Check the selected project's documented container or cross-toolchain setup |

## References

- [F' Documentation](https://fprime.jpl.nasa.gov/)
- [Hello World Tutorial](https://fprime.jpl.nasa.gov/latest/tutorials-hello-world/)
- [Cross-Compilation Setup](https://fprime.jpl.nasa.gov/latest/docs/tutorials/cross-compilation/)
- For framework lookup, use the project-local `fprime-docs-search` skill when available or search the official documentation for the project's F′ revision.
