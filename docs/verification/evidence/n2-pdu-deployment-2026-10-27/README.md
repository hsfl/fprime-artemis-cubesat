# N2 PDU readability deployment — October 27, 2026

Deployed and tested on the two physical Pis. All nine PDU enable readbacks were
OFF at the final GET ALL, 12:11:06 HST. This is UART/RPC/PDU functional evidence;
it is not measured rail voltage, payload operation, RF, or flight qualification.

## Deployed pair

- Source: `neutron2-develop`, HEAD `a99e22e05874f1a175aa0ac82dd71c8a13641d1f`
  **plus the uncommitted PDU readability changes**. The manifest records source
  hashes; commit `5bdbcb3` preserves the matching component/topology changes. The new
  operator guide and local test script have hashes in the manifest too.
- Release: `a99e22e-pdu-readable-20261027`.
- Zero W: `n2-obc.local`, `192.168.0.152`, account `n2-obc`, ARMv6l.
- Pi 5: `192.168.0.203` Ethernet / `.204` Wi-Fi, account `n2-fprime-yamcs`.
- Binary SHA256:
  `e498eb77cc8b01434cd2bbf5290d328d46d51ea6eb08186290d51e8cd26c6a2f`.
- Dictionary SHA256 on laptop, OBC and ground:
  `51968b0cfb73320d9ab8c6eabcc9d55038576def6179651400c3f60f3985ef1f`.
- Profile: CDR TCP ON, LOCAL_YAMCS OFF, Release, ARMv6 v6KZ / VFPv2,
  `/lib/ld-linux-armhf.so.3`; target `ldd` resolved every dependency.
- All existing 84 commands, 267 events and 187 channels matched the previously
  deployed dictionary entry-for-entry.

The maintained Docker ARMv6 script ran with `--local-only`, reusing the verified
CDR build cache and sysroot. New binary, dictionary and manifest were staged and
hash-checked on the OBC before selection. The dictionary was backed up and
atomically replaced on the ground. Both ground services restarted to reload it.
The OBC `current` symlink was atomically switched; terminating only its old
application child let the existing supervisor/systemd policy start the new pair.
No sudo policy, systemd units, OS configuration, supervisor or persistent state
was changed. No PDU or Teensy firmware was flashed.

The restart exposed the existing TCP TIME_WAIT delay: port 50000 initially
returned bind status -9, then reopened and reconnected at 12:07:52 HST. The
supervisor became ready. The OBC restart counter rose once for the planned
restart; each ground counter rose once (1 to 2), then remained stable throughout
the test. The prior release is retained.

## Live GUI acceptance

Every operation below was sent using Yamcs **Send a command**, not a direct PDU
serial utility. Returned events were inspected in **F′ Events**, and named rail
channels in **Telemetry → Parameters → epsManager**, filtered by `Rail`.

| Check | Observed result |
| --- | --- |
| GET_PDU_INFO | Protocol 2, firmware 2.2.2, flags 1, Git `e94ee066442a` |
| GET_PDU_STATUS | Nine named `PduSummaryRail` events, all OFF, sequence 1 |
| GET_ALL_PDU_RAILS | Nine named readbacks, all OFF, local request 54 |
| SET 3V3_1 ON with CANCEL | Rejected INVALID_OR_CANCELLED, VALIDATION_ERROR |
| First confirmed ON attempt | Rejected BUSY during background polling; no successful SET |
| Retry SET 3V3_1 ON | Desired ON, readback ON, result OK, local request 110 |
| Independent GET 3V3_1 | ON, local request 116; named telemetry also ON |
| SET 3V3_1 OFF | Desired OFF, readback OFF, result OK, local request 122 |
| Independent GET 3V3_1 | OFF, local request 127; named telemetry also OFF |
| Final GET ALL | Nine named OFF readbacks, local request 135 |

Successful operations also returned F′ `OpCodeCompleted`; CANCEL and BUSY
returned the corresponding `OpCodeError`. `result.json` records 13 assertions
against the captured target journal, service states and deployed hashes.
`RailFreshMask=511`, `RailKnownMask=511`, bitmap 0 and all nine named states OFF
were visible in the GUI. Burn rails were read only. Only 3V3_1 was switched.

The previous [local evidence](../n2-pdu-readability-2026-10-26/README.md) remains
the evidence for malformed replies, timeout, STALE and recovery; those faults
were not injected into the physical system here.

## Operator URLs and known limitations

Use [Yamcs over IPv4](http://192.168.0.203:8090/) for commanding. The `.local`
name selected link-local IPv6; its command IDs included a scope suffix `%3`.
Opening those reports failed with `URLDecoder: Illegal hex characters in escape
(%) pattern`. Repeating read-only GET_PDU_INFO over IPv4 produced a fully loaded
report with Queued, Released and Sent OK. This workaround needs no server or
network configuration change. Existing IPv6-origin history entries still have
the report URL issue.

Yamcs report completion remains `-`: general PDU terminal verification in
command history is a deferred feature. Sent OK alone does not prove PDU success;
use fresh `PduInfoResult`, `PduRailSetResult` / `PduRailReadback`, named telemetry
and F′ terminal events. Poll contention can still return BUSY. Legacy polling
noise remains because the installed supervisor uses `EpsStatusUpdated` for
liveness.

## Recovery

Prepared before selection:

- OBC: `/home/n2-obc/artemis/pdu-backup-20261027/rollback.py`, run as `n2-obc`.
  Restores the prior `current` link and terminates the current application child
  so the existing supervisor restarts the prior release.
- Ground: `/home/n2-fprime-yamcs/yamcs-station/pdu-backup-20261027/rollback.sh`,
  run as `n2-fprime-yamcs`. Restores the prior dictionary and restarts the owned
  Yamcs and bridge processes through their existing Restart=always policy.

The full prior ground config was also copied into that backup directory. No
rollback was necessary or exercised. Recover both sides together if rolling
back this deployment.

## Evidence files

- `build-manifest.json` and ELF/readelf outputs: artifact identity.
  `readelf-l.txt` has trailing spaces normalized; values are unchanged.
- `obc-journal.log`: deployment, terminal commands and actual PDU response events.
- `obc-status.txt`, `ground-status.txt`: live processes, hashes and service states.
- `result.json`: bounded acceptance assertions.
- `gui-info.png`, `gui-summary.png`: human-readable version and all-rail events.
- `gui-named-command.png`, `gui-set-results.png`: named selection and ON/OFF results.
- `gui-rail-on.png`, `gui-final-rails.png`: actual ON and final all-OFF telemetry.
- `gui-ipv4-report.png`: verified report-page workaround.

## Publication checkpoint — October 27

The deployed F′ component/topology source hashes match commit `5bdbcb3`.
The build manifest retains the original pre-commit HEAD and dirty-source identity;
committing the sources does not change the deployed binary/dictionary hashes.
Operator documentation was updated afterward to reflect this acceptance. The
original build-time patch is retained locally at ignored
`yamcs/run/pdu-deploy-20261027/source.patch`; the published source commit and
per-file manifest provide portable implementation provenance.

Before publication, all seven component suites (42 cases) passed again, the CDR
Teensy host smoke passed, the Teensy 4.1 CDR firmware compiled successfully, and
Python/shell syntax checks passed. No hardware operation or firmware upload was
performed during commit preparation. Build products and caches remain ignored.
