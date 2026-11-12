# Local Artemis twin runtime and ground

This tooling runs the real Zephyr/F′ flight controller and Linux/F′ payload
computer in separate containers. It never opens a physical serial port or RF
device. Docker Desktop supplies one Linux VM; Compose gives each role its own
processes, UART endpoints, build artifacts, and data.

Operator walkthrough: [C3M power → capture → cache → downlink → download](../../docs/operations/walkthroughs/C3M_YAMCS_LOCAL_TWIN.md).
Read its verified unchanged-C3M-app compatibility result and producing-dictionary requirements.

Architecture and documentation map: [current twin design](../../docs/architecture/ARTEMIS_LOCAL_TWIN.md).

## Start on macOS

From the repository root:

```sh
python3 tools/twin/twin.py build
python3 tools/twin/twin.py up
python3 tools/twin/twin.py status
python3 tools/twin/twin.py logs
python3 tools/twin/twin.py demo --captures 3
python3 tools/twin/twin.py demo --captures 1 --pi-off-repair
python3 tools/twin/twin.py faults
python3 tools/twin/twin.py down
```

Windows developers use the same commands in WSL2 with Docker Desktop enabled.
The Yamcs browser interface is at `http://127.0.0.1:8090`. The virtual-device state/fault interface
is at `http://127.0.0.1:8092/state`. Port conflicts fail visibly; this launcher
does not stop unrelated applications.

`build --image-only` builds just the dependency image. `build --role fc`,
`build --role pi`, and `build --role pdu` build individual applications.
`shell fc` / `shell pi` opens the local toolchain container. The image uses
Python 3.11, Debian bookworm, the selected F′ release's Python requirements,
and Zephyr's base tooling. The base image digest and resolved Python dependency
set are pinned in `twin/Dockerfile` and `twin/requirements.lock`. No macOS build
cache is reused inside Linux.

The build first configures into dedicated `/build/fc` and `/build/pi` volumes,
then builds only the selected deployment. It does not compile unrelated
framework test-helper modules. Role-specific artifacts contain a normalized
executable, the generated dictionary, and a hash/version manifest.
Manifests explicitly record dirty source state and a digest of the actual project
source, including new source files. A commit ID alone is not described as the
identity of an uncommitted build. Ground requires matching working-source and
dependency states, as well as executable/dictionary hashes.

## Architecture and storage

| Role | Local behavior |
|---|---|
| Flight controller | Zephyr `native_sim`, real mission/managers, RAM-only cache |
| Payload computer | Linux F′ application, explicitly recorded Lepton backend, persistent `/payload` |
| Devices | Paces FC↔Pi bytes, GPS NMEA, actual PDU C host adapter, virtual power and packet RF |
| Ground | RF framing/ACK/reassembly, production science receiver, Yamcs, exact raw FDP object readback |

Pi starts powered off. A flight-controller virtual GPIO transition writes
`POWER ON\n` or `POWER OFF\n`; the payload supervisor starts/stops only the
payload application. Its UART bridge and supervisor remain available.

The ground service cannot mount Pi product storage. It receives science only
through radio channel 1, saves complete/partial reception separately, and admits
only complete CRC-valid products to its bounded Yamcs publication worker.
Yamcs upload and GET must agree byte-for-byte and by SHA-256. Yamcs stores the
original FDP file and its verified ground receipt. Image and temperature analysis
belongs to your separate mission operations software; this runtime performs no
image decoding, rendering or analysis.
FC and Pi dictionary hashes and framework revisions are validated at startup.
Partial and `.badcrc` files remain diagnostics and never verified science.

`down` retains Pi and Yamcs named volumes. Per-run logs and received ground
products live under `.twin-runs/<run-id>/`; `current.json` identifies the latest
run. These files are generated state, not source. Remove old runs only after
reviewing any evidence that should be retained.

## Virtual wire contract

Every process opens its own raw PTYs; the supervisor bridges them to the device
service over TCP. PTYs and UART buffers remain local to their container.

| Device TCP port | Endpoint |
|---|---|
| 6100 | FC `/run/twin/pc.tty` |
| 6101 | Pi `/run/twin/fc.tty` |
| 6102 | FC `/run/twin/gps.tty` |
| 6103 | FC `/run/twin/pdu.tty` → actual PDU C helper |
| 6104 | FC `/run/twin/radio.tty` |
| 6105 | FC `/run/twin/power.tty`, ASCII power state |
| 6106 | Pi supervisor power-state subscription |
| 6107 | Ground radio packet link |

The unused FC `/run/twin/ground.tty` exists for its USB/debug transport binding.
Ground TCP 5100 carries CCSDS after radio processing into `fprime-yamcs`.

Radio adapter records are `52 46 | length,to,from,network,version | data | CRC16`.
Data length is 1–49 bytes. CRC16-CCITT starts at `0xffff`, excludes the two magic
bytes, covers the five header bytes plus data, and is sent little-endian.
This record is the simulated driver's local I/O boundary, not the on-air packet.
The on-air A5/A6 segment format and asymmetric ACK settings come from the
existing transport/network JSON manifests. Channel reassembly and message
counters are independent; one channel's incomplete frame cannot erase another.

PC bytes use 115200 8N1 pacing; GPS and PDU byte streams use 9600 8N1.
Radio airtime uses the configured 125 kb/s profile
with half-duplex serialization. This is a software link model, not proof of
physical throughput, IRQ timing, or electrical power behavior.

The PDU helper compiles the actual `app.c` and `pdu_packet.c` with the firmware's
existing GPIO/UART test shims. It has monotonic scheduler ticks and standard-I/O
UART, so framing, command semantics, output state and timers execute production
C. GPIO/electrical behavior and watchdog reset latency remain modeled. The host
build copies handwritten files into `/build/pdu`; it never modifies generated
metadata in the read-only PDU source checkout.

## Fault controls and tests

GET `/state` shows endpoints, power and counters. POST `/faults` accepts only:

- `pc_connected` / `radio_connected`: booleans.
- `drop_science_packet_once` / `drop_science_packet_always`: DATA packet index,
  or `-1` to disable.
- `radio_delay_ms`: extra per-packet delay, up to 10000 ms.

For example, an HTTP JSON body `{"drop_science_packet_once":0}` loses the first
science DATA packet and allows a later cached repair packet through. These
controls act only on the local twin.

```sh
python3 tools/twin/twin.py test
python3 -m compileall -q tools/twin
docker compose -f twin/compose.yaml config --quiet
```

## Milestone evidence

### Runtime/toolchain bootstrap — 2026-11-11 HST

- ARM64 Docker image built successfully; release F′/Yamcs requirements installed
  and `pip check` reported no broken requirements.
- Compose validation and all Python syntax checks passed.
- Real PDU C helper compiled successfully in the Docker build volume.
- Fifteen targeted tests pass: radio CRC/length/resynchronization, independent
  channel reassembly, duplicate/header/ACK behavior, malformed science admission,
  stale artifact rejection, tampered Yamcs readback rejection, raw file handoff, and actual C PDU parser/GPIO
  request/response and corrupt-frame recovery.
- FC configuration reached the real source build. Native runner/platform
  compatibility fixes are owned by the platform integration milestone.

Ground/Yamcs runtime, full split capture, and three-cycle acceptance are not
claimed by these unit/bootstrap results; their observed evidence is appended
after integrated runs.

The `demo` command issues mission commands through Yamcs, waits for a new cache
commit, requests downlink, and independently downloads the raw Yamcs object and verifies
its size, CRC and SHA-256. The optional Pi-off run injects one
lost DATA packet after staging and powers Pi off through MissionApp before
downlink. Its receipt must include a repair round. A separate, read-only Pi
source-hash oracle runs only after delivery; it never transfers source bytes to
ground. JSON evidence is written into the current run directory even on failure.

### Real runtime and Yamcs dependency startup — 2026-11-11 HST

The real Zephyr `native_sim/native/64` executable booted with F′ 4.4.2 and Zephyr
4.3.0. All local UART PTYs connected, GPS reported READY with eight modeled
satellites, and the actual PDU C application accepted its boot command. Pi power
started OFF as required by MissionApp STANDBY.

The dependency-only probe started the real pinned Yamcs bundle and its realtime
processor, converted the real FC dictionary, and resolved
`missionApp.ENTER_BASE_MODE` and `scienceCoordinator.JobState` in the generated
MDB. It injected no FSW frames or science. Evidence is
`ground/yamcs-dependency-probe.json` in the initial run directory. The startup
probe does not establish integrated capture or radio acceptance.

Fifteen targeted tests now pass, including coverage proving that source
fingerprints change for FPP packet/topology templates, YAML native sensor
bindings, generated-header templates, native toolchain configuration and the PDU
adapter. Manifests also record the PDU dependency revision/state and exact
protocol-header hash. Normalized binary/dictionary publication uses atomic file
replacement.

The pinned fprime-yamcs adapter does not provide a Yamcs `CommandComplete`
verifier. The acceptance harness serializes commands and requires a new actual
F′ dispatcher `OpCodeCompleted` event with the corresponding opcode; explicit
`OpCodeError` fails immediately. Subsequent fresh mode, payload READY, job state,
product identity and archive readback checks establish the requested behavior.
HTTP command submission alone is not treated as completion. Dispatcher events
lack an echoed Yamcs command identifier, so this correlation assumes that the
harness is the only command issuer during acceptance.

```sh
python3 tools/twin/twin.py faults
```

The additional fault harness interrupts a live UART stage, drops radio traffic
while preserving the FC cache, and pauses only the real local Yamcs Java process
while received science queues in the ground outbox. It restores each service and
requires a verified object readback. Radio channel 1 has no per-segment link ACK:
local `SENT` alone cannot prove receipt. These cases are implemented; their live
results are recorded separately in `acceptance-faults.json` after execution.


## Download the file for mission operations

Open `http://127.0.0.1:8090`, enter the `fprime-project` instance, then open
**Storage** and the **artemis-science** bucket. Download the `.fdp` object and its
matching `.fdp.receipt.json`. Each name includes the twin run ID, product ID,
transfer ID and SHA prefix. The receipt records byte count, CRC16, SHA-256,
repair rounds, producing dictionary hash and an exact `download_path`.

The same raw file, independently downloaded from Yamcs, is retained locally at:

```text
.twin-runs/<run-id>/ground/archived/<object-name>.fdp
.twin-runs/<run-id>/ground/archived/<object-name>.fdp.receipt.json
.twin-runs/<run-id>/ground/dictionaries/pi.json
```

Use that FDP and the matching Pi dictionary with your own mission operations
software. Ground does not mount Pi product storage; these local archive files
come exclusively from Yamcs GET after complete radio reception. The REST path is
`http://127.0.0.1:8090/api/storage/buckets/artemis-science/objects/<object-name>`.
For example, with the actual downloaded object's name:

```sh
curl --fail --output product.fdp \
  'http://127.0.0.1:8090/api/storage/buckets/artemis-science/objects/OBJECT_NAME.fdp'
shasum -a 256 product.fdp
```

### Raw FDP handoff scope — 2026-11-11 HST

The runtime now ends at Yamcs file/receipt delivery. The added custom web UI,
HTTP port 8091 and automatic image/temperature decoding were removed. Acceptance
uses actual Yamcs object listing and GET, CRC/SHA verification, plus a read-only
Pi source-hash check after delivery. The producing dictionary is preserved for
external mission operations software. Prior diagnostic image checks are not a
requirement or feature of this runtime.

### Raw FDP three-cycle acceptance — 2026-11-11 HST

Run `20261112T052132Z-f1628f` passed all three complete cycles on the real
native Zephyr FC and Linux Pi F′ applications. Products 7, 8 and 9 were each
38,486-byte FDP files. Every cycle required an actual dispatcher completion,
payload READY, full FC cache commit, radio reception, CRC-valid ground file,
Yamcs object upload/GET with matching byte count/CRC/SHA, and a matching
read-only Pi source hash checked only after delivery. No image analysis ran.

Evidence: `.twin-runs/20261112T052132Z-f1628f/acceptance.json` and the role
manifests under its `ground/dictionaries/`. Both compiled deployments have
working-source fingerprint
`5f112123856d311de8837bf456677ab5461697cadcd665a50ea376f15139ce7f`.
FC telemetry generation times now use actual UTC; the native boot adapter sets
its clock from the supervisor's launch epoch without changing the monotonic
scheduler clock. Fifteen targeted runtime tests pass.

### Pi-off cached packet repair — 2026-11-11 HST

The same final run passed `demo --captures 1 --pi-off-repair`. Product 10 was
fully staged, MissionApp entered STANDBY and the real Pi application stopped.
The device model dropped science DATA packet 0 exactly once. Ground requested
one repair round, and the FC retransmitted from its RAM cache while Pi power
remained OFF. Yamcs GET matched all 38,486 bytes, CRC16, SHA-256 and the persisted
Pi source hash. The one-shot loss setting was cleared afterward.

Evidence: `.twin-runs/20261112T052132Z-f1628f/acceptance-pi-off-repair.json`.
The additional failure/recovery suite runs after this milestone.

### Failure and recovery acceptance — 2026-11-11 HST

The final run passed `python3 tools/twin/twin.py faults` with exit code 0:

- A nonnumeric U32 command argument was rejected by Yamcs with HTTP 400.
- A real 30-second capture entered BUSY, was cancelled, returned to READY, and
  successfully captured/archived replacement product 11.
- Interrupting FC↔Pi UART during a partial stage produced FAILED with invalid
  cache. Restoring the link and restarting the modeled Pi allowed clean capture
  and verified archive of product 13; incomplete product 12 was not archived.
- Radio traffic loss produced no verified receipt. The FC retained its complete
  cache; restoring RF and explicitly resending archived product 14 correctly.
- Pausing the real Yamcs Java process left complete product 15 in the ground
  publication outbox with an observed five-second HTTP read timeout. After
  resume, retry uploaded the original raw file and verified its exact GET.

Evidence: `.twin-runs/20261112T052132Z-f1628f/acceptance-faults.json`.
The first harness attempt expected HTTP rejection for a negative U32; the
pinned encoder instead sent it and F′ correctly returned VALIDATION_ERROR. That
report is preserved as `acceptance-faults-attempt1.json`; the final argument test
uses a nonnumeric value to verify HTTP rejection. No failed report was discarded.

All four containers remain running for inspection, Yamcs has resumed, and all
injected link/loss/delay settings are restored to defaults. Final verification
passed all 15 targeted tests, Python compilation and Compose configuration.
These are local software-twin results; physical RF, GPIO, sensors and power
switching were not exercised.

### Existing C3M app handoff verified — 2026-11-11 HST

The twin producer now preserves Lepton container/record 0x10027000; its Pi link
buffer manager moved to 0x10034000. Both roles rebuilt, three fresh Yamcs capture
cycles and a Pi-off cached repair passed. All four downloaded files passed the
unchanged existing C3M app using its F′ tools 4.2.1 environment and the regenerated
producing Pi dictionary: complete Lepton History, full temperature CSV and PNG.
Use the operator walkthrough's explicit offline `verify_c3m_handoff.py` command
to repeat this separate handoff check. Yamcs still only delivers the raw file.
