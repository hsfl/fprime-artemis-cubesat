# F´ Ground Interfaces Primer: Commands, Events, Telemetry, Parameters

If you are new to F´, the first thing to internalize is the **four ways flight software talks to the ground**. Almost everything you add to a component is one of these four. This primer explains each, shows the real FPP syntax from this repo, the C++ that gets generated, and how it appears in `fprime-gds`.

Background: [`SYSTEM_ARCHITECTURE.md`](SYSTEM_ARCHITECTURE.md) (component model + how bytes reach the ground) and the [Glossary](GLOSSARY.md).

## The four interfaces at a glance

| Interface | Direction | Think of it as | Lives for |
| --- | --- | --- | --- |
| **Command** | ground → spacecraft | "do this now" | one action |
| **Event** | spacecraft → ground | a timestamped log line | one occurrence |
| **Telemetry (channel)** | spacecraft → ground | a gauge / measured value | sampled over time |
| **Parameter** | ground ↔ spacecraft | a persistent setting | until changed |

All four are **declared in FPP** inside a component, and F´ autocodes the boilerplate (serialization, dispatch, dictionary entries) so GDS knows about them automatically. The examples below are from [`Components/MissionApp/MissionApp.fpp`](../ArtemisRpiTeensy_N2/Components/MissionApp/MissionApp.fpp).

## 1. Commands — ground tells the spacecraft to do something

Declare in FPP:

```
@ Simple local ping command for manual GDS loop checks
async command PING(token: U32)

@ Schedule data collection delay in seconds
async command SCHEDULE_COLLECTION(delaySeconds: U32)
```

You implement a handler (autocoded signature):

```cpp
void MissionApp::PING_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 token) {
    this->m_pingCount += 1;
    this->log_ACTIVITY_LO_Pong(token, this->m_pingCount);   // emit an event
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);  // ALWAYS reply
}
```

- `async` = queued and run on the component's thread (needs an active component); `sync`/`guarded` run in the caller's context.
- **Always call `cmdResponse_out(...)`** so GDS sees the command complete (OK / error). The command travels ground → `CdhCore.cmdDisp` → your handler by **opcode** (see the [end-to-end trace](SYSTEM_ARCHITECTURE.md#end-to-end-example-tracing-a-command-and-a-telemetry-channel)).

In GDS: the command appears in the commanding panel; send it with the GUI or:

```bash
fprime-cli command-send ArtemisRpiTeensyDeployment.missionApp.PING --arguments 4245
```

## 2. Events — the spacecraft's log

Declare with a severity and a printf-style format:

```
@ Ping response event
event Pong(token: U32, count: U32) severity activity low format "MissionApp pong token={} count={}"

@ Invalid service-requested mission mode transition rejected
event ModeUpdateRejected(requested: Components.MissionMode, current: Components.MissionMode, detail: U32) \
    severity warning low format "Rejected mode update requested={} current={} detail={}"
```

Emit from C++ (autocoded `log_<SEVERITY>_<Name>`):

```cpp
this->log_ACTIVITY_HI_ModeChanged(this->m_currentMode);
this->log_WARNING_LO_ModeUpdateRejected(mode, this->m_currentMode, detail);
```

- Severities: `diagnostic`, `activity low/high`, `warning low/high`, `command`, `fatal`. Use `warning`/`fatal` sparingly so they mean something.
- Events are great for **discrete things that happened** ("mode changed", "collection scheduled"). They show in the GDS events panel with a timestamp.
- ⚠️ **Keep format strings short** — `FW_LOG_STRING_MAX_SIZE` is only 80 here (see [RF MVP config](SYSTEM_ARCHITECTURE.md#rf-mvp-config-overrides-a-landmine-to-know-about)). Long event strings can truncate.

## 3. Telemetry channels — sampled values

Declare:

```
@ Current mission mode
telemetry CurrentMode: Components.MissionMode

@ Number of pings handled
telemetry PingCount: U32

@ Mission manager heartbeat
telemetry ModeHeartbeat: U32
```

Write from C++ (autocoded `tlmWrite_<Name>`):

```cpp
void MissionApp::writeTelemetry() {
    this->tlmWrite_CurrentMode(this->m_currentMode);
    this->tlmWrite_PingCount(this->m_pingCount);
    this->tlmWrite_ModeHeartbeat(this->m_modeHeartbeat);
}
```

- Telemetry is for **values you watch over time** (mode, counts, voltages). Each channel shows in the GDS channels panel and can be graphed.
- **You choose when to write.** This project writes telemetry on a paced cadence (every ~30 rate-group ticks) to avoid flooding the radio — see [`TIME_AND_SCHEDULING.md`](TIME_AND_SCHEDULING.md) and the [RF constraint](SYSTEM_ARCHITECTURE.md#the-rf-link-constraint-how-fprime-gds-talks-over-a-walkie-talkie).
- Event vs telemetry rule of thumb: *something happened* → event; *current value of something* → telemetry.

## 4. Parameters — persistent settings

MissionApp has none, so here is the general shape. A parameter is a value the ground can set that the spacecraft remembers:

```
@ Example: capture duration the science manager uses
param CaptureDurationSeconds: U32 default 5
```

F´ autocodes a getter and `SET`/`SAVE` commands:

```cpp
Fw::ParamValid valid;
U32 dur = this->paramGet_CaptureDurationSeconds(valid);
```

- Parameters persist via the parameter database (`param connections instance FileHandling.prmDb` in the topology), so they survive across runs once saved.
- Use a parameter for **tunable configuration** (durations, thresholds, limits) rather than hardcoding. Use a **command** when you want a one-shot action, not a remembered setting.

## How this maps to adding a feature

Working within the [Application -> Manager -> Driver](SYSTEM_ARCHITECTURE.md#flight-software-architecture-application--manager--driver-hal) model:

- Want the operator to trigger something? → add a **command** to a manager/service.
- Want the operator to *see* something happened? → add an **event**.
- Want the operator to *monitor* a value? → add a **telemetry channel**.
- Want a remembered, ground-settable knob? → add a **parameter**.

After editing the `.fpp`, rebuild (`fprime-util build`) to regenerate the dictionary so GDS picks up the new command/event/channel/param automatically.

## Where to go deeper

- F´ local docs (matched to the checked-out submodule version): `ArtemisRpiTeensy_N2/lib/fprime/docs` — see `user-manual/overview/04-cmd-evt-chn-prm.md`.
- This repo's component examples under `ArtemisRpiTeensy_N2/Components/` (e.g. `MissionApp`, `ScienceApp`, `EpsManager`).
- Student starter guidance: [`STUDENT_COMPONENT_STARTERS.md`](STUDENT_COMPONENT_STARTERS.md).
