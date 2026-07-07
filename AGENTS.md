# AGENTS.md
This file provides guidance to coding agents when working with code in this repository.
IMPORTANT: Prefer retrieval-led reasoning over pre-training-led reasoning for any F Prime tasks

<!>DOCS
[F Prime Local Docs]|root: ./ArtemisRpiTeensy_N2/lib/fprime/docs
|policy:{Use these repo-local docs before web docs; they match the checked-out F Prime submodule version used by this project. Use `rg` under `ArtemisRpiTeensy_N2/lib/fprime/docs` to find specifics.}
|entry:{index:index.md,install:INSTALL.md,manual:user-manual/index.md,tutorials:tutorials/index.md,howto:how-to/index.md,reference:reference/index.md,doxygen:doxygen/mainpage.md}
|overview:{intro:user-manual/overview/01-full-intro.md,arch:user-manual/overview/02-fprime-architecture.md,port-comp-top:user-manual/overview/03-port-comp-top.md,cmd-evt-chn:user-manual/overview/04-cmd-evt-chn-prm.md,types:user-manual/overview/05-enum-arr-ser.md,dev-practice:user-manual/overview/development-practice.md,proj-dep:user-manual/overview/proj-dep.md,source:user-manual/overview/source-tree.md,unit-test:user-manual/overview/unit-testing.md,gds-intro:user-manual/overview/gds-introduction.md}
|howto:{index:how-to/index.md,framing:how-to/custom-framing.md,state-machines:how-to/define-state-machines.md,channels:how-to/derive-channels-on-ground.md,driver:how-to/develop-device-driver.md,libs:how-to/develop-fprime-libraries.md,gds-plugin:how-to/develop-gds-plugins.md,subtopo:how-to/develop-subtopologies.md,ext-libs:how-to/integrate-external-libraries.md,port:how-to/porting-guide.md,tdd:how-to/test-driven-development.md}
|build:{intro:user-manual/build-system/01-cmake-intro.md,api:user-manual/build-system/cmake-api.md,custom:user-manual/build-system/cmake-customization.md,impl:user-manual/build-system/cmake-implementations.md,platforms:user-manual/build-system/cmake-platforms.md,targets:user-manual/build-system/cmake-targets.md,toolchains:user-manual/build-system/cmake-toolchains.md,uts:user-manual/build-system/cmake-uts.md,settings:user-manual/build-system/settings.md,topology:user-manual/framework/building-topology.md}
|framework:{assert:user-manual/framework/assert.md,autocoded:user-manual/framework/autocoded-functions.md,baremetal:user-manual/framework/baremetal-multicore.md,config:user-manual/framework/configuring-fprime.md,data-products:user-manual/framework/data-products.md,dynamic-mem:user-manual/framework/dynamic-memory.md,ground-if:user-manual/framework/ground-interface.md,state-machines:user-manual/framework/state-machines.md,platforms:user-manual/framework/supported-platforms.md}
|gds:{cli:user-manual/gds/gds-cli.md,dashboards:user-manual/gds/gds-custom-dashboards.md,dash-ref:user-manual/gds/gds-dashboard-reference.md,dev:user-manual/gds/gds-dev-guide.md,test-api:user-manual/gds/gds-test-api-guide.md,seqgen:user-manual/gds/seqgen.md}
|design:{app-man-drv:user-manual/design-patterns/app-man-drv.md,ports:user-manual/design-patterns/common-port-patterns.md,health:user-manual/design-patterns/health-checking.md,hub:user-manual/design-patterns/hub-pattern.md,manager-worker:user-manual/design-patterns/manager-worker.md,rate-group:user-manual/design-patterns/rate-group.md,subtopo:user-manual/design-patterns/subtopologies.md}
|reference:{comm-adapter:reference/communication-adapter-interface.md,fpp-json-dict:reference/fpp-json-dict.md,translations:reference/fprime-translations.md,nomenclature:reference/nomenclature.md,numerical-types:reference/numerical-types.md,gds-plugins:reference/gds-plugins}
|component-sdds:{root:./ArtemisRpiTeensy_N2/lib/fprime,locations:Svc/**/docs/*.md plus Fw/**/docs/*.md plus Drv/**/docs/*.md plus Os/**/docs/*.md plus Utils/**/docs/*.md,search:`rg --files ArtemisRpiTeensy_N2/lib/fprime | rg '/docs/.*\\.md$'`}
</!>

## Agent Skills

Project skills are shared through `.agents/skills/` for Codex-style agents and `.claude/skills/` for Claude Code. Keep these skill folders symlinked so both agent surfaces use the same skill files. Codex-style agents should consult `.agents/skills/` directly; Claude Code can auto-load from `.claude/skills/`.

| Skill | File | Use when... |
|-------|------|-------------|
| `fprime-swe` | [`.agents/skills/fprime-swe/SKILL.md`](.agents/skills/fprime-swe/SKILL.md) | Developing F' components, ports, topologies, running builds, or using fprime-util |
| `fprime-docs-search` | [`.agents/skills/fprime-docs-search/SKILL.md`](.agents/skills/fprime-docs-search/SKILL.md) | Looking up F' framework docs — check repo-local docs at `ArtemisRpiTeensy_N2/lib/fprime/docs` first |
| `fprime-cross-compilation` | [`.agents/skills/fprime-cross-compilation/SKILL.md`](.agents/skills/fprime-cross-compilation/SKILL.md) | Cross-compiling for ARM targets; Pi Zero W ARMv6 landmine documented here |
| `fprime-fpp-authoring` | [`.agents/skills/fprime-fpp-authoring/SKILL.md`](.agents/skills/fprime-fpp-authoring/SKILL.md) | Writing/editing `.fpp` files, wiring a component into the topology, base IDs, or diagnosing FPP autocoder errors |
| `fprime-testing` | [`.agents/skills/fprime-testing/SKILL.md`](.agents/skills/fprime-testing/SKILL.md) | Writing or running component unit tests, `fprime-util check`, GDS integration tests, or `tools/validate_local.sh` |
| `fprime-local-emulation` | [`.agents/skills/fprime-local-emulation/SKILL.md`](.agents/skills/fprime-local-emulation/SKILL.md) | Running or debugging the no-hardware laptop demo: local emulation, `local-demo` topology profile, simulated payload, payload viewer |
| `fprime-hil-testing` | [`.agents/skills/fprime-hil-testing/SKILL.md`](.agents/skills/fprime-hil-testing/SKILL.md) | Guiding live hardware-in-the-loop bring-up: Teensy staging, serial enumeration, Pi service checks, GDS launch, RF smoke tests |
| `teensy-firmware` | [`.agents/skills/teensy-firmware/SKILL.md`](.agents/skills/teensy-firmware/SKILL.md) | Building/uploading satellite or ground Teensy firmware, upload-ID targeting, link protocol and transport-constants regeneration |
| `student-git-handoff` | [`.agents/skills/student-git-handoff/SKILL.md`](.agents/skills/student-git-handoff/SKILL.md) | Helping non-technical students use GitHub feature branches, commits, pushes, and pull requests with Software Dev Lead-approved base branches |

## Student Platform Policy

- All new software choices, tools, scripts, dependencies, and workflows must support student use on macOS first, then Windows.
- F Prime development on Windows means WSL unless an official native-Windows path is documented for the specific tool. Do not present native PowerShell/CMD F Prime builds as the default student path.
- Prefer cross-platform tools with clear macOS and Windows installation paths. Avoid Linux-only assumptions unless the workflow is explicitly target-hardware-only or includes a documented macOS/Windows path through Docker, WSL, or a VM.
- When adding student-facing commands, include macOS examples first and Windows notes second when the commands differ.
- For non-technical testing users, prefer browser/Python-standard-library tools that run natively on Windows. Require WSL only for F Prime build/developer workflows, not for simple ground-side viewers, payload review, or demo-data inspection.

## F' Agent Usage Guide

Use this project with the `fprime-swe` skill and follow these steps exactly.

## Current Project Quick Start (Read First)

- Before making assumptions about the repo state, run:
  ```bash
  git status --short --branch
  git submodule status --recursive
  ```
  Treat the active branch and local uncommitted files as the current working context. Do not assume the checkout is on `main`, `neutron_2`, `neutron2-develop`, or any Codex feature branch without verifying.
- Read `docs/SYSTEM_ARCHITECTURE.md` first.
  - This is the required system-level crosswalk between the Neutron 2 target architecture and the Artemis-based prototype used for the current demo.
  - Do not continue with subsystem or architecture work until this file has been read.
- Read `README.md` for the top-level architecture and current status.
- Read `docs/agents_notes.md` for latest implementation details and pending TODO items.
- For subsystem/component architecture details, point agents to:
  - `docs/SYSTEM_ARCHITECTURE.md`
  - `docs/archive/OPTIMAL_FPRIME_COMPONENT_TOPOLOGY_PLAN.md`
  - `docs/STUDENT_COMPONENT_STARTERS.md`
  Keep detailed architecture in those docs rather than duplicating it here.
- Treat the shortened FlatSat FSR end-to-end demo as the current target mission narrative:
  1. boot into `Base Mode`
  2. determine mock ground-contact timing with `D2S2` inputs
  3. downlink live `SOH` telemetry for display to judges
  4. send a command that schedules data collection after a short delay such as `10` seconds
  5. run data collection using real or simulated payload data
  6. downlink science data
  7. review/analyze science data on the ground PC with `fprime-gds` for the MVP demo; treat `Yamcs` as the longer-term end-goal ground stack
- Active F' project root is:
  - `ArtemisRpiTeensy_N2`
- Active baremetal Teensy workspace is:
  - `ArtemisTeensy_N2_Baremetal`
- Active ground-station Teensy workspace is:
  - `GDS_Teensy`

## Quick Start
1. Activate the venv before any F' command:
   ```bash
   . ArtemisRpiTeensy_N2/fprime-venv/bin/activate
   ```
2. Configure and build from the active F' project root:
   ```bash
   cd ArtemisRpiTeensy_N2
   fprime-util generate -f
   fprime-util build
   ```

## Creating Components
- Always run from the `Components/` folder:
  ```bash
  cd ArtemisRpiTeensy_N2/Components
  fprime-util new --component
  ```
- The generator is interactive and asks additional questions at the end:
  - Add the component to `Components/CMakeLists.txt`? (answer yes)
  - Generate implementation files? (answer yes)
- If the generator fails partway, delete the partial component directory and rerun.

## Creating Deployments
- Run from the project root:
  ```bash
  cd ArtemisRpiTeensy_N2
  fprime-util new --deployment
  ```
- When prompted, add the deployment to `project.cmake`.

## Wiring a Basic Ping Component (Health)
- Add `pingIn`/`pingOut` ports in the component FPP (type `Svc.Ping`).
- Implement `pingIn_handler` to return the key via `pingOut_out(0, key);`.
- Add instance in `Top/instances.fpp` (active instance for active components).
- Add instance in `Top/topology.fpp` and wire health connections.
- Add ping entries in `Top/*TopologyDefs.hpp` for the new instance.

## Build
From the project root:
```bash
fprime-util generate -f
fprime-util build
```

## RPi Native Build (Minimal)
- Use manual native build + run instructions:
  - `docs/RPI_BUILD.md`
- Build policy:
  - build on the Raspberry Pi target and run the locally built binary.

## Run `fprime-gds` over UART (RPi/Operator Side)

Preferred launcher (repo-maintained defaults):
macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/run_gds_uart.sh --port "$PORT"
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/run_gds_uart.sh --port "$PORT"
```

Script defaults:
- UART device: auto-detected when exactly one supported serial device is present;
  otherwise pass `--port <device>`
- UART baud: `115200`
- GUI port: `5050`
- dictionary: auto-detected under `build-artifacts` when not supplied

Useful overrides:
```bash
./tools/run_gds_uart.sh --port "$PORT" --baud 115200
./tools/run_gds_uart.sh --gui-port 5050
./tools/run_gds_uart.sh --dictionary build-artifacts/pi-zero-w-armv6hf/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
./tools/run_gds_uart.sh --dry-run
```

Notes:
- On macOS, port `5000` may already be occupied by Control Center/AirPlay Receiver. Use non-5000 GUI ports (default script port is `5050`).
- If using raw CLI instead of script, pass UART args explicitly:
  - `fprime-gds -n --communication-selection uart --uart-device "$PORT" --uart-baud 115200 --framing-selection space-packet-space-data-link`

## Common Pitfalls
- Running generators without a build cache.
- Running `fprime-util new --component` from the project root instead of `Components/`.
- Not answering the final generator prompts (CMake + impl generation), which leaves partial directories.
- Forgetting to add deployments to `project.cmake`.
- Building features that do not directly improve the live end-to-end demo path.
- Assuming real payload integration is mandatory for the demo when a documented simulated-data fallback is acceptable.

## Teensy Baremetal Agent Usage Guide

Use this section when working in the Teensy bridge workspace:
- `ArtemisTeensy_N2_Baremetal`

### Build (Arduino CLI)
Run from the baremetal project root:
macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

### Upload (Arduino CLI)
macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

### Teensy Source of Truth
- Main sketch:
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/satellite_teensy.ino`
- Relay/link modules:
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/relay_uart_rf.*`
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/rf23_driver.*`
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_protocol.hpp`
  - `ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/src/link_counters.hpp`

### Teensy Common Pitfalls
- Running `arduino-cli` from outside `ArtemisTeensy_N2_Baremetal` (wrong config path).
- Editing generated build cache under `ArtemisTeensy_N2_Baremetal/build/` instead of source files.
- Changing UART settings without updating both firmware and UART contract docs.

## Ground Teensy (`GDS_Teensy`) Agent Usage Guide

Use this section when working in the ground bridge workspace:
- `GDS_Teensy`

### Build (Arduino CLI)
Run from `GDS_Teensy`:
macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
./tools/arduino-cli/build.sh
```

### Upload (Arduino CLI)
Run from `GDS_Teensy`:
macOS:
```bash
cd ~/Developer/fprime-artemis-cubesat/GDS_Teensy
PORT="$(ls /dev/cu.usbmodem* | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```

Windows WSL2:
```bash
cd ~/fprime-artemis-cubesat/GDS_Teensy
PORT="$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n 1)"
./tools/arduino-cli/upload.sh "$PORT"
```
- `upload.sh` expects artifacts from `build.sh` in `build/arduino-cli`.

### Ground Teensy Source of Truth
- Main sketch:
  - `GDS_Teensy/firmware/gds_teensy/gds_teensy.ino`
- Relay/link modules:
  - `GDS_Teensy/firmware/gds_teensy/src/relay_uart_rf.*`
  - `GDS_Teensy/firmware/gds_teensy/src/rf23_driver.*`
  - `GDS_Teensy/firmware/gds_teensy/src/link_protocol.hpp`
  - `GDS_Teensy/firmware/gds_teensy/src/link_counters.hpp`

### Ground Teensy Common Pitfalls
- Uploading without compiling first can fail with “Compiled sketch not found”.
- Stale `build/arduino-cli` cache can cause link errors; clean with:
  - `arduino-cli compile --clean ...` or remove `build/arduino-cli`.
- On macOS, first upload may fail while `teensy.app` starts; retry once it is open.
- Running `arduino-cli` outside `GDS_Teensy` can pick up wrong config paths.

## Biggest Lessons / SWE Flow (Neutron 2)

### Lessons Learned
- Keep `ArtemisRpiTeensy_N2/lib/fprime` as a root-managed Git submodule; avoid nested standalone repos inside the workspace.
- For external subsystems, submodule whole implementation repos under `external/` only when they own real firmware, ICDs, bench tools, or hardware protocol code. Do not make constants-only repos the primary source of truth.
- Current external/reference repos:
  - `external/artemis-pdu/` is the PDU firmware/ICD/bench-tooling reference and current EPS/PDU implementation context.
  - `external/epscorc3m/` is the more current full Artemis CubeSat baremetal demo reference; it documents practical integration footguns, but it is not the target F Prime architecture.
  - `external/artemis-cubesat-examples/` is legacy Artemis CubeSat sample code. Use it for subsystem interface examples only; do not copy its Teensy-main-computer architecture into this F Prime project, where the Raspberry Pi hosts the F Prime deployment.
  - `external/payload-neutron-simulation/` is the current local simulated-payload source for no-HIL/demo work.
- Future submodule priority: `external/satnogs-radio/` once the SatNOGS repo/dev board is real, and `external/payload/` only once the real payload-board repo exists. Do not add ADCS/GPS/IMU/thermal submodules until they have standalone firmware/tooling repos.
- If using `fprime-util new --deployment` for validation, register it in `project.cmake` during the prompt so build targets exist.
- In this repo, prefer `fprime-util generate -f` to avoid stale build-cache failures.
- For deployment smoke validation, direct CMake target build is reliable:
  - `cmake --build build-fprime-automatic-native --target <DeploymentName>`
- Remove temporary smoke deployments after validation unless explicitly requested to keep them.

### Recommended Development Flow
1. Check active branch/worktree and submodule state:
   - `git status --short --branch`
   - `git submodule status --recursive`
2. Read `README.md`, `docs/SYSTEM_ARCHITECTURE.md`, and `docs/agents_notes.md` for current architecture/state.
3. Activate F' venv:
   - `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
4. Validate F' build:
   - `cd ArtemisRpiTeensy_N2`
   - `fprime-util generate -f`
   - `fprime-util build`
5. Validate Teensy compile:
   - `cd ../ArtemisTeensy_N2_Baremetal`
   - `./tools/arduino-cli/build.sh`
6. Re-run both validations after topology/driver changes.
7. Before commit/push:
   - verify submodule SHA is intentional (`git submodule status --recursive`)
   - verify no build/cache artifacts are staged (`git status --short`)
   - verify remote branch naming is push-compatible (avoid `dev/x` if `dev` branch already exists remotely)

### Student Branch / Handoff Rules
- `neutron2-develop` is the shared post-v1 student development branch, created from the frozen MVP demo baseline; verify the current branch before making student-facing edits.
- Keep student-facing docs KISS: clear, concise, consistent, and explicit about what students should edit.
- Use native application/manager/driver terminology from `docs/SYSTEM_ARCHITECTURE.md` and `docs/STUDENT_COMPONENT_STARTERS.md`; do not restate the full architecture here.
- Placeholder or request-state commands must not imply real hardware actuation. Say plainly when a component records intent only.
- When helping students with GitHub flow, use the `student-git-handoff` skill and keep their work on feature branches from the approved student base.

### Demo-First Decision Rule
- Prefer the smallest implementation that supports the live demo story.
- Demo-critical visible behaviors take priority over deeper architectural cleanup.
- The current minimum acceptable demo story is:
  - nominal bring-up in `Base Mode`
  - live `SOH` telemetry on the ground display
  - operator-issued scheduled collection command
  - data collection after a short delay
  - science-data downlink
  - ground-side review or analysis of that science data
- If a real subsystem is not stable enough, document and use a simulation/fallback path rather than leaving the demo story incomplete.

### Runtime Smoke Flow (RPi)
- Binary:
  - `ArtemisRpiTeensy_N2/build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment`
- Run:
  - `cd ~/fprime-artemis-cubesat/ArtemisRpiTeensy_N2 && ./build-artifacts/Linux/bin/ArtemisRpiTeensyDeployment -d /dev/serial0`
- Minimum pass criteria:
  - startup banner appears
  - no immediate init assertion
  - process remains alive for multiple seconds
