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
| `SCIENCE_READY` | `SCIENCE_PROCESSING` | ScienceApp, once StorageManager reports the product committed |
| `DOWNLINKING` | `SCIENCE_DOWNLINK` | CommsApp, or `SET_MODE SCIENCE_DOWNLINK` for a ground-run window |
| `SAFE` (new, value 5) | `SAFE` | `SET_MODE SAFE`, a fault on `faultIn`, or `BOOT_IN_SAFE` at start-up |

- `SET_MODE(mode)` is the single "switch operations mode" command (C&T Modes row 1). Ground can pick `BASE`, `SAFE` or `SCIENCE_DOWNLINK`. `SCIENCE_COLLECTION` is refused with `USE_SCHEDULE_COLLECTION` and `SCIENCE_PROCESSING` with `AUTOMATIC_ONLY`, because those modes start only when the payload confirms them. A downlink window is refused with `COLLECTION_ACTIVE` while a collection is queued or running. Refusals are reported by the `SetModeRejected` event.
- `SAFE` holds against every subsystem report. Only `SET_MODE BASE` or `ENTER_BASE_MODE` can leave, and only when no fault latch is set (`CLEAR_SAFE_LATCH`) and no fault arrived within `SAFE_EXIT_STABLE_SECONDS` (parameter, default 10, provisional). Entering SAFE drops any queued collection.
- MissionApp owns the mode rules and decides what each mode allows. Components take simple runtime controls through ports and enforce them where the operation starts. `capturePermitted()` in MissionApp is the one rule for captures.
- On SAFE entry MissionApp tells PayloadManager to inhibit new captures (`captureControlOut`) and ScienceApp to cancel pending collections (`cancelRequestOut`). Both report back. SAFE entry is complete, and `SafeModeEntered` logged, only then. `SafeEntryUnconfirmed` warns after 5 s, and SAFE cannot be left until both reports are in.
- While inhibited, PayloadManager refuses every new capture and pass-through with `INHIBITED`, at one check, whether the request came from a direct command or from ScienceApp. ScienceApp reports the refusal and holds no mode logic.
- **Partial SAFE, tracked separately:** a capture already running when SAFE starts finishes, and its product is stored. SAFE does not stop the payload or switch its power off yet, EPS faults do not reach `faultIn` yet, and SAFE downlink policy is not set, so a capture that finishes in SAFE and asked for automatic downlink still sends it.
- On every operations-mode change, `SubsystemStatesRequested` reports what the new mode asks of EPS, OBC, UHF TX/RX, S-band, GPS, ADCS and the payload (MOP v2.0 §8 hardware status). MissionApp declares the plan. No manager port acts on it yet.
- `AUTO_RETURN_TO_BASE` (parameter, default 0) applies step 3.4 of the Phase A EM MVP demo: with 1, MissionApp returns to `BASE` as soon as ScienceApp reports the committed product. A storage failure reports `BASE` with `ScienceProductNotStored`, the same mode report a failed capture gives. With 0, `SCIENCE_READY` holds until a downlink or the ground, which the current team scripts check for.
- `faultIn` (port) and `SIMULATE_SAFE_FAULT` (local Yamcs builds only) are the fault paths. No manager is wired to `faultIn` yet. `BOOT_IN_SAFE` (parameter, default 0) applies FSW-013. It stays off because the CDR and demo scripts expect `BASE` after start-up. Saved parameters load at boot, so turning it on takes `BOOT_IN_SAFE_PRM_SET`, `BOOT_IN_SAFE_PRM_SAVE`, then `prmDb.PRM_SAVE_FILE`. `PrmDb.dat` is read from the folder F′ runs in, and with no file F′ logs `PrmFileReadError` at boot.
- New command, event, channel and parameter IDs are appended. Every earlier MissionApp ID and every earlier `MissionMode` value is unchanged.

[MissionApp tests](../../ArtemisRpiTeensy_N2/Components/MissionApp/test/ut/MissionAppTestMain.cpp) cover nominal transitions, rejection guards, manual downlink retry, and cancellation. They passed in [baseline packaging validation](../verification/evidence/n2-packaging-2026-10-14/README.md). The D5 tests add every cell of the transition table (36 subsystem-report cells, 30 `SET_MODE` cells, 6 fault rows), the SAFE guards, boot behavior, and the MOP hardware plan. [PayloadManager tests](../../ArtemisRpiTeensy_N2/Components/PayloadManager/test/ut/PayloadManagerTestMain.cpp) cover the capture gate: direct commands, ScienceApp requests and pass-through while inhibited, a request queued behind the inhibit, a capture already running, and captures after SAFE. [ScienceApp tests](../../ArtemisRpiTeensy_N2/Components/ScienceApp/test/ut/ScienceAppTestMain.cpp) cover `SCIENCE_READY` waiting for the storage commit, a storage failure, a capture refused at the gate, and the cancel report. [StorageManager tests](../../ArtemisRpiTeensy_N2/Components/StorageManager/test/ut/StorageManagerTestMain.cpp) cover the commit answer. [`tools/yamcs/mode_cycle.py`](../../tools/yamcs/mode_cycle.py) runs the same cycle through Yamcs ([YAMCS_LOCAL_TESTING.md](../development/YAMCS_LOCAL_TESTING.md)).

Revised October CDR scope is payload acquisition plus PDU-controlled payload on/off, without COMMS or required formal mission modes. CDR command/telemetry uses Pi Zero W F′ TCP over the local network to Pi 5 Ubuntu/Yamcs, with operator browsers on the LAN. The direct stock ComCcsds/TCP and FileDownlink profile passed [local native acceptance](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md) on October 20, with no network mux and independent fake UART channel-2 PDU RPC. Pi runtime and hardware validation remain open. Use the [canonical current plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) for CDR implementation and remaining gates. December FSR adds the complete COMMS/ground chain with the previously planned nominal scheduling/modes and integrated PDU power sequence; the fuller flight-mode model and hardware sequence remain separate work. See [development intent](../planning/DEVELOPMENT_INTENT.md) and [system architecture](SYSTEM_ARCHITECTURE.md).
