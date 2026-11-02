# N2 PDU readability: local acceptance

October 26, 2026 HST (October 27 UTC). Local implementation on
`neutron2-develop`; no deployment, firmware upload, physical UART or power
switching. Native F′ uses a newly allocated PTY, real local Yamcs and a stateful
fake PDU. The PDU reference remains `e94ee066442a0cdf980bc62854c8351b2eb0aa04`
(firmware 2.2.2); F′ remains `a750219414881d3461054e73c25a0868a5e648f9`.

## Results

- Native CDR generation/build passed (`NEUTRON2_CDR_TCP=ON`, Darwin arm64).
- All seven project component suites passed: **42 tests**, including manager
  4/4 and driver 5/5. See [component log](component-tests.log).
- [Yamcs acceptance](result.json) passed 17 recorded checks: INFO, single GET,
  all nine GET ALL events, all nine explicit summary events, SET ON/OFF and
  telemetry, CANCEL without SET transmission, malformed GET ALL, mismatched
  SET readback, timeout, all-rail STALE and recovery to all OFF.
- Positive commands produced dispatcher completion events; CANCEL produced
  VALIDATION_ERROR; malformed/mismatch/timeout produced EXECUTION_ERROR. These
  commands ran sequentially and checks matched opcode plus release time. This
  does not implement a general Yamcs command-history verifier.
- Baseline dictionary comparison passed: every existing entry was identical
  across **84 commands, 267 events and 187 telemetry channels**.
- GUI inspection confirmed named rail states, readable SET/GET/error logs and
  seven public switched rails in the SET dropdown. [Status](gui-rails.png) ·
  [Events](gui-events.png) · [Command menu](gui-command.png).

The status/event screenshots are from the first passing local run; the command
menu screenshot and JSON are from the final, stronger acceptance run. Both use
the same binary/dictionary. The test fake finished with all nine rails OFF.

## Tested artifacts

```text
F′ native SHA-256:
c9c8ca3c9cc9868ebcf507acbf69d6078d676c0ef3f002bea96fe4d78c5fd7d0
Dictionary SHA-256:
88b2ec7cb5ea403e9094070691e8358ebcadcda28b19a0c788c2e4c84a719abc
```

The source changes were uncommitted during this test; the matching F′ component
and topology changes were subsequently committed as `5bdbcb3`. Native artifacts are in
`ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/`.
Detailed application/ground logs remain under
`yamcs/run/pdu-readability-20261027T020547Z-f665d3/` (ignored local runtime data).
Reproduce with [test_pdu_readability.py](../../../../tools/yamcs/test_pdu_readability.py)
and the [operator guide](../../../operations/PDU_READABILITY.md).

## Boundaries

No target ARMv6 build/runtime or hardware acceptance is claimed. Deploy the F′
binary and matching Yamcs dictionary together after target validation. This
slice changes no PDU wire protocol or Teensy bridge and requires no PDU or
Teensy flash. Readback means enable-pin state, not measured rail voltage.
Legacy liveness events, poll/command BUSY contention, complete command-history
verification and a dedicated PDU dashboard remain outside this slice.
