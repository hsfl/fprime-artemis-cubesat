# N2 mission modes

**Implemented:** a handwritten C++ demo transition guard in [MissionApp](../../ArtemisRpiTeensy_N2/Components/MissionApp/MissionApp.cpp), with mode values declared in [Types.fpp](../../ArtemisRpiTeensy_N2/Components/Types/Types.fpp). This is not the full flight-mode design in the SDD.

The existing local/legacy custom channel-1 demo follows the sequence below. CDR instead uses collection-only commands followed by stock FileDownlink and does not couple file receipt to these mission-mode transitions.

The nominal scheduled demo follows:

```text
BASE → COLLECTION_PENDING → COLLECTING → SCIENCE_READY → DOWNLINKING → BASE
```

MissionApp owns the current mode and validates updates. ScienceApp owns capture scheduling/execution and reports collection states; CommsApp reports downlink state. MissionApp's rate callback publishes heartbeat/telemetry; it does not itself execute the capture countdown. Commands include `SCHEDULE_COLLECTION` (delay 1–300 seconds), `CANCEL_COLLECTION`, `ENTER_BASE_MODE`, and the read-only `REPORT_MODE`, which answers with one `ModeReport` event naming the current mode and changes nothing.

Additional allowed transitions support direct collection/downlink from BASE. Repeating the current mode and returning to BASE are accepted; other transitions are rejected by `isAllowedTransition`. Returning to BASE/canceling a pending collection is not proof of aborting every active hardware operation. See source for exact behavior.

[MissionApp tests](../../ArtemisRpiTeensy_N2/Components/MissionApp/test/ut/MissionAppTestMain.cpp) cover nominal transitions, rejection guards, manual downlink retry, and cancellation. They passed in [baseline packaging validation](../verification/evidence/n2-packaging-2026-10-14/README.md).

Revised October CDR scope is payload acquisition plus PDU-controlled payload on/off, without COMMS or required formal mission modes. CDR command/telemetry uses Pi Zero W F′ TCP over the local network to Pi 5 Ubuntu/Yamcs, with operator browsers on the LAN. The direct stock ComCcsds/TCP and FileDownlink profile passed [local native acceptance](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md) on October 20, with no network mux and independent fake UART channel-2 PDU RPC. Pi runtime and hardware validation remain open. Use the [canonical current plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) for CDR implementation and remaining gates. December FSR adds the complete COMMS/ground chain with the previously planned nominal scheduling/modes and integrated PDU power sequence; the fuller flight-mode model and hardware sequence remain separate work. See [development intent](../planning/DEVELOPMENT_INTENT.md) and [system architecture](SYSTEM_ARCHITECTURE.md).
