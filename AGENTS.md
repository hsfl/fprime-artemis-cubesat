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

Project skills live in `.claude/skills/` and are auto-loaded by Claude Code. Codex-style agents (which read AGENTS.md but don't auto-load skills) should consult these files directly:

| Skill | File | Use when... |
|-------|------|-------------|
| `fprime-swe` | [`.claude/skills/fprime-swe/SKILL.md`](.claude/skills/fprime-swe/SKILL.md) | Developing F' components, ports, topologies, running builds, or using fprime-util |
| `fprime-docs-search` | [`.claude/skills/fprime-docs-search/SKILL.md`](.claude/skills/fprime-docs-search/SKILL.md) | Looking up F' framework docs — check repo-local docs at `ArtemisRpiTeensy_N2/lib/fprime/docs` first |
| `fprime-cross-compilation` | [`.claude/skills/fprime-cross-compilation/SKILL.md`](.claude/skills/fprime-cross-compilation/SKILL.md) | Cross-compiling for ARM targets; Pi Zero W ARMv6 landmine documented here |

## F' Agent Usage Guide

Use this project with the `fprime-swe` skill and follow these steps exactly.

## Current Project Quick Start (Read First)

- Read `docs/SYSTEM_ARCHITECTURE.md` first.
  - This is the required system-level crosswalk between the Neutron 2 target architecture and the Artemis-based prototype used for the current demo.
  - Do not continue with subsystem or architecture work until this file has been read.
- Read `README.md` for the top-level architecture and current status.
- Read `docs/agents_notes.md` for latest implementation details and pending TODO items.
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
- `espcor_teensy_demo` is reference-only unless explicitly requested:
  - `espcor_teensy_demo`

## Quick Start
1. Activate the venv before any F' command:
   ```bash
   . ArtemisRpiTeensy_N2/fprime-venv/bin/activate
   ```
2. Ensure a build cache exists (required before generators):
   ```bash
   cd ArtemisRpiTeensy_N2
   fprime-util generate
   ```
   If it already exists, use:
   ```bash
   fprime-util generate -f
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
  - `rpi_build.instructions`
- Build policy:
  - build on the Raspberry Pi target and run the locally built binary.

## Run `fprime-gds` over UART (RPi/Operator Side)

Preferred launcher (repo-maintained defaults):
```bash
cd ArtemisRpiTeensy_N2
./tools/run_gds_uart.sh
```

Script defaults:
- UART device: `/dev/cu.usbmodem115551201`
- UART baud: `115200`
- GUI port: `5050`
- dictionary: `build-artifacts/Darwin/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json`

Useful overrides:
```bash
./tools/run_gds_uart.sh --port /dev/cu.usbmodemXXXX --baud 115200
./tools/run_gds_uart.sh --gui-port 5050
./tools/run_gds_uart.sh --dictionary /abs/path/to/TopologyDictionary.json
./tools/run_gds_uart.sh --dry-run
```

Notes:
- On macOS, port `5000` may already be occupied by Control Center/AirPlay Receiver. Use non-5000 GUI ports (default script port is `5050`).
- If using raw CLI instead of script, pass UART args explicitly:
  - `fprime-gds -n --communication-selection uart --uart-device <device> --uart-baud 115200 --framing-selection space-packet-space-data-link`

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
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/build.sh
```

### Upload (Arduino CLI)
```bash
cd ArtemisTeensy_N2_Baremetal
./tools/arduino-cli/upload.sh /dev/ttyACM0
```
- Replace `/dev/ttyACM0` with the actual connected Teensy port.

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
- Treating `espcor_teensy_demo/` as active code; it is reference-only unless explicitly requested.
- Changing UART settings without updating both firmware and UART contract docs.

## Ground Teensy (`GDS_Teensy`) Agent Usage Guide

Use this section when working in the ground bridge workspace:
- `GDS_Teensy`

### Build (Arduino CLI)
Run from `GDS_Teensy`:
```bash
cd GDS_Teensy
./tools/arduino-cli/build.sh
```

### Upload (Arduino CLI)
Run from `GDS_Teensy`:
```bash
cd GDS_Teensy
./tools/arduino-cli/upload.sh /dev/cu.usbmodemXXXX
```
- Use the actual detected USB modem/ACM port.
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
- Current submodule priority: `external/pdu-firmware/` first, `external/satnogs-radio/` second once the SatNOGS repo/dev board is real, `external/payload/` only once the payload board repo exists. Do not add ADCS/GPS/IMU/thermal submodules until they have standalone firmware/tooling repos.
- If using `fprime-util new --deployment` for validation, register it in `project.cmake` during the prompt so build targets exist.
- In this repo, prefer `fprime-util generate -f` to avoid stale build-cache failures.
- For deployment smoke validation, direct CMake target build is reliable:
  - `cmake --build build-fprime-automatic-native --target <DeploymentName>`
- Remove temporary smoke deployments after validation unless explicitly requested to keep them.

### Recommended Development Flow
1. Read `README.md` and `docs/agents_notes.md` for current architecture/state.
2. Activate F' venv:
   - `. ArtemisRpiTeensy_N2/fprime-venv/bin/activate`
3. Validate F' build:
   - `cd ArtemisRpiTeensy_N2`
   - `fprime-util generate -f`
   - `fprime-util build`
4. Validate Teensy compile:
   - `cd ../ArtemisTeensy_N2_Baremetal`
   - `./tools/arduino-cli/build.sh`
5. Re-run both validations after topology/driver changes.
6. Before commit/push:
   - verify submodule SHA is intentional (`git submodule status --recursive`)
   - verify no build/cache artifacts are staged (`git status --short`)
   - verify remote branch naming is push-compatible (avoid `dev/x` if `dev` branch already exists remotely)

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
  - `ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment`
- Run:
  - `./ArtemisRpiTeensyDeployment -d /dev/serial0`
- Minimum pass criteria:
  - startup banner appears
  - no immediate init assertion
  - process remains alive for multiple seconds
