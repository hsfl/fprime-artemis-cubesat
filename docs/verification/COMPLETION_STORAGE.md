# N2 Yamcs completion and storage restart plan — October 9, 2026

**Status:** D/E implementation and all six Yamcs completion/storage scenarios PASS on October 9. A/C/B and final pulse regressions also PASS. Dennis authorized implementation and Yamcs-mediated functional acceptance.

## Scope decisions recorded October 9

- Real payload files/sample captures will not be supplied for this work. Use existing synthetic files to verify byte acquisition, integrity, retention, and raw downlink. Do not block this slice on detector data or invent a real payload protocol.
- The payload team provides the science-viewing JAR. FSW owns acquiring the raw science file, storing it, and downlinking its bytes. The JAR owns science interpretation/display. Existing local waveform/spectrum/PSD views remain demonstration/regression fixtures; new FOM, calibration, classification, or replacement science UI is outside this slice.
- Another team is developing SatNOGS/SDR. Preserve the current software transport seam; their physical transport and RF integration are separate work.
- All functional acceptance commands and observations use Yamcs. Builds/static inspection are setup evidence. No hardware, flashing, deployment, EPS switching, or RF operation.

## D — Yamcs completion verification

Expose operation admission and terminal result in actual Yamcs command history, separate from dispatch. For automatic raw delivery, also expose sender completion and independently verified ground receipt as distinct stages. Bind results to the correct command, request/generation, product/transfer, and local run. Reject ambiguous/stale evidence; a released command or sender DONE alone must not mark science delivery complete.

The verified command variants are `scienceApp.VERIFIED_COLLECT_AND_DOWNLINK(verificationToken, durationMinutes)` and `commsApp.VERIFIED_REQUEST_SCIENCE_DOWNLINK(verificationToken)`. Use a unique nonzero token for each verified command. Existing commands remain available with their original dispatch semantics. F′ dispatcher sequence numbers differ from Yamcs command IDs, so explicit tokens avoid time-only correlation. A ground verifier writes Yamcs command-history stages and independently checks raw object and receipt readback.

Acceptance through Yamcs: nominal automatic capture/downlink; failed and timed-out capture; invalid/busy rejection; repeated request IDs/late callbacks; independently downloaded raw bytes matching receipt size/CRC/SHA. Confirm command history stages, final status, and absence of false success on failure or missing receipt. Preserve A/C/B/pulse regressions.

## E — durable storage and restart recovery

StorageManager owns an immutable durable copy of opaque product bytes plus versioned metadata. Publish a product only after finalized storage succeeds. Recover valid products after restart; reject missing/corrupt/incomplete records. Preserve identity across restart and allow selecting an older stored product for raw downlink. Use an explicit isolated storage root for local tests and retain it across process restarts.

Acceptance through Yamcs: capture two distinct products; stop/restart using the same durable root; report both recovered products; select/downlink the older product and independently compare Yamcs bytes; collect after restart without identity overwrite. Exercise corrupt/missing file and incomplete-record rejection. Preserve products after sender completion. Receipt ACK and automatic source reclamation are a later separate phase; this slice retains stored products and rejects full storage rather than silently discarding them.

## Evidence

The final artifacts below record source/dirty-state, executable/dictionary hashes, storage root, run IDs, Yamcs command IDs and returned statuses/events, product/transfer IDs, downloaded raw hashes, restart/fault scenarios, and failures.

Related: [SDD gap plan](../planning/SDD_GAP_PLAN.md), [development intent](../planning/DEVELOPMENT_INTENT.md), [accepted pulse baseline](SYNTHETIC_PULSE_ACCEPTANCE.md).

## Operator contract

For a new capture, issue `scienceApp.VERIFIED_COLLECT_AND_DOWNLINK` with a fresh nonzero `verificationToken` and `durationMinutes`. Inspect its Yamcs command history: `PayloadAccepted`, `PayloadTerminal`, `StorageCommitted`, `SenderComplete`, `GroundReceipt`, and `CommandComplete` each provide `_Status`, `_Time`, and `_Message`. Delivery success requires `CommandComplete_Status=OK`; dispatch or sender success alone is insufficient. Timeout causes remain visible as stage `TIMEOUT` and a final `NOK`, which Yamcs displays as FAILURE. Numeric correlation fields are decimal strings, and stage timestamps are stable UTC ISO strings because the [Yamcs 5.12.8 external history-update implementation](https://github.com/yamcs/yamcs/blob/yamcs-5.12.8/yamcs-core/src/main/java/org/yamcs/commanding/CommandQueueManager.java#L354-L362) accepts STRING values only. Native dispatcher acknowledgments keep their normal Yamcs types.

To retrieve an older product, issue `storageManager.REPORT_STORAGE_HISTORY`, then `storageManager.SELECT_PRODUCT(productId)` and check the returned `ProductSelected` event. Issue `commsApp.VERIFIED_REQUEST_SCIENCE_DOWNLINK` with another fresh token. Its sender, ground-receipt, and final completion stages identify the selected product and transfer. Download the opaque raw object from Yamcs; forward it to the payload team's JAR workflow once that integration is available.

The ground verifier is automatically started and restarted by Yamcs ProcessRunner, using the current run's manifest and persisted verifier state. Only the two VERIFIED command variants receive these completion stages. Legacy commands retain their dispatch behavior. Tokens must be unique within a boot; an operator should keep them unique across sessions as well.

Set `N2_STORAGE_ROOT` explicitly for local work. `local_stack.py` defaults to `yamcs/data/onboard-storage`; onboard code defaults to `/home/pi/artemis/data/science-products`. Use the same root after restart. The catalog holds at most 16 immutable products and rejects new products when full. `REMOVE_OLD_DATASETS(confirm=1)` removes transient simulator files, retaining durable products. Receipt acknowledgment and automatic reclamation are deferred.

## Reproduce the two-run acceptance

From the `neutron_2` checkout, start a pulse stack with an empty isolated storage directory:

```sh
N2_STORAGE_ROOT=/absolute/path/to/isolated-store \
N2_LOCAL_SCIENCE_FAULTS=drop-first-0,corrupt-first-1 \
./tools/yamcs/run_pulses.sh
```

In another terminal, run:

```sh
./tools/yamcs/completion_storage_cycle.sh before-restart \
  --manifest /absolute/path/to/products.json
./tools/yamcs/stop.sh
```

Wait for shutdown, start the stack again with the **same** `N2_STORAGE_ROOT`, then run:

```sh
./tools/yamcs/completion_storage_cycle.sh after-restart \
  --manifest /absolute/path/to/products.json
```

The manifest records command-history entries and independent Yamcs raw-object verification. Each probe also saves a PASS/FAIL JSON artifact in its current run directory. Additional `missing-receipt`, `storage-fault`, and `verifier-restart` phases require deliberately isolated fault fixtures; see the script's `--help` and the acceptance record below.


## October 9 acceptance record

Every functional assertion below uses Yamcs commands, returned events/parameters, actual command history, or independently downloaded Yamcs objects. Filesystem edits only construct isolated corruption/full-storage fixtures; native logs are diagnostic evidence, not functional acceptance.

| Scenario | Result and observed behavior | Yamcs evidence |
| --- | --- | --- |
| Capture before restart | PASS: two raw products delivered; invalid, busy, duplicate-token, failed and timed-out operations never acquire false completion. | [before restart](evidence/n2-software-2026-10-09/20261009-130131/completion-storage-before-restart-3b4bdd24ba9d.json) |
| FSW process restart | PASS: products 2/3 recovered; older product 2 downlinked with identical size/CRC/SHA; new product 4 retained with a fresh ID. | [after restart](evidence/n2-software-2026-10-09/20261009-130240/completion-storage-after-restart-e20707ff693f.json) |
| Ground verifier restart | PASS: Yamcs ProcessRunner restarted the verifier during an admitted command; acceptance time and command identity survived; final delivery succeeded. | [verifier restart](evidence/n2-software-2026-10-09/20261009-130425/completion-storage-verifier-restart-5763e83ec232.json) |
| Damaged storage recovery | PASS: corrupt product 2, missing product 3 bytes, incomplete record 100 rejected; intact product 4 delivered; orphan ID 101 avoided and new product 102 delivered. | [storage faults](evidence/n2-software-2026-10-09/20261009-130515/completion-storage-storage-fault-6889a41095e0.json) |
| Full storage | PASS: exact 16-entry catalog through Yamcs; new capture rejected; all prior products retained and an intact older product delivered. | [full storage](evidence/n2-software-2026-10-09/20261009-131319/completion-storage-storage-fault-d7ee06308a89.json) |
| Missing ground receipt | PASS: payload/storage/sender stages OK; GroundReceipt TIMEOUT; final CommandComplete NOK; native Yamcs Completion FAILURE. | [missing receipt](evidence/n2-software-2026-10-09/20261009-131403/completion-storage-missing-receipt-44a691ef61d4.json) |

Product 2 was independently downloaded before and after restart: 902 bytes, CRC16-CCITT 58578, SHA256 `319b61ec8a258cba0cde05d554bc1dfaba3adb9f477b22375675f5fa6e358286`. Product 3 was 1694 bytes, CRC 23933, SHA256 `2a8d25f02427e6898137039cd33c1b2f0498221e84966472b6798a4befe1b698`. The [two-run product manifest](evidence/n2-software-2026-10-09/acceptance-completion-storage-20261009-4be9e7/final-products.json) includes exact command and receipt bindings.

The same native build passed legacy regressions: [A pass-through](evidence/n2-software-2026-10-09/20261009-125729/passthrough-dfd02b8a9ae8.json), [C lifecycle: 31 checks](evidence/n2-software-2026-10-09/20261009-125729/lifecycle-b20592f3c52c.json), and [B raw capture/downlink](evidence/n2-software-2026-10-09/20261009-125729/raw-cycle-21d8ba4ca098.json), including configured dropped/corrupt frames. The [final pulse regression](evidence/n2-software-2026-10-09/20261009-131453/pulse-cycle-b6dd5adf0f29.json) and repeated A pass-through also PASS with the final verifier source. The [acceptance summary](evidence/n2-software-2026-10-09/acceptance-completion-storage-20261009-4be9e7/acceptance-summary.json) lists all final artifacts.

Native build identity: branch `neutron2-develop`, HEAD `33aef0a186205e27d0a05be5a570614ef223de8c` plus preserved local changes; F′ submodule `a750219414881d3461054e73c25a0868a5e648f9`. Executable SHA256 `80fe0b51bce96f82ccabb8ad17ecc6e004f4e616ef799b66a0fe2df10b89402a`; dictionary SHA256 `752c1726eb43b46a82ccf16210b4bcd1a8a6097aa4aac6e64afc28685216fda0`. The [source fingerprint](evidence/n2-software-2026-10-09/acceptance-completion-storage-20261009-4be9e7/implementation-source-sha256.json) records the final implementation and harness hashes; each stack manifest records its verifier hash and exact dirty state. The [native build log](evidence/n2-software-2026-10-09/acceptance-completion-storage-20261009-4be9e7/native-build.log) passed.

The first full-catalog probe received 14 of 16 report events through the local event bridge and stopped. The probe now records at most three fresh explicit Yamcs reports and requires a single complete exact-count/ID/metadata report; partial reports are never merged. The final full-storage run passed on its first report. Earlier harness failures and their logs remain in the [matrix progress record](evidence/n2-software-2026-10-09/acceptance-completion-storage-20261009-4be9e7/final-matrix-progress.json); they are not represented as successful acceptance. Earlier lifecycle expectations were updated for the durable catalog, and separate fresh runs avoided cached mode telemetry from a previous downlink.

Native Yamcs UI captures: [verified delivery SUCCESS](evidence/n2-software-2026-10-09/acceptance-completion-storage-20261009-4be9e7/final-yamcs-verified-completion.png) and [missing receipt FAILURE](evidence/n2-software-2026-10-09/acceptance-completion-storage-20261009-4be9e7/final-yamcs-missing-receipt.png). Stable stage timestamps and final NOK mapping were verified through the UI as well as archived command history.

This establishes software process-restart recovery on the host filesystem. It does not establish power-loss qualification, real PLD acquisition/protocol compatibility, payload-JAR interpretation, Pi/iOBC deployment, or hardware/RF operation. ACK-based reclamation, automatic retention policy and power-loss testing remain future work. The real payload adapter remains an interface integration step when its public contract is available; a real detector sample is not required to complete this raw-byte software slice.

October 9 historical handoff: native build, Python syntax checks, shell syntax checks and `git diff --check` passed. Independent source/evidence review found no blockers. Changes remain local and uncommitted on `neutron2-develop`; prior work is preserved. The local stack was stopped after acceptance and UI capture, with ports 8090/8063 confirmed closed.

## October 14 baseline packaging

Selected acceptance records are now versioned under [the evidence snapshot](evidence/n2-software-2026-10-09/README.md); original local run files remain untouched. See [the team handoff](SOFTWARE_BASELINE.md) for current packaging checks and any post-acceptance corrections. The historical source fingerprints above identify the October 9 implementation, not later corrections.
