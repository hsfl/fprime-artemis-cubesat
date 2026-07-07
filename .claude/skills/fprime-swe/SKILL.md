---
name: fprime-swe
description: Use when developing F Prime (F') flight software - creating components, ports, topologies, building, testing, cross-compiling for embedded targets like Raspberry Pi, or using fprime-util CLI commands
---

# F Prime (F') Flight Software Development

## This Repo

For all repo-specific workflow — venv path, `generate -f` policy, `Components/` cwd rule, topology wiring, demo-first decision rules — read `AGENTS.md` first. The generic guidance below applies everywhere; AGENTS.md overrides it for this project.

Key repo-specific facts:
- Venv: `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
- Always use `fprime-util generate -f` (not bare `generate`) to avoid stale cache failures
- New components must be created from inside `ArtemisRpiTeensy_N2/Components/`, not the project root
- Active F' project root: `ArtemisRpiTeensy_N2`

## Overview

F' is NASA JPL's open-source flight software framework for embedded systems (CubeSats, Mars Ingenuity). Component-based architecture with code generation tools.

**Environment/bootstrapping rules:**
- Always check if the project is already bootstrapped (look for `lib/fprime/`, `settings.ini`, and `fprime-venv/`).
- If not bootstrapped, run **interactive** `fprime-bootstrap project` once (allow stdin prompts).
- Always create/activate a `.venv` (or `fprime-venv/`) before installing or running tools.
- Always use `python` and `pip` (not `python3`/`pip3`).
- Do not downgrade Python.
- Run `fprime-util generate` before `fprime-util new --component` if a build cache doesn't exist.
- Create new components from inside the `Components/` folder (not project root).

**Activate venv first:** `. .venv/bin/activate` or `. fprime-venv/bin/activate`

## Quick Reference - CLI Commands

| Command | Purpose |
|---------|---------|
| `fprime-bootstrap project` | Create new F' project (once, interactive input required) |
| `fprime-util new --component` | Scaffold component (.fpp, C++, CMake) — run from `Components/` |
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
├── fprime-venv/           # Python venv (activate this!)
├── settings.ini           # Build config (toolchain, paths)
└── CMakeLists.txt
```

## Development Workflow

### 1. Create Component
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
```bash
fprime-util generate && fprime-util build
fprime-util generate --ut && fprime-util build --ut && fprime-util check --ut
```

**If `fprime-util generate` fails because the build cache already exists:**
```bash
fprime-util generate -f
```

### 5. Run with GDS
```bash
fprime-gds  # Opens web UI at http://127.0.0.1:5000
```

**This repo / macOS note:** port 5000 is often taken by Control Center/AirPlay Receiver on macOS. Use the repo launcher `./tools/run_gds_uart.sh` (defaults to GUI port 5050) or pass `--gui-port 5050`.
- **Commanding tab**: Send commands
- **Events tab**: View logs
- **Channels tab**: Monitor telemetry

## Cross-Compilation (Raspberry Pi)

**THIS REPO'S TARGET IS A PI ZERO W (ARMv6).** Do NOT use `aarch64-linux` or standard `arm-hf-linux` for it — both target ARMv7+ and produce binaries that SIGILL on the Pi Zero W. Use the `fprime-cross-compilation` skill, which documents the ARMv6 toolchain and the repo's `tools/docker_cross_compile_pi_zero_w.sh` workflow.

Generic F' cross-compilation (for ARMv7+/ARMv8 targets only):

```bash
export ARM_TOOLS_PATH=/opt/toolchains  # Point to ARM GCC
fprime-util generate aarch64-linux     # 64-bit Pi 3/4/5
fprime-util build aarch64-linux
```

Platforms: `aarch64-linux` (64-bit), `arm-hf-linux` (32-bit ARMv7)

**macOS users**: Use Docker container for ARM cross-compilation.

Transfer binary to Pi, run deployment, connect GDS:
```bash
fprime-gds --ip <pi_address>
```

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
| Forgot to activate venv | `. fprime-venv/bin/activate` |
| Running `fprime-util new --component` from project root | Run it inside `Components/` |
| Generator prompts fail due to EOF (piped input) | Answer all prompts including the final "add to CMake" and "generate impl" questions, or run interactively in a tty | 
| Build cache error before generator | Run `fprime-util generate` first |
| Build cache already exists and `generate` fails | Use `fprime-util generate -f` |
| Partial component folder left after a failed run | Remove the directory and rerun the generator |
| CMake doesn't see new files | Run `fprime-util generate` again |
| Port type mismatch | Only same-type ports can connect |
| Missing port connection | Check topology for unconnected required ports |
| Cross-compile fails on macOS | Use Docker container |

## References

- [F' Documentation](https://fprime.jpl.nasa.gov/)
- [Hello World Tutorial](https://fprime.jpl.nasa.gov/latest/tutorials-hello-world/)
- [Cross-Compilation Setup](https://fprime.jpl.nasa.gov/latest/docs/tutorials/cross-compilation/)
- Use the "fprime-docs-search" skill if you need to figure out how to live search web fetch query the F' Documentation.
