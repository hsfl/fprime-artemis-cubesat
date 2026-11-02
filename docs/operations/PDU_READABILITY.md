# PDU commands and readback in Yamcs

The readability slice was deployed and tested on the Pi Zero W and Pi 5 on
October 27, 2026. The matching F′ binary and Yamcs dictionary are required; the
wire protocol and Teensy bridge are unchanged, so this slice requires **no PDU
or Teensy firmware flash**. See the [live deployment record](../verification/evidence/n2-pdu-deployment-2026-10-27/README.md).

## Commanding

For the tested station, open [Yamcs over IPv4](http://192.168.0.203:8090/).
When `.local` resolved to scoped IPv6 on the test computer, command report URLs
failed to decode; the IPv4 report loaded successfully. The address may change
with the LAN configuration.

In Yamcs **Commanding → Send a command**, open
`ArtemisRpiTeensyDeployment_ArtemisRpiTeensyDeployment/` →
`ArtemisRpiTeensyDeployment/` → `epsManager/` and use:

| Command | Arguments / result |
| --- | --- |
| `GET_PDU_INFO` | Firmware version, protocol, build flags and Git prefix in `PduInfoResult` |
| `GET_PDU_STATUS` | Summary plus one `PduSummaryRail` event for each of nine rails |
| `GET_PDU_RAIL` | Select a named rail; receive `PduRailReadback` |
| `GET_ALL_PDU_RAILS` | Read all nine rails; receive nine named readback events |
| `SET_PDU_RAIL` | Select rail, `OFF`/`ON`, and explicit `CONFIRM`; receive validated `PduRailSetResult` |

Example: `rail=RAIL_5V_1`, `desiredState=ON`, `confirmation=CONFIRM`.
`CANCEL` is rejected without transmitting a SET. The SET rail menu contains only
the seven public switched rails. Burn rails are available for reads only; the
reserved output 6, burn activation and ALL are excluded from this SET interface.
Existing numeric EPS commands remain compatible.

## Finding the response

Open **F′ Events** (`/ext/fprime-events`) and filter on `Pdu` or
`epsDriverArtemis` / `epsManager`. The ordinary **Events** page also contains
these events. A successful SET shows the named rail, desired state, actual
readback and `result=OK`, for example:

```text
PDU SET rail=RAIL_5V_1 desired=ON readback=ON result=OK local_request=...
```

`PduRailSetMismatch` is a failure even though a valid reply was received. Its
actual readback is retained. `PduOperationError` distinguishes `DRIVER`,
`TEENSY_RPC` and `PDU`, with a readable reason and the original numeric code.
For example, code 3 in `TEENSY_RPC` means TIMEOUT, while code 3 in `PDU` means
BAD_PARAM. A bare `status=0` means protocol success, not an OFF rail.

Manager result/failure events include the F′ command sequence and opcode.
The driver's `local_request` is an 8-bit diagnostic counter that wraps; it is
not a persistent Yamcs command-history correlation key. Yamcs release/sent
acknowledgments alone do not prove that the PDU applied an operation.
The tested Yamcs report showed `Completion -`; confirm the actual PDU result
with these events and the fresh rail telemetry. If a request is rejected as
`BUSY` during background polling, retry after the in-flight request completes.

## All-rail status

Open **Telemetry → Parameters** and select `epsManager`. The named channels are
`Rail3V3_1`, `Rail3V3_2`, `Rail5V_1`, `Rail5V_2`, `Rail5V_3`, `Rail12V`,
`RailVBatt`, `RailBurn1` and `RailBurn2`:

| Value | Meaning |
| --- | --- |
| `UNKNOWN` | No validated observation of that rail since startup |
| `OFF` | Fresh PDU enable readback is OFF |
| `ON` | Fresh PDU enable readback is ON |
| `STALE` | A previous observation exists, but current readback is unavailable |

Readback is an enable-pin state, **not measured voltage** or proof a payload is
ready. `RailStateBitmap` retains the legacy last value. `RailKnownMask` and
`RailFreshMask` use the nine-rail order above; operators can use the named states
without decoding these masks.

A summary or GET ALL refreshes all rails; a single GET/SET refreshes only that
rail. INFO does not refresh rails. Ages are driver scheduler ticks, normally
two seconds per tick in this deployment. Freshness expires after three ticks
(on the fourth tick), or immediately after a transport/malformed-response
failure. `RailMaximumAgeTicks` is the oldest known observation's age, with zero
before any observation. Check Yamcs acquisition time/status as well: if F′ or
the telemetry link stops entirely, ground-cached ON/OFF values cannot update
themselves to STALE.

## Local QA

Build native CDR and component tests using the repository's F′ workflow, then:

```bash
python3 tools/yamcs/test_pdu_readability.py \
  --binary ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment \
  --dictionary ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json \
  --hold 120
```

The test allocates its own PTY and runs loopback F′/Yamcs with a stateful fake
PDU. It checks INFO, GET, GET ALL, summary, SET ON/OFF, CANCEL, mismatched
readback, malformed reply, timeout, STALE and recovery. Evidence is written to
`yamcs/run/pdu-readability-*/result.json`, alongside application/ground logs.
`--baseline <pre-change-dictionary.json>` additionally compares every existing
command, event and telemetry entry for compatibility. The optional hold permits
browser inspection at `http://127.0.0.1:8090`; the test then cleans up its local
processes. The fake PDU finishes with all rails OFF.

The [October 27 live record](../verification/evidence/n2-pdu-deployment-2026-10-27/README.md)
covers the deployed pair, hardware GUI results, final all-OFF readback, and
rollback paths. For a new hardware session, first verify target identities and
fresh INFO/status/GET ALL, and record final readback after authorized switching.

## Deliberately retained for the next slice

The old `EpsStatusUpdated` cadence is retained because the Pi supervisor uses
that exact event as a liveness signal. Routine polls do not emit nine new
readback events; explicit reads and observed changes do. Old low-level events
remain available. Removing legacy noise requires updating supervisor liveness
and the event-view defaults together. Poll/command contention can still return
BUSY, as observed once in the live check; wait for the active poll/request to
finish, then retry once. Queue/admission changes, full Yamcs command verification,
and a dedicated PDU dashboard remain separate work.
