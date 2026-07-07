# Time & Scheduling

How "time" works in the flight software and, concretely, how the **"schedule data collection in N seconds"** demo beat is actually implemented. Written so a student can find and modify the timing behavior with confidence.

See the [Glossary](GLOSSARY.md) for terms and [`SYSTEM_ARCHITECTURE.md`](SYSTEM_ARCHITECTURE.md) for the component model.

## Two different notions of "time"

F´ separates **wall-clock time** from **periodic scheduling**, and this project uses both:

1. **Wall-clock time (timestamps).** Every event and telemetry sample is stamped with a real time so GDS shows when things happened. The time source is the `chronoTime` instance, wired in the topology as `time connections instance chronoTime`. Components request the current time through a `time get` port (e.g. MissionApp declares `time get port timeCaller`).
2. **Periodic scheduling (rate groups).** Anything that needs to "run every so often" is ticked by a **rate group**, not by sleeping or reading the clock in a loop.

The countdown for scheduled collection is built on the **rate-group** mechanism, not on wall-clock deltas — see below.

## The rate-group clock

The program's master cycle runs at **1 Hz**:

```cpp
// ArtemisRpiTeensyDeployment/Main.cpp
ArtemisRpiTeensyDeployment::startRateGroups(Fw::TimeInterval(1,0));  // 1 Hz base cycle
```

That 1 Hz cycle drives `timer → rateGroupDriver`, which fans out to three rate groups with divisors `{1, 2, 4}`:

| Rate group | Divisor | Frequency | Period |
| --- | ---: | ---: | ---: |
| `rateGroup1` | 1 | **1 Hz** | 1 s |
| `rateGroup2` | 2 | 0.5 Hz | 2 s |
| `rateGroup3` | 4 | 0.25 Hz | 4 s |

(Divisors are set by `rateGroupDivisorsSet{{{1,0},{2,0},{4,0}}}` in `Top/ArtemisRpiTeensyDeploymentTopology.cpp`.)

A component "joins" a rate group by connecting its `run: Svc.Sched` input to a `RateGroupMemberOut` port in `Top/topology.fpp`. For example:

```
rateGroup1.RateGroupMemberOut[6] -> missionApp.run
rateGroup1.RateGroupMemberOut[8] -> scienceApp.run
```

Both `missionApp` and `scienceApp` tick on **rateGroup1 at 1 Hz**. That 1 Hz is the key number that makes the countdown below come out in real seconds.

## How "collect in N seconds" actually works

The demo command is `missionApp.SCHEDULE_COLLECTION(delaySeconds)`. Here is the full chain:

1. **Command arrives.** `MissionApp::SCHEDULE_COLLECTION_cmdHandler` sets mode to `COLLECTION_PENDING`, records `delaySeconds`, emits `ModeChanged` + `CollectionScheduled`, and **forwards the delay** to ScienceApp: `collectionRequestOut_out(0, delaySeconds)`.
2. **ScienceApp takes ownership of the countdown.** `ScienceApp::requestIn_handler` stores it: `m_pendingDelaySeconds = delaySeconds` and logs `CollectionTriggered`.
3. **The countdown ticks down once per second.** `ScienceApp::run_handler` runs every rateGroup1 tick (1 Hz) and decrements:
   ```cpp
   if (m_pendingDelaySeconds > 0) {
       m_pendingDelaySeconds -= 1;          // one tick == one second at 1 Hz
       if (m_pendingDelaySeconds == 0) {
           payloadRequestOut_out(0, m_captureDurationSeconds);                 // start collection
           missionModeOut_out(0, MissionMode::COLLECTING, m_captureDurationSeconds);  // announce mode
       }
   }
   ```
4. **Collection fires.** When the counter hits 0, ScienceApp requests a payload capture and emits a `COLLECTING` mode update, which flows to `missionApp.modeUpdateIn` and moves the mission state forward.

So `SCHEDULE_COLLECTION(10)` = 10 rateGroup1 ticks = **~10 real seconds**, because rateGroup1 runs at exactly 1 Hz.

## Telemetry cadence (same mechanism)

The rate-group tick also paces telemetry so the radio is not flooded (see the [RF link constraint](SYSTEM_ARCHITECTURE.md#the-rf-link-constraint-how-fprime-gds-talks-over-a-walkie-talkie)). For example, `MissionApp::run_handler` increments a heartbeat each tick and only writes telemetry every **30 ticks (~30 s)** unless something changes; `ScienceApp` writes immediately while a countdown is active and otherwise every 30 ticks.

## Gotchas

- **The countdown is tick-based, not wall-clock-based.** It assumes rateGroup1 actually runs at 1 Hz. If the rate group stalls or the base cycle changes, the countdown stretches with it. It is not a real-time deadline.
- **Changing the base rate rescales every countdown.** If you change `startRateGroups(...)` away from 1 Hz, the per-tick `-= 1` no longer equals one second. Keep the 1 Hz base, or convert tick math to use elapsed wall-clock time from `chronoTime`.
- **Don't sleep in a handler.** Long work or `sleep()` inside a rate-group or command handler blocks the component. Use the rate-group tick to do a little work each cycle (as the countdown does).
- **Wall-clock vs scheduling are separate.** GDS timestamps come from `chronoTime`; the collection delay comes from rate-group ticks. Don't assume one controls the other.
