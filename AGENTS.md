# AGENTS.md
This file provides guidance to coding agents when working with code in this repository.
IMPORTANT: Prefer retrieval-led reasoning over pre-training-led reasoning for any F Prime tasks

<!>DOCS
[F Prime Docs Index]|root: ./.project-docs
|overview:{main,powerful-swa,streamline,docs,manual,intro,arch,port-comp-top,cmd-evt-chn,types,dev-practice,proj-dep,source,unit-test}
|tutorial:{start,install,index,cross-compile}
|howto:{index,framing,state-machines,channels,driver,libs,gds-plugin,subtopo,ext-libs,port,tdd}
|build:{intro,api,custom,impl,platforms,targets,toolchains,uts,settings,topology}
|framework:{assert,autocoded,baremetal,config,data-products,dynamic-mem,ground-if,state-machines,platforms}
|gds:{intro,cli,dashboards,dash-ref,dev,test-api,seqgen}
|design:{app-man-drv,ports,health,hub,manager-worker,rate-group,subtopo}
|svc:{index,deframer,deframer-sdd,framer,framer-sdd,subtopo-index}
|subtopo:{cdh,ccsds,fprime,data,files}
|security:{sbom}
|reference:{api}
|urls:{overview/main|https://fprime.jpl.nasa.gov/overview/,overview/powerful-swa|https://fprime.jpl.nasa.gov/overview/powerful-swa/,overview/streamline|https://fprime.jpl.nasa.gov/overview/streamline-sd/,overview/docs|https://fprime.jpl.nasa.gov/latest/docs/,overview/manual|https://fprime.jpl.nasa.gov/latest/docs/user-manual/,overview/intro|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/01-full-intro/,overview/arch|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/02-fprime-architecture/,overview/port-comp-top|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/03-port-comp-top/,overview/cmd-evt-chn|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/04-cmd-evt-chn-prm/,overview/types|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/05-enum-arr-ser/,overview/dev-practice|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/development-practice/,overview/proj-dep|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/proj-dep/,overview/source|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/source-tree/,overview/unit-test|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/unit-testing/,tutorial/start|https://fprime.jpl.nasa.gov/latest/docs/getting-started/,tutorial/install|https://fprime.jpl.nasa.gov/latest/docs/getting-started/installing-fprime/,tutorial/index|https://fprime.jpl.nasa.gov/latest/docs/tutorials/,tutorial/cross-compile|https://fprime.jpl.nasa.gov/latest/docs/tutorials/cross-compilation/,howto/index|https://fprime.jpl.nasa.gov/latest/docs/how-to/,howto/framing|https://fprime.jpl.nasa.gov/latest/docs/how-to/custom-framing/,howto/state-machines|https://fprime.jpl.nasa.gov/latest/docs/how-to/define-state-machines/,howto/channels|https://fprime.jpl.nasa.gov/latest/docs/how-to/derive-channels-on-ground/,howto/driver|https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-device-driver/,howto/libs|https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-fprime-libraries/,howto/gds|https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-gds-plugins/,howto/subtopo|https://fprime.jpl.nasa.gov/latest/docs/how-to/develop-subtopologies/,howto/ext-libs|https://fprime.jpl.nasa.gov/latest/docs/how-to/integrate-external-libraries/,howto/port|https://fprime.jpl.nasa.gov/latest/docs/how-to/porting-guide/,howto/tdd|https://fprime.jpl.nasa.gov/latest/docs/how-to/test-driven-development/,build/intro|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/01-cmake-intro/,build/api|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-api/,build/custom|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-customization/,build/impl|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-implementations/,build/platforms|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-platforms/,build/targets|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-targets/,build/toolchains|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-toolchains/,build/uts|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/cmake-uts/,build/settings|https://fprime.jpl.nasa.gov/latest/docs/user-manual/build-system/settings/,build/topology|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/building-topology/,framework/assert|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/assert/,framework/autocoded|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/autocoded-functions/,framework/baremetal|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/baremetal-multicore/,framework/config|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/configuring-fprime/,framework/data-products|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/data-products/,framework/dynamic-mem|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/dynamic-memory/,framework/ground-if|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/ground-interface/,framework/state-machines|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/state-machines/,framework/platforms|https://fprime.jpl.nasa.gov/latest/docs/user-manual/framework/supported-platforms/,gds/intro|https://fprime.jpl.nasa.gov/latest/docs/user-manual/overview/gds-introduction/,gds/cli|https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-cli/,gds/dashboards|https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-custom-dashboards/,gds/dash-ref|https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-dashboard-reference/,gds/dev|https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-dev-guide/,gds/test-api|https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/gds-test-api-guide/,gds/seqgen|https://fprime.jpl.nasa.gov/latest/docs/user-manual/gds/seqgen/,design/app-man-drv|https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/app-man-drv/,design/ports|https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/common-port-patterns/,design/health|https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/health-checking/,design/hub|https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/hub-pattern/,design/manager-worker|https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/manager-worker/,design/rate-group|https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/rate-group/,design/subtopo|https://fprime.jpl.nasa.gov/latest/docs/user-manual/design-patterns/subtopologies/,svc/index|https://fprime.jpl.nasa.gov/latest/docs/Svc/,svc/deframer|https://fprime.jpl.nasa.gov/latest/docs/Svc/Deframer/,svc/deframer-sdd|https://fprime.jpl.nasa.gov/latest/docs/Svc/Deframer/docs/sdd.md,svc/framer|https://fprime.jpl.nasa.gov/latest/docs/Svc/Framer/,svc/framer-sdd|https://fprime.jpl.nasa.gov/latest/docs/Svc/Framer/docs/sdd.md,svc/subtopo-index|https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/,subtopo/cdh|https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/CdhCore,subtopo/ccsds|https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/ComCcsds,subtopo/fprime|https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/ComFprime/,subtopo/data|https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/DataProducts,subtopo/files|https://fprime.jpl.nasa.gov/latest/docs/Svc/Subtopologies/FileHandling,security/sbom|https://fprime.jpl.nasa.gov/latest/docs/user-manual/security/software-bill-of-materials/,reference/api|https://fprime.jpl.nasa.gov/latest/docs/reference/}
</!>

## F' Agent Usage Guide

Use this project with the `fprime-swe` skill and follow these steps exactly.

## Current Project Quick Start (Read First)

- Read `/Users/sozodennis/Developer/fprime-artemis-cubesat/README.md` for the top-level architecture and current status.
- Read `/Users/sozodennis/Developer/fprime-artemis-cubesat/NOTES.md` for latest implementation details and pending TODO items.
- Active F' project root is:
  - `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisRpiTeensy_N2`
- Active baremetal Teensy workspace is:
  - `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal`
- `espcor_teensy_demo` is reference-only unless explicitly requested:
  - `/Users/sozodennis/Developer/fprime-artemis-cubesat/espcor_teensy_demo`

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

## Common Pitfalls
- Running generators without a build cache.
- Running `fprime-util new --component` from the project root instead of `Components/`.
- Not answering the final generator prompts (CMake + impl generation), which leaves partial directories.
- Forgetting to add deployments to `project.cmake`.

## Teensy Baremetal Agent Usage Guide

Use this section when working in the Teensy bridge workspace:
- `/Users/sozodennis/Developer/fprime-artemis-cubesat/ArtemisTeensy_N2_Baremetal`

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

## Biggest Lessons / SWE Flow (Neutron 2)

### Lessons Learned
- Keep `ArtemisRpiTeensy_N2/lib/fprime` as a root-managed Git submodule; avoid nested standalone repos inside the workspace.
- If using `fprime-util new --deployment` for validation, register it in `project.cmake` during the prompt so build targets exist.
- In this repo, prefer `fprime-util generate -f` to avoid stale build-cache failures.
- For deployment smoke validation, direct CMake target build is reliable:
  - `cmake --build build-fprime-automatic-native --target <DeploymentName>`
- Remove temporary smoke deployments after validation unless explicitly requested to keep them.

### Recommended Development Flow
1. Read `README.md` and `NOTES.md` for current architecture/state.
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

### Runtime Smoke Flow (RPi)
- Binary:
  - `ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment`
- Run:
  - `./ArtemisRpiTeensyDeployment -d /dev/serial0`
- Minimum pass criteria:
  - startup banner appears
  - no immediate init assertion
  - process remains alive for multiple seconds
