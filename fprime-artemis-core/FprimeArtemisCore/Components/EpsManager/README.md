# Direct PDU manager

Milestone M3 direct-PDU slice, implemented 2026-11-11 (HST).

`EpsManager` uses the PDU's framed UART v2 directly through a synchronous byte
stream driver. It does not reuse the old Pi→Teensy channel-2 proxy. The owning
header is `external/artemis-pdu/src/pdu_protocol_v2.h`; opcodes, statuses, output
IDs, frame sizes, capabilities, and timing limits come from that header.

## Integration

Register `Types/Eps.fpp` and the component. Wire:

| Manager port | Driver connection |
|---|---|
| `drvSendOut` | UART `send` |
| `drvReceiveIn` | UART `recv` |
| `drvReceiveReturnOut` | UART `recvReturnIn` |
| `drvConnected` | UART `ready` |

Provide the UART's bounded allocation/return pool as for GPS. Schedule `run` at
100 Hz and call `configure(10, 2000)`: 10 ms per run and a 2 s automatic summary
poll. A zero polling interval disables background polling for tests. The guarded
ports/commands serialize state; no heap allocation or extra worker is used.
The Pi-enable GPIO remains owned by `RpiPowerManager`, independent of PDU rails.

## Commands and state evidence

- `PING`: succeeds after a valid, correlated version reply.
- `GET_SUMMARY_STATUS`: reports the nine-output bitmap, reset cause, fault bits,
  uptime, and capability bitmap. `knownOutputs` marks which bitmap values have
  recent readback; a zero bit means UNKNOWN, not OFF.
- `SET_OUTPUT_STATE(outputId, state)`: permits only ordinary switched rails.
  Burn-wire, reserved 5V4, H-bridge, and all-output IDs fail validation. Command
  OK requires a matching output ID and requested post-command GPIO readback.
  A valid mismatched readback remains a known observation but fails the command.
- `POWER_CYCLE_OUTPUT(outputId, offTimeMs)`: validates a normal rail and the
  firmware's 1–10000 ms bound. The initial reply proves only that the output is
  OFF. The manager waits, queries `GET_OUTPUT_STATE`, and reports command OK /
  `CycleRestored` only after observing ON. It polls briefly if restoration is
  delayed and fails with unknown/observed status rather than assuming success.

One operation is pending at a time; competing commands receive BUSY. Public
command responses are delayed until observed completion or bounded failure.
The byte client verifies CRC, version, response type, opcode, sequence, and
length. It bounds frames to the shared maximum, times out a partial frame after
100 ms, and times out a response after 1000 ms. A timed-out or failed transport
clears known output state. Individual observations become unknown after 5 s
without refresh. The driver receives its buffer back on every path.

Telemetry contains communication state, busy state, output values/known mask,
reset/fault/uptime/capabilities, CRC/framing/timeout counters, and the last wire
status. It does **not** invent current, voltage, temperature, or state of charge.
GPIO readback confirms a logical PDU state; it is not measured rail voltage.

This slice does not add burn, torque, charger, or protective-abort commands. A
cycle follows the actual firmware's existing serialized timed-operation rules.

## Validation and evidence

From this component directory on macOS:

```sh
cmake -S test -B /tmp/artemis-pdu-client-build \
  -DPython3_EXECUTABLE="$(pwd)/../../../fprime-venv/bin/python"
cmake --build /tmp/artemis-pdu-client-build -j4
ctest --test-dir /tmp/artemis-pdu-client-build --output-on-failure
```

Use the equivalent shell/project Python under WSL2 on Windows. No hardware is
opened. `pdu_client_contract` covers framing, CRC, length bounds, wrong
version/type/opcode/sequence, busy results, timeout/wrap, maximum payloads, and
rail policy. `pdu_actual_firmware_interop` sends production client frames to the
existing twin host wrapper built from the actual `app.c` and `pdu_packet.c`.
It verifies GPIO readback, timed restoration, firmware BUSY, bad-CRC rejection,
partial-frame recovery, and the host's modeled watchdog reset.

Observed: **2/2 portable/actual-firmware tests pass**, and the same **2/2 pass
with AddressSanitizer and UndefinedBehaviorSanitizer**. Strict Cortex-M7 C++14 compilation passes with 1,785 bytes of
unoptimized text and zero object data/BSS. FPP syntax and status-type generation
pass. **3/3 registered F′ component tests pass** in the ARM64 Linux Docker
toolchain, with framework address/undefined/leak sanitizers enabled. They cover
command completion, busy handling, observed-versus-requested state, cycle
restoration, CRC timeout, and staleness. The tested target is
`FprimeArtemisCore_Components_EpsManager_ut_exe`; its build uses `BUILD_TESTING=ON`
and keeps framework/autocoder UT targets disabled. It was built in the separate
`artemis-eps-native-ut` Docker volume, without modifying the shared FC/Pi caches.

Hardware UART, actual watchdog latency, voltage behavior, and target heap/stack
acceptance are not established by these host checks.
