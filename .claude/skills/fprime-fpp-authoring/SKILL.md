---
name: fprime-fpp-authoring
description: "Use when writing or editing FPP (F Prime Prime) files: component definitions, ports, commands, events, telemetry channels, parameters, enums/arrays/structs, instances.fpp, topology.fpp wiring, base IDs, or diagnosing FPP autocoder/codegen errors. Covers this repo's FPP conventions and the wiring checklist for adding a component to the topology."
---

# FPP Authoring Cookbook

## Ground Rules

- After ANY `.fpp`/`.fppi` change: `fprime-util generate -f` then `fprime-util build`
  (from `ArtemisRpiTeensy_N2`, venv active). The autocoder only runs at generate time.
- FPP language reference is NOT on the main F' docs site. Use
  `https://nasa.github.io/fpp/fpp-users-guide.html` (via the `fprime-docs-search` skill).
- Copy an existing repo component as your template — `Components/SoHApp/SoHApp.fpp`
  is a clean example. `docs/STUDENT_COMPONENT_STARTERS.md` has student-facing starters.

## Component Definition Anatomy

```fpp
module Components {
    @ One-line description (@ comments become generated doc strings)
    active component MyThing {

        @ Health ping input                     # Svc.Ping pair = health wiring
        async input port pingIn: Svc.Ping
        @ Health ping output
        output port pingOut: Svc.Ping

        @ Rate group scheduling input
        sync input port run: Svc.Sched

        @ Status inputs (port array, size 8)
        sync input port statusIn: [8] Components.HealthStatus

        @ An operator command
        async command DO_THING(delaySeconds: U32)

        @ A telemetry channel
        telemetry ThingCount: U32

        @ An event with format string ({} per arg)
        event ThingDone(count: U32) severity activity high format "thing done count={}"

        @ Port for requesting the current time
        time get port timeCaller

        import Fw.Command    # enables commands (cmdIn/cmdRegOut/cmdResponseOut)
        import Fw.Event      # enables events
        import Fw.Channel    # enables telemetry
        # import Fw.Param    # only if the component has parameters
    }
}
```

Key choices:

- `active` = own thread + queue → use `async` input ports/commands.
  `passive` = runs on caller's thread → `sync` (or `guarded` for mutex) only.
  A passive component CANNOT have `async` ports/commands (common codegen error).
- Event severities: `diagnostic`, `activity low`, `activity high`, `warning low`,
  `warning high`, `fatal`.
- Parameters: `param NAME: type default <value>` — needs `import Fw.Param` and
  PrmDb wiring.

## Adding a Component to the Deployment — Checklist

All files under `ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/Top/` unless noted.

1. **Component library**: `Components/<Name>/` with `.fpp`, `.cpp`, `.hpp`,
   `CMakeLists.txt` (`register_fprime_library` with `AUTOCODER_INPUTS` + `SOURCES`),
   and the component added to `Components/CMakeLists.txt`.
2. **`instances.fpp`** — declare the instance. Base ID convention `0xDSSCCxxx`:
   D = deployment digit (1), SS = subtopology (00 = main), CC = component number,
   xxx = reserved for internal items. Pick the next free CC. Active instances need
   queue/stack/priority:

   ```fpp
   instance myThing: Components.MyThing base id 0x100NN000 \
     queue size Default.QUEUE_SIZE \
     stack size Default.STACK_SIZE \
     priority 30
   ```

   (Passive instances: just `instance x: Components.Y base id 0x...`.)
3. **`topology.fpp`** — add `instance myThing` to the instance list, then wire
   connections in the matching `connections` block. Health ping is handled by
   `health connections instance CdhCore.$health` — components with
   `pingIn`/`pingOut: Svc.Ping` ports are auto-wired.
4. **`ArtemisRpiTeensyDeploymentTopologyDefs.hpp`** — add a ping entry for the
   new instance in the `PingEntries` namespace (copy an existing entry).
5. **Rate-driven components** — connect `run` to a rate group in `topology.fpp`
   (`rateGroupN.RateGroupMemberOut[i] -> myThing.run`). The topology is
   unified — one `topology.fpp` serves both laptop emulation and HIL.
6. `fprime-util generate -f && fprime-util build`, fix errors, repeat.

## Common FPP/Codegen Errors → Fixes

| Error smell | Cause / fix |
|-------------|-------------|
| `async input port ... in passive component` | Make component `active` or the port `sync`/`guarded` |
| Port type mismatch at connection | Output and input port types must be identical; check both `.fpp` files |
| Duplicate base id / id overlap | Two instances share `0x...` ranges; re-check `instances.fpp` against the `0xDSSCCxxx` map |
| Undefined symbol `Components.X` | Missing `module Components { }` wrapper, or component dir not in `Components/CMakeLists.txt` |
| Missing `timeCaller` / time get port errors | Events/telemetry need `time get port timeCaller` |
| Command/event/tlm helpers missing in C++ | Missing `import Fw.Command` / `Fw.Event` / `Fw.Channel` in the component |
| Changes to `.fpp` seem ignored | Regenerate: `fprime-util generate -f` (stale cache) |
| Unconnected required port assert at startup | Wire it in `topology.fpp` or make the port optional in the component |

## After Codegen — Implementing

`fprime-util impl` produces stubs; handlers follow fixed names:

```cpp
void MyThing ::DO_THING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 delaySeconds) {
    this->log_ACTIVITY_HI_ThingDone(1);
    this->tlmWrite_ThingCount(this->count++);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);  // ALWAYS respond
}
void MyThing ::run_handler(FwIndexType portNum, U32 context) { /* rate tick */ }
```

Forgetting `cmdResponse_out` leaves the command hanging in GDS.

## Repo Conventions

- HAL tiers follow F´'s native Application-Manager-Driver (App-Man-Drv)
  pattern per `docs/SYSTEM_ARCHITECTURE.md`: `<X>App` owns mission logic,
  `<X>Manager` owns the stable subsystem contract, `<X>Driver_<Impl>` owns
  hardware/sim specifics. Applications talk to managers; managers talk to
  drivers; drivers talk to hardware. Managers and drivers swap via shared
  port interfaces defined in `Components/Types/Types.fpp`.
  (Pre-2026-07-06 names — Manager→App, Service→Manager, Adapter→Driver — may
  linger in old logs and archived plans; active code uses the new names.)
- Placeholder commands must not imply real hardware actuation — record intent
  only, and say so in the `@` doc comment.
- Shared types live in `Components/Types` (dep name `Components_Types`).
