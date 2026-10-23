# N2 local Yamcs software test runbook

This runbook starts the current `neutron2-develop` native F′ deployment and Yamcs on one computer. The Artemis hardware is a proxy in this demonstration: F′ runs on the host, and software-only pseudo-terminals model the UART path. Functional checks go through Yamcs HTTP or its web UI. This is not board, flight, or hardware-in-the-loop evidence. macOS arm64 has been exercised; Linux/WSL has not.

## Prerequisites and checkout

For the future shared Ubuntu Pi 5 station, see the [dedicated ground-host plan](../integration/GROUND_HOST.md). This runbook remains the single-host simulated-spacecraft setup; its `run.sh` is not a hardware ground-only launcher. The [Yamcs primer](../operations/YAMCS_BASICS.md) explains the server, browser, dictionary, and transport roles.

Use Python 3.11 and a working native C++ build toolchain. From the parent workspace, a fresh checkout and the two required submodules are:

```sh
git clone --branch neutron2-develop https://github.com/hsfl/fprime-artemis-cubesat.git neutron_2
cd neutron_2
git submodule update --init --recursive ArtemisRpiTeensy_N2/lib/fprime external/artemis-pdu
```

The superproject pins F′ at v4.2.1, commit `a750219414881d3461054e73c25a0868a5e648f9`, and `external/artemis-pdu` at `e94ee066442a0cdf980bc62854c8351b2eb0aa04`. Keep those gitlink revisions; do not move F′ to the separate v4.3 study checkout. The current PDU submodule commit is on `codex/n2-cdr-pdu` (firmware 2.2.2).

Yamcs dependencies are isolated under `tools/yamcs/.venv`; native F′ Python dependencies are isolated under `ArtemisRpiTeensy_N2/fprime-venv`. The branch's direct Yamcs pins are:

| Package | Pin |
| --- | --- |
| `fprime-yamcs` | `0.2.4` |
| `fprime-gds` | `4.4.0` |
| `fprime-xtce` | `0.1.5` |
| `fprime-yamcs-bundle` | `5.12.8.0` |
| `fprime-jre` | `25.0.4.1.0` |
| `yamcs-client` | `1.12.1` |
| `PyYAML` | `6.0.2` |

`tools/yamcs/requirements.lock` pins the resolved transitive dependencies. No global `fprime-material` checkout is needed. The software baseline is described in [the team handoff](../verification/SOFTWARE_BASELINE.md). Use a checkout containing that baseline; the July branch tip alone does not contain these tools. Repository access may require your GitHub authentication.

## Build and start

Run these from the `neutron_2` repository root. Setup and build steps are explicit; they create only ignored local environments and build outputs.

```sh
./tools/yamcs/bootstrap.sh
./tools/yamcs/build_native.sh
./tools/yamcs/run.sh
```

Leave `run.sh` in the foreground. It starts Yamcs first, waits for the instance, then starts the native F′ app through a software UART-mux/CCSDS adapter. Ground-first startup preserves boot telemetry instead of displaying cached values from a prior app run. The adapter adds/removes the existing UART envelope, forwards command bytes unchanged, and delegates TC/TM framing to F′ and the upstream bridge. It does not emulate RFM23BP packetization. It writes a run manifest, copied working config, and logs below `yamcs/run/<timestamp>/`. Build artifacts, venvs, Yamcs data/logs, caches, and run directories are ignored by Git.

`build_native.sh` explicitly sets `NEUTRON2_LOCAL_YAMCS=ON`. This opt-in configuration uses the framework's 512-byte communication buffer and 1024-byte TM framing defaults. Its project-owned `LocalYamcsConfig/FpConstants.fpp` derives command-string capacity from the available command-argument buffer and keeps `FW_FIXED_LENGTH_STRING_SIZE` consistent; both generated string constants are 504 bytes. Individual FPP command declarations also have their own string storage sizes. The host UART envelope uses its U16 length range, rather than the old 220-byte proxy setting. No custom 40-byte string check or radio packet-size policy is imposed by the test tools. These are local software configuration choices, not a SatNOGS specification.

The CMake option defaults OFF for the existing hardware configuration; select `-DNEUTRON2_LOCAL_YAMCS=OFF` explicitly when returning an existing build cache to that configuration. Do not deploy this host-testing artifact to a board. The local build does not establish Pi memory/timing behavior or real hardware interface support.

Open <http://127.0.0.1:8090>; use Yamcs instance `fprime-project` and processor `realtime`. The local stack binds TCP 8090 for Yamcs HTTP/UI and UDP 50000 (TM frames into Yamcs), 50001 (commands out to the software bridge), and 50002 (split telemetry packets into Yamcs). The launcher checks that these loopback ports are free before starting. Do not start a second stack while the current one owns them.

The active branch dictionary is generated from the current topology and uses `missionApp` (for example, `ArtemisRpiTeensyDeployment.missionApp.PING`). The frozen branch's `missionManager` dictionary is stale for this run. The tracked `yamcs/config/mdb/fprime.xtce.xml` is an intentional minimal placeholder: `fprime-yamcs` converts the JSON dictionary passed by the launcher and generates the working XTCE file in the ignored run config before Yamcs starts. Do not treat or edit the placeholder as the active runtime dictionary.

## Smoke check through Yamcs

With the foreground runner still active, use a second terminal from the repository root:

```sh
./tools/yamcs/smoke.sh
```

The script waits for the Yamcs instance, resolves the current `missionApp.PING` command from Yamcs's MDB, posts it through the Yamcs HTTP API, and waits for the matching `missionApp.Pong` archive event. It saves a JSON record under the active `yamcs/run/<timestamp>/` directory. The smoke check does not use the GDS UI or a direct serial connection; use the Yamcs web UI or HTTP API for other functional tests as well.

To stop this checkout's recorded stack, press Ctrl+C in its foreground terminal or run `./tools/yamcs/stop.sh` from another terminal. The helper checks the recorded PID's command path before signaling it. A deliberate restart creates fresh native application state while retaining the local Yamcs archive; the probe checks fresh reception/acquisition times so archived successes do not satisfy a new run.

## Mode report check through Yamcs

With the foreground runner still active and no other operator commands in flight, from a second terminal at the repository root:

```sh
tools/yamcs/.venv/bin/python tools/yamcs/report_mode.py
tools/yamcs/.venv/bin/python tools/yamcs/report_mode.py --expect-mode BASE
```

The script sends the read-only `missionApp.REPORT_MODE` command through the Yamcs HTTP API. It then checks the Yamcs event archive for exactly one `missionApp.ModeReport` answer, plus the stock `CdhCore.cmdDisp` `OpCodeDispatched` and `OpCodeCompleted` events for its opcode. It prints the reported mode and saves a JSON record under the active `yamcs/run/<timestamp>/` directory. `--expect-mode` also requires a specific mode; a freshly started stack is in `BASE`. For the CDR TCP stack ([CDR_TCP.md](../operations/CDR_TCP.md)), whose launcher does not write `yamcs/run/current.json`, add `--dictionary <matching dictionary>`; the record then goes to `yamcs/run/report-mode-<time>/` unless `--run-dir` is given. Operators can send the same command from the Yamcs web UI Commanding page; the answer appears under Events as `Mission mode is BASE`. This is local software evidence only.

## Existing collection/file compatibility probe

On a freshly started, otherwise idle local stack, run:

```sh
tools/yamcs/.venv/bin/python tools/yamcs/demo.py
```

This diagnostic issues `scienceApp.CONFIGURE_CAPTURE_DURATION(1)` and `missionApp.SCHEDULE_COLLECTION(2)` through Yamcs. It checks a fresh zero collection baseline, configured duration, capture product identity, storage count/bytes, SCIENCE_READY, and product ID/CRC telemetry. It then issues stock `FileHandling.fileDownlink.SendFile` through Yamcs, downloads the reconstructed object from the `fprimeFilesIn` bucket through Yamcs's [Get Object API](https://docs.yamcs.org/yamcs-http-api/buckets/get-object/), and compares independent received bytes against the source integrity oracle. The source is never uploaded/copied into the bucket by this test.

Fresh parameter checks require both generation and acquisition timestamps from the current run. `CollectionCount` is emitted on change, so its initial zero must be fresh since app startup; changing capture duration does not refresh that unchanged counter. After scheduling, the probe waits for the expected final values rather than accepting an intermediate fresh mode such as COLLECTING. Events and telemetry arrive separately.

Use no concurrent operator commands during the single-collection probe. It locks other probe invocations and checks source/count identity again after transfer. Its source read is test-oracle instrumentation, not an operator retrieval path. The current custom mission science channel-1 route is outside this probe; stock file retrieval does not yet close the mission integration, failure, timeout, retention, or return-to-standby gates.

## Recorded evidence

The ignored run `yamcs/run/20261007-091554/` records successful Yamcs PING/Pong exchanges for tokens `10070917`, `10071042`, and `10071101`, plus an acquired `missionApp/ModeHeartbeat` value of 240 in `heartbeat-10071042.json`. Its manifest identifies the software-only PTY transport and records the exact binary and dictionary hashes for that run.

The first collection probe succeeded (`productId=1`, one row, 26 bytes), but the old RF MVP configuration blocked file retrieval: the long-path command exceeded a 128-byte TC frame, and a later 48-byte path reached F′ but returned FORMAT_ERROR because its command-string parser was configured for 40 bytes. These are distinct configuration constraints. Dennis requested removing the added size policy and adjusting FPP constants; the final local build uses the opt-in configuration above. Earlier failures remain in the ignored run artifacts, including `collection-stock-file-fdb01d6a4e8d.json` and `collection-stock-file-a6958d3b8583.json`; they are not file-receipt proof.

The final opt-in native build passed on macOS arm64. The clean run `yamcs/run/20261007-100058/` started at `2026-10-07T20:00:58.172346Z` (10:00:58 HST) using software-only PTYs, CCSDS spacecraft ID 68, and the configuration above. Its manifest records:

| Artifact | SHA-256 |
| --- | --- |
| Native executable | `f887a737e10b2031af90996bc031aa34117d1dbe3c5159c9d7a37800a13f11a8` |
| Generated JSON dictionary | `afd872e2c34ecfb16d0c9d842da9852b44becef981d525ea54767092d969b13b` |

- **Ping PASS:** token `10071601` returned the matching `missionApp.Pong`, count 1, through Yamcs. Evidence: `ping-10071601.json` in that run.
- **Nominal collection + stock file retrieval PASS:** one-second simulated capture, product ID 1, one CSV row / 26 bytes; SCIENCE_READY and collection count 1 acquired through Yamcs. The SendFile command used the actual 86-byte source path, without shortening it for the old 40-byte configuration. Evidence: `collection-stock-file-497a281c8ec2.json`.
- **Independent receipt PASS:** Yamcs reconstructed `n2-local-497a281c8ec2.csv` in `fprimeFilesIn`. Its Get Object response was saved separately as `downloaded-n2-local-497a281c8ec2.csv`, matching source size, CRC-16/CCITT `45394`, and SHA-256 `210b3f4e188414be78fe12ff01285d994a0e5e88f0a6cd125c7f6648204fbc20`. The received bytes were obtained from Yamcs, not copied into its bucket.
- **Independent read-only recheck PASS:** a separate validator observed RUNNING, freshly generated/acquired `missionApp.ModeHeartbeat=180`, and HTTP 200 for the same object with the same 26-byte response/hash. Evidence: `readonly-rest-verification.json`.

This closes local connectivity and the nominal stock-file compatibility gate. It does **not** close the integrated mission science channel-1 route, opaque opcode-string facade, negative collection cases, automatic receipt/retention policy, return to standby, target timing, or real hardware gates. The existing mission remains in SCIENCE_READY after this diagnostic. Restart the stack before repeating the single-collection probe.

For unresolved mission/interface decisions, see [open decisions in the N2 development intent](../planning/DEVELOPMENT_INTENT.md#open-decisions-and-questions).

## October 8: synthetic pass-through acceptance

After a local-profile native rebuild, `./tools/yamcs/passthrough.sh` passed in `yamcs/run/20261008-123909/`. The new command is `payloadManager.SYNTHETIC_PASSTHROUGH(requestId, literalCommand)`. PayloadManager forwards the literal through typed ports; PayloadDriver_NeutronSim echoes it with `SIMULATED_ECHOED`; the manager emits the correlated `SyntheticPassthroughResponse` event. The handler does not interpret opcodes, collect science, or change modes.

The synthetic interface supports up to 128 printable ASCII bytes in the local profile; embedded NUL and non-ASCII are outside the demonstrated contract. This bound is distinct from Aris's future protocol. The default OFF/RF profile still has a 40-byte command-string capacity and was not rebuilt or exercised for this acceptance.

- Automated Yamcs command/return PASS: exact 59-byte punctuation/space/quote string and exact 128-byte boundary string, with matching request IDs. Mode remained BASE; no mode-change events followed. Evidence: `passthrough-f4b2b4466617.json`.
- Manual UI replay PASS: request `1008001`, literal `DEMO ONLY: opaque text with spaces [raw] = test`; independent read-only Yamcs API check confirmed exact response, correlation, status, and BASE mode. Evidence: `operator-ui-verification.json`.
- Running binary and dictionary hashes matched the fresh run manifest. The launcher now rejects dictionary constants inconsistent with the local profile and records source SHA/dirty state. Wildcard CORS was removed from the local configuration.
- The first build attempt stopped on a missing generated constants-header include; it was added, and the subsequent native build passed. Python syntax and `git diff --check` passed.
- Startup telemetry string-decoding (`42>35`) and `PrmIdNotFound` (`0x10007000`) warnings remain unresolved; the pass-through fields and mode decoded successfully. This evidence does not establish that every telemetry item is clean.

Only after the automated demo passed were the five screenshots captured for the [operator walkthrough](../operations/walkthroughs/PASSTHROUGH.md). This records slice A software evidence. C was subsequently accepted below and B in the later record; neither adds evidence for Pi/PLD runtime, SPI/radio, or CDR hardware acceptance.

## October 8: correlated lifecycle acceptance (C)

The final local-profile rebuild passed on macOS arm64. On the fresh, otherwise idle stack `yamcs/run/20261008-132335/`, these commands were run from a second terminal:

```sh
./tools/yamcs/passthrough.sh
./tools/yamcs/lifecycle.sh
```

The recorded A regression `passthrough-ee7f959c9f13.json` passed. The C probe `lifecycle-4467b883d409.json` passed **all 31 checks**, using Yamcs commands and returned events/telemetry only. It proved shared admission across pass-through and five capture entries, immediate busy/invalid/unavailable rejection, explicit failure and timeout recovery, stale replies after external-ID reuse, duplicate suppression, hung-worker cleanup followed by responsiveness and successful capture, and exactly one terminal event per admitted generation. Failed/timed-out/busy captures produced no science-ready/storage success; duplicate capture results incremented counters once. Existing capture/mode/scheduling routes were exercised for regression coverage, not December FSR acceptance.

`PayloadOperationStatus(requestId, generation, kind, state)` is the operation authority: admitted requests receive `ACCEPTED` and one `COMPLETED`, `FAILED`, or `TIMED_OUT`; immediate rejection uses generation `0` and `BUSY`, `INVALID`, or `DRIVER_UNAVAILABLE`. Command transport acknowledgments or F′ opcode completion are not collection completion. Reusing an active external ID is busy; reusing it later allocates a new internal generation. `PayloadReplyIgnored` reason `1` identifies stale replies and reason `2` duplicates.

`CONFIGURE_SIMULATION(faultMode, delayMs, timeoutMs)` configures one local-only fault for the next admitted operation and then reverts to `NORMAL`. Modes are `NORMAL`, `FAIL`, `NO_RESPONSE`, `DELAYED_RESPONSE`, `DUPLICATE_RESPONSE`, `HANG_CAPTURE`, and `DRIVER_UNAVAILABLE` (the last fixture is consumed as an immediate rejection). Delay is bounded to `0–8000` ms, timeout to `1000–8000` ms; configuration while active/unavailable is rejected. Deadlines are checked on replies and by the two-second scheduler. A child at its deadline receives TERM, then KILL after a 200 ms grace if necessary, and is reaped without blocking the component. Yamcs observed `CaptureWorkerReaped` with timeout exit status `124`, fresh driver availability, and a subsequent successful capture within the probe's recovery checks. The child-reap observation bound was eight seconds; fresh availability was observed within its ten-second wait. This is observed host recovery, not a target real-time guarantee.

The scoped ScienceApp unit suite passed **8/8** (`scienceapp-ut.log` in the run). Independent read-only source/evidence review passed bounded C acceptance. The running executable and dictionary hashes matched the manifest:

| Artifact | SHA-256 |
| --- | --- |
| Native executable | `3d2192e159fc905398860ed9ca5a5aadf63084a5a9049a4b64bdfe87a5d7f41f` |
| Generated JSON dictionary | `362ecc8d8568094b10c384658802e295c02ff9d5337d212e602331a5fe8f7581` |

After this final PASS, the UI replay timed out request `10081003` (generation `20`) and completed request `10081004` (generation `21`) with exact `C recovery demo` echo on the same stack. Its independent Yamcs API verification is `operator-lifecycle-ui-verification.json` — PASS, mode BASE unchanged. All five screenshots in the [C operator walkthrough](../operations/walkthroughs/LIFECYCLE.md) were captured after automated acceptance.

Restart into a fresh application before repeating `lifecycle.sh`: it requires zero initial collection/storage counters and no concurrent operator commands. Archive retention is intentional; freshness and generation checks exclude prior-run evidence. Earlier failed probes remain under their run directories; the final 31-check record supersedes them. Startup string-decoding (`42>35`) and `PrmIdNotFound 0x10007000` warnings remain. Arbitrary malformed/large child-output stress and the default OFF/hardware profile were not exercised. B has a separate acceptance record below; this C run itself does not establish B or the Pi/PLD, SatNOGS/SPI/RF, or full CDR/FSR hardware path.

## October 8: raw-data cycle acceptance (B)

The automated B probe passed on macOS arm64 in the fresh software-only run `yamcs/run/20261008-135910/`, with run nonce `aa83d80ef47f`. It issued `scienceApp.COLLECT_AND_DOWNLINK(durationSeconds)` through Yamcs, stored validated simulator products, delivered their bytes over custom channel 1 through software PTYs, reconstructed them with the local ground receiver, and verified raw objects and receipt manifests through Yamcs GET. The [full acceptance record](../planning/SDD_GAP_PLAN.md#slice-b-acceptance-record-2026-10-08) contains the per-product CRC/SHA, test matrix, and matching build/dictionary evidence; the [operator walkthrough](../operations/walkthroughs/RAW_CYCLE.md) records the UI replay and screenshots.

| Capture | Product / transfer | Bytes | Ground display |
| --- | --- | ---: | --- |
| 30 seconds | `1 / 1` | 400 | 30 windows; 3,145 counts; one packet drop and one corrupted packet were repaired after a retry request. |
| 5 seconds | `2 / 2` | 82 | 5 windows; 562 counts; nominal packet delivery. |
| UI replay, 30 seconds | `3 / 3` | 405 | 30 windows; 3,573 counts; downloaded raw object independently matched its receipt. |

The automated probe also confirmed that a competing command reusing the active request ID returned `BUSY`; duration `0` returned `ScienceCommandRejected`; and a configured `FAIL` returned `ScienceOperationRejected`. The negative cases emitted no product/downlink/ground-receipt events and did not increment the collection count. The first capture used a one-shot 4000 ms `DELAYED_RESPONSE` fixture; the second capture had no ground packet fault. The receiver's ground-receipt event is distinct from `PayloadDownlinkComplete`; sender DONE alone is not proof of receipt or an acknowledgment back to F′. The probe reads the simulator source only after a verified ground receipt as an integrity oracle.

Current-run A regression passed. The C lifecycle probe passed all 31 checks on the preceding run `20261008-135404` with the same binary and dictionary hashes. Six F′ component test suites passed 31 cases total; seven ground Python unit tests passed. The subsequent Yamcs UI replay and all eight walkthrough screenshots followed the automated PASS.

This acceptance is for the simulator/host software route only. The duration command automatically invokes simulated capture; it does not exercise Aris's opaque payload command or real raw-data interface. No Pi Zero W runtime, physical PLD, SatNOGS SPI/firmware, SDR, RF, or complete CDR sequence was tested. At that October 8 acceptance, the storage catalog was in RAM and simulator files were under `/tmp`. October 9 adds durable copies and restart recovery; see the [D/E acceptance plan](../verification/COMPLETION_STORAGE.md). ACK-based reclamation remains deferred. The viewer displays simulated count-frequency, not detector-energy science.

To replay the automated fault-recovery check on a fresh stack, use the local-only fixture and run the probes from a second terminal:

```sh
N2_LOCAL_SCIENCE_FAULTS=drop-first-0,corrupt-first-1 ./tools/yamcs/run.sh
./tools/yamcs/passthrough.sh
./tools/yamcs/raw_cycle.sh
```

For the normal UI walkthrough, start `run.sh` without `N2_LOCAL_SCIENCE_FAULTS` and follow [N2_RAW_CYCLE_OPERATOR_WALKTHROUGH_2026-10-08.md](../operations/walkthroughs/RAW_CYCLE.md).

## October 8: KISS synthetic pulse profile

`./tools/yamcs/run_pulses.sh` selects versioned `n2-pulses-v1` raw JSON instead of aggregate count CSV. Ground derives waveform, integrated-charge spectrum, and PSD density from verified Yamcs raw bytes. All science is SIMULATED, charge is ADC-sample area, and figure of merit/calibration are deferred. Ordinary `run.sh` retains count CSV. The local ON profile sends 32 data packets per one-second tick for pulses only; OFF/hardware pacing remains one packet per tick.

The [pulse plan](../verification/SYNTHETIC_PULSE_ACCEPTANCE.md) records the fixture contract and acceptance evidence; the [pulse operator walkthrough](../operations/walkthroughs/PULSE_SCIENCE.md) describes the command, returned events, plots, and raw download. Repeat `passthrough.sh` then `pulse_cycle.sh` on a fresh pulse stack with `N2_LOCAL_SCIENCE_FAULTS=drop-first-0,corrupt-first-1`. Use one stack and one probe at a time; the demo lock rejects concurrent probes. On restart the OS may briefly retain closed TCP sockets, so allow ports to clear before retrying the launcher.

**PASS:** final run `20261008-144213`, nonce `70be186b0cb0`, automated record `pulse-cycle-fc3dff12dcdd.json`. The 30-second/60-event and 5-second/10-event raw products were independently retrieved and checked; every waveform point, area/ratio, spectrum bin, and PSD bin matched. One dropped and one corrupted packet were recovered; invalid/failed captures created no success products. A passed on the same run, while A/C/B count-CSV regressions passed on `20261008-143103`. The browser replay and seven pictures followed automated acceptance; its raw download and product 3 plots also passed. This does not establish real PLD science, Pi Zero W runtime, or SatNOGS/SDR/RF integration.


## October 9 completion and storage acceptance

The [D/E operator contract and acceptance record](../verification/COMPLETION_STORAGE.md) documents the VERIFIED command variants, persistent `N2_STORAGE_ROOT`, older-product selection, and six passing Yamcs completion/restart/fault scenarios. Legacy A/C/B and pulse regressions also pass on the same native build. The payload team's JAR owns actual science display; FSW delivers opaque raw files.
