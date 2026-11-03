# N2 mission modes

**Implemented:** a handwritten C++ demo transition guard in [MissionApp](../../ArtemisRpiTeensy_N2/Components/MissionApp/MissionApp.cpp), with mode values declared in [Types.fpp](../../ArtemisRpiTeensy_N2/Components/Types/Types.fpp). This is not the full flight-mode design in the SDD.

The existing local/legacy custom channel-1 demo follows the sequence below. CDR instead uses collection-only commands followed by stock FileDownlink and does not couple file receipt to these mission-mode transitions.

The nominal scheduled demo follows:

```text
BASE → COLLECTION_PENDING → COLLECTING → SCIENCE_READY → DOWNLINKING → BASE
```

MissionApp owns the current mode and validates updates. ScienceApp owns capture scheduling/execution and reports collection states; CommsApp reports downlink state. MissionApp's rate callback publishes heartbeat/telemetry; it does not itself execute the capture countdown. Commands include `SCHEDULE_COLLECTION` (delay 1–300 seconds), `CANCEL_COLLECTION`, `ENTER_BASE_MODE`, and the read-only `REPORT_MODE`, which answers with one `ModeReport` event naming the current mode and changes nothing.

Additional allowed transitions support direct collection/downlink from BASE. Repeating the current mode and returning to BASE are accepted; other transitions are rejected by `checkTransition`. Returning to BASE/canceling a pending collection is not proof of aborting every active hardware operation. See source for exact behavior.

## Demo modes (roadmap D5, proposal under review)

MOP v2.0 §8 names the operations modes. Each demo step belongs to one of them, reported in the `OperationsMode` telemetry channel:

| Demo step (`MissionMode`) | Operations mode (`OperationsMode`) | Entered by |
| --- | --- | --- |
| `BASE`, `COLLECTION_PENDING` | `BASE` | start-up, `SET_MODE BASE`, `ENTER_BASE_MODE`, end of a downlink |
| `COLLECTING` | `SCIENCE_COLLECTION` | ScienceApp, once PayloadManager accepts the capture |
| `SCIENCE_READY` | `SCIENCE_PROCESSING` | ScienceApp, once a valid product is handed to storage |
| `DOWNLINKING` | `SCIENCE_DOWNLINK` | CommsApp, or `SET_MODE SCIENCE_DOWNLINK` for a ground-run window |
| `SAFE` (new, value 5) | `SAFE` | `SET_MODE SAFE`, a fault on `faultIn`, or `BOOT_IN_SAFE` at start-up |

- `SET_MODE(mode)` is the single "switch operations mode" command (C&T Modes row 1). Ground can pick `BASE`, `SAFE` or `SCIENCE_DOWNLINK`. `SCIENCE_COLLECTION` is refused with `USE_SCHEDULE_COLLECTION` and `SCIENCE_PROCESSING` with `AUTOMATIC_ONLY`, because those modes start only when the payload confirms them. A downlink window is refused with `COLLECTION_ACTIVE` while a collection is queued or running. Refusals are reported by the `SetModeRejected` event.
- `SAFE` holds against every subsystem report. Only `SET_MODE BASE` or `ENTER_BASE_MODE` can leave, and only when no fault latch is set (`CLEAR_SAFE_LATCH`) and no fault arrived within `SAFE_EXIT_STABLE_SECONDS` (parameter, default 10, provisional). Entering SAFE drops any queued collection.
- Science is inhibited in SAFE (SDD v4.0 Table 5). MissionApp sends its mode to ScienceApp on every change and at start-up (`missionApp.modeOut -> scienceApp.missionModeIn`, both topologies). While it reads SAFE, ScienceApp refuses every capture path it owns: `START_COLLECTION`, `SCIENCE_CAPTURE`, `COLLECT_AND_DOWNLINK`, `VERIFIED_COLLECT_AND_DOWNLINK`, a scheduled request and a countdown that reaches zero. Each refusal gives `VALIDATION_ERROR` (for commands) and a `ScienceInhibited` event naming the path. `CONFIGURE_CAPTURE_DURATION` still works because it starts nothing.
- Two captures cannot be stopped from the application layer: one already running when SAFE begins, and one started by PayloadManager's own `START_PAYLOAD_COLLECTION` or `SCIENCE_CAPTURE` command. PayloadManager owns the capture (its request port has no cancel) and the EPS rail owns payload power. ScienceApp reports `CaptureRunningInSafe`, and when such a capture ends it withholds the product (`CaptureWithheld`): not handed to StorageManager, not reported as `SCIENCE_READY` and not sent, even if SAFE has ended by then. The attempt stays partial (SDD v4.0 §5.2) and the payload's file stays where the payload wrote it. Stopping the capture itself needs a PayloadManager cancel or the EPS payload rail switched off on SAFE entry: open for the manager and EPS owners.
- On every operations-mode change, `SubsystemStatesRequested` reports what the new mode asks of EPS, OBC, UHF TX/RX, S-band, GPS, ADCS and the payload (MOP v2.0 §8 hardware status). MissionApp declares the plan. No manager port acts on it yet.
- `AUTO_RETURN_TO_BASE` (parameter, default 0) applies step 3.4 of the Phase A EM MVP demo: with 1, MissionApp returns to `BASE` as soon as ScienceApp reports the stored product. With 0, `SCIENCE_READY` holds until a downlink or the ground, which the current team scripts check for.
- `faultIn` (port) and `SIMULATE_SAFE_FAULT` (local Yamcs builds only) are the fault paths. No manager is wired to `faultIn` yet. `BOOT_IN_SAFE` (parameter, default 0) applies FSW-013. It stays off because the CDR and demo scripts expect `BASE` after start-up.
- New command, event, channel and parameter IDs are appended. Every earlier MissionApp ID and every earlier `MissionMode` value is unchanged.

[MissionApp tests](../../ArtemisRpiTeensy_N2/Components/MissionApp/test/ut/MissionAppTestMain.cpp) cover nominal transitions, rejection guards, manual downlink retry, and cancellation. They passed in [baseline packaging validation](../verification/evidence/n2-packaging-2026-10-14/README.md). The D5 tests add every cell of the transition table (36 subsystem-report cells, 30 `SET_MODE` cells, 6 fault rows), the SAFE guards, boot behavior, and the MOP hardware plan. [ScienceApp tests](../../ArtemisRpiTeensy_N2/Components/ScienceApp/test/ut/ScienceAppTestMain.cpp) cover every capture path in SAFE, a capture running when SAFE begins, a PayloadManager capture in SAFE, and captures after SAFE. [`tools/yamcs/mode_cycle.py`](../../tools/yamcs/mode_cycle.py) runs the same cycle through Yamcs ([YAMCS_LOCAL_TESTING.md](../development/YAMCS_LOCAL_TESTING.md)).

Revised October CDR scope is payload acquisition plus PDU-controlled payload on/off, without COMMS or required formal mission modes. CDR command/telemetry uses Pi Zero W F′ TCP over the local network to Pi 5 Ubuntu/Yamcs, with operator browsers on the LAN. The direct stock ComCcsds/TCP and FileDownlink profile passed [local native acceptance](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md) on October 20, with no network mux and independent fake UART channel-2 PDU RPC. Pi runtime and hardware validation remain open. Use the [canonical current plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) for CDR implementation and remaining gates. December FSR adds the complete COMMS/ground chain with the previously planned nominal scheduling/modes and integrated PDU power sequence; the fuller flight-mode model and hardware sequence remain separate work. See [development intent](../planning/DEVELOPMENT_INTENT.md) and [system architecture](SYSTEM_ARCHITECTURE.md).
