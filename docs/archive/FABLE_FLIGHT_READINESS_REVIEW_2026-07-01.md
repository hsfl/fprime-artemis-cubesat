# Flight-Readiness Review & FSR v2.0 Engineering Plan

> **🤖 Special note: this document was produced by FABLE.**
> Written by **Claude Fable 5** (`claude-fable-5`, Anthropic's Mythos-class model, Claude 5 family)
> during an interactive review session with Dennis Sarsozo on **2026-07-01**, on branch `neutron_2`.
> Claims marked *verified* were checked against the repo, the repo-local F´ docs
> (`ArtemisRpiTeensy_N2/lib/fprime/docs`), the installed toolchain, or live web sources during the
> session. This is an AI-assisted review: treat it as a strong first pass, and re-verify anything
> load-bearing before acting on it. Status: **active planning input for FSR** — move to
> `docs/archive/` only after v2.0 lands.

## TL;DR

The repo is in genuinely good shape for the FlatSat demo. The Manager → Service → Adapter HAL is
real (not decorative), the docs are unusually strong, and the transport-constants pipeline is the
right engineering instinct. The biggest risks are **not architectural** — they are operational:

1. A "test what you fly" gap between the `local-demo` and `hil` topology profiles.
2. Unvalidated operator-command edge cases in the demo-critical scheduling path.
3. No enforcement of the validation gate the repo already built (CI — now resolved: repo is public,
   Actions is free).
4. No watchdog/recovery chain for the Teensys and Pi, which have both wedged before.

**Meta-takeaway: the project's risk has moved from *building* to *operating*.** Nearly every major
historical debugging loss (wedged RF, stale Arduino artifacts, wrong-Teensy flash, duplicate
systemd service, wrong GDS framing) was **version/configuration skew across the three nodes**, not
a logic bug. Configuration management — tags, releases, hash triplets, drift checks — *is* this
project's reliability engineering.

---

## Part 1 — Demo-Readiness Findings (ranked)

### 1.1 Rehearse one topology, demo another (test-what-you-fly gap)

`tools/validate_local.sh` and the automated demo build `NEUTRON2_TOPOLOGY_PROFILE=local-demo`; the
judge-facing demo runs `hil`. The two profiles are hand-forked full copies
(`Top/topology.fpp` vs `Top/topology.local-demo.fpp`) that differ in rate-group ticks. In `hil`,
`sohManager.run`, `payloadService.run`, and `storageService.run` are disabled — so **the SoH health
channels never publish periodically in the real demo**; judges see the `MissionManager` heartbeat
(every 30 ticks @ 1 Hz = every 30 s) plus the command-driven `EMIT_SOH_SNAPSHOT` event. That may be
the deliberate RF-budget tradeoff — but the runbook/demo script must say so explicitly.

**Fix:** extract shared connection graphs into a `.fppi` included by both profiles (only the
rate-group block stays per-profile), and rehearse at least once end-to-end on the **hil** profile
before the demo.

### 1.2 Operator-input landmines in the scheduling path (verified in code)

- `SCHEDULE_COLLECTION` with `delaySeconds=0` **never collects** — `MissionManager` enters
  `COLLECTION_PENDING`, `ScienceManager` stores `0`, and the countdown only acts when `> 0`.
  Stuck forever.
- **No cancel exists, and `ENTER_BASE_MODE` doesn't actually cancel** — `ScienceManager`'s countdown
  keeps ticking after a mode reset and fires a surprise collection later (a `BASE → COLLECTING`
  push is an allowed transition).
- No upper bound on `delaySeconds` / `durationSeconds` (a fat-fingered `1000` = 17-minute dead air).
- Commands bypass `isAllowedTransition` while port updates respect it — pick one policy.

**Fix:** reject/clamp bad inputs (`VALIDATION_ERROR`, the `EpsService confirm=1` house style), add a
cancel path, and lock it all in with `ScienceManager` unit tests (already on the open list).

### 1.3 No CI — **resolved during session**

`hsfl/fprime-artemis-cubesat` is **public**, so GitHub Actions standard runners are **free and
unmetered** (the 2,000 min/month cap applies to private repos only). Recommended shape: a ~2-minute
job running only the cheap tier (transport-drift check + Python unittests) on PRs; keep full
`validate_local.sh` as the human pre-handoff gate.

### 1.4 No watchdog on either Teensy

Both firmware trees confirmed watchdog-free, and the bench has wedged before. See Part 3 for the
full recovery-chain design the hardware already supports.

### 1.5 The Pi is a pet, not cattle

`artemis-fprime.service` and the `/home/pi/artemis/{cross,current}` layout exist only as prose in
handoff notes. **Fix:** version the unit file + provisioning checklist in a `deploy/pi/` dir, and —
highest-value single mitigation — keep a **flashed spare SD card** (and spare-Teensy hexes) from the
frozen tag in the bench kit.

### 1.6 Demo freeze discipline

Hashes are recorded but nothing pins the proven **triplet** (Pi binary + satellite Teensy + ground
Teensy) together. **Fix:** annotated tag (e.g. `demo-fsr-rc1`) + GitHub Release with attached
artifacts: ARMv6 Pi binary, dictionary JSON, both Teensy hexes, `SHA256SUMS`. Demo-day flashing
pulls from release assets only; rehearse from the tag.

### 1.7 Duplicated Teensy firmware with no drift guard

Satellite/ground workspaces carry copies of shared modules. Verified at review time:
`artemis_rf23bp.hpp` and `link_counters.hpp` identical; `rf23_driver.*` differs only by the
satellite `linkStats()`; `relay_uart_rf.cpp` ~40 divergent lines / 760 (some intentional, some
probably accidental). **Fix (KISS):** add a must-be-identical file check to `validate_local.sh` —
the silent one-node-only-fix bug class produces exactly the "works sometimes" RF symptoms of April.

### 1.8 Documentation hygiene

- Five stale plain-text refs to renamed `docs/RF_MVP_DEMO_RUNBOOK.md` (now
  `NEUTRON2_RF_MVP_DEMO_RUNBOOK.md`) in `GET_STARTED_TESTING.md`,
  `SOFTWARE_DEBUGGING_TROUBLESHOOTING.md`, `agents_notes.md`. (Markdown links in the key docs all
  resolve — verified.)
- `agents_notes.md` is a ~978-line append-only log mixing current truth with superseded history —
  apply the existing `docs/archive/` pattern to the dated handoff sections.
- Base-ID convention comment in `Top/instances.fpp` doesn't match `payloadDownlinkManager`
  (`0x10030000`).

### Carried risks for the review board (honest framing)

This is FlatSat-demo readiness, not flight readiness. Top carried risks: **channel 2 never
HIL-proven against real PDU** (the EPS story is emulation until then); **uplink is best-effort burst
framing** (operator retry is the mitigation — keep it scripted in the runbook); **RfMvpConfig global
limits** (96-byte com buffers) will bite the first long event string — the landmine doc section
exists; a startup assertion would be better.

---

## Part 2 — F´ Capability Audit ("are we using F´ fully?")

**Verdict: the *architecture* layer, yes — the *operations* layer, no.** The unused value is
concentrated in features JPL built precisely for this constraint profile (tiny lossy link, small
CPU, unattended ops), most of them **one-line FPP declarations** — ideal student-sized PRs.

### Used well (credit where due)

Subtopologies (CdhCore/ComCcsds/DataProducts/FileHandling); App-Man-Drv done correctly (documented
crosswalk in `SYSTEM_ARCHITECTURE.md`); update-safe project-local config overrides (`RfMvpConfig/`);
**`Svc.Health` ping fully wired for all 22 instances** (many teams skip this); UTs on 4 components;
validated ARMv6 cross-compile pipeline; transport-constants generation.

### Unused, verified by grep, mapped to constraints

| Feature | Status in repo | Why it matters here |
| --- | --- | --- |
| Event `throttle` | 0 uses (hand-rolled in `PayloadDownlinkManager`) | April's failure logs were an event storm over 128-byte frames; declarative, resettable by command |
| Telemetry `update on change` | 0 uses | Slow-moving channels re-downlink identical values every tick under TlmChan |
| Channel limits (`low`/`high` yellow-orange-red) | 0 uses | GDS colors channels automatically — judge display red/green for free |
| Parameters (`PrmDb`) | Wired, **0 `param` declarations** | Capture config is volatile RAM — a reboot mid-demo silently resets it; prerequisite for auto-restart |
| `Svc.TlmPacketizer` | TlmChan active; swap scaffolded (commented block + starter `Packets.fppi`) | Deterministic telemetry byte budget; enables live SOH packet in `hil` without flooding |
| `CmdSequencer`/seqgen (+ **fpy**) | `cmdSeq` wired, no `.seq` anywhere | Whole demo story as one uplinked sequence — one uplink instead of six over lossy half-duplex |
| FPP state machines | Hand-rolled mode logic | Autocodes `MissionManager`'s state machine; closes the command-bypass hole |
| `Svc.ComLogger` | Absent | Always-on forensic record of every downlink frame (April debugging hand-captured this) |
| GDS integration test API | Unused | HIL pass criteria as Python asserts — the rehearsal gate as a script |
| GDS dashboards / derived channels | Unused | See Part 6; derived channels require `--no-zmq` |
| `fprime-util visualize` | Available in venv (verified) | Auto topology diagrams that never go stale |
| Thread priorities | Set (27–43) but likely **silently ignored** | Linux RT scheduling needs privileges; on a single-core Pi Zero W priorities are the only lever — grant `AmbientCapabilities=CAP_SYS_NICE` or accept they're cosmetic; check journal for the warning |

### Correctly NOT used (KISS instincts validated)

Hub pattern (no second F´ node); DataProducts/stock `FileDownlink` for science (channel-1 sidecar
exists for a documented physical reason, graduation criteria written); custom framing plugins
(`UartChannelMux` as a component is equivalent and clearer); F´-on-Teensy (decided 2026-02-26,
still right for MVP); multi-core/baremetal OSAL work.

---

## Part 3 — Hardware Findings (Artemis manual + RFM23BP datasheet)

### The supervision chain the board already supports

The manual documents **`RPI_ENABLE`** (Teensy-controlled GPIO gating the Pi 5 V switch), a
**modular-radio reset GPIO**, and a **GPS reset pin**. Three recovery layers, all hardware-on-hand:

1. **Teensy watches itself** — Teensy 4.1 hardware WDOG (`Watchdog_t4`), ~1 day of work.
2. **Teensy watches the Pi** — no valid UART frames for N s while link active → toggle `RPI_ENABLE`.
   *(Only after params persistence — see sequencing constraint below.)*
3. **Pi watches the F´ process** — verify `Restart=always` (+ consider `WatchdogSec`) in
   `artemis-fprime.service`. Note: `Svc.Health` → FATAL → process exit is only a recovery mechanism
   if systemd restarts it.

**Sequencing constraint (novel takeaway): recovery is a chain, and persistence is its
prerequisite.** Auto-restart without persisted parameters silently resets demo config = demo-killer.

### The radio chip is a sensor (real telemetry, cheaply)

RFM23BP/Si4432 has **digital RSSI (±0.5 dB), an on-die temperature sensor, an 8-bit ADC, and a
low-battery detector**. The channel-2 RPC seam already exists (`TEENSY_TARGET_RF_STATUS` /
`rf_op_link_stats` in `config/transport_constants.json`). Extending that response with RadioHead
`lastRssi()` + radio temp turns model-driven adapter telemetry into **real** telemetry judges can
watch react. Same pattern reaches the manual's Teensy-attached I²C current/power sensors and analog
temp channels. Highest demo-credibility-per-hour item found in this review.

### Power landmine for PDU integration

**550 mA @ +30 dBm TX** (datasheet), half-duplex ACK/retry = high TX duty cycle, and the bench has
only ever been USB-powered. First battery/PDU bring-up must include *sustained downlink while
measuring the radio rail* — a TX brownout would look exactly like the historical "wedged RF"
symptom. Never power the radio from Pi header pins. Also check what TX power RadioHead actually
configures; full +30 dBm across a bench may be saturating the front end anyway.

---

## Part 4 — July 31 v2.0 Plan Feasibility (assessed 2026-07-01: 4.3 weeks)

The plan's shape is sound — **every workstream lands on an existing seam** (the HAL paying off as
designed). Constraints: people overlap (Kenoi ×3, Aris/Dennis/Joe/Piper ×2 each) and one shared
bench.

| Workstream | Feasibility | Key notes |
| --- | --- | --- |
| **Payload** — N2 ASU EDU + e2e demo | **HIGH**, gated on EDU arrival ~wk 2 | One new adapter against the proven `PayloadService` contract (pattern proven ×2). Define **max product size now** (35 B/packet ⇒ 10 kB ≈ 300 packets over lossy RF). Develop capture-replay-first; keep sim adapter as documented fallback. |
| **Mission** — ops testing + stress | **HIGH**, ordering matters | Fix the known landmines (§1.2) *before* ops gets it; do event-throttle work *before* the stress test or the link wedges and you debug transport instead of mission logic. Hand ops sequences + runbook. |
| **COMMS** — SatNOGS/SDR GO/NOGO | **GO/NOGO deliverable: feasible. Full integration by 7/31: NO.** | ⚠️ The 2026-06-30 repo investigation said SDR ground station "not worth it"; the plan says "need SDR for FSR" — **clarify which requirement changed** before spending lab time. Written, dated gates: board+ICD in hand by ~7/10; 518/HTS-1 access confirmed; bench byte transport with **MTU ≥ 128 B** by ~7/18 (an MTU fitting one TM frame would *eliminate the entire RF segmentation/ACK layer*). Any gate missed → NOGO → RFM23BP for FSR at zero demo cost. |
| **PDU** — restart via EPS, HIL, watchdogs | **MODERATE-HIGH** | Channel 2 has never met real PDU hardware — this is the right priority. Sequence: PDU on bench supply → Pi rail cycle → radio TX under battery (brownout test, §3). Params persistence **before** auto-restart. July watchdog scope: Teensy WDT + `Restart=always`; Pi power-cycle supervision after the restart story is tested. Keep `confirm=1` gating. |
| **ADCS** — D2S2 CubeComputer | Stretch (correctly hedged) | Externally dependent; seam exists. Don't count toward the v2.0 gate. |

**Cross-cutting:** freeze first (tag before v2.0 branches diverge); schedule bench days (PDU work
takes the stack offline for everyone; push Payload/Mission dev to local emulation); define "v2.0
done" per component as runbook-style pass criteria; name a primary owner per workstream.

---

## Part 5 — Open-Source Ecosystem (verified 2026-07-01)

Direct hits so we don't build from scratch:

- **[fprime-yamcs](https://github.com/fprime-community/fprime-yamcs)** +
  [fprime-yamcs-reference](https://github.com/fprime-community/fprime-yamcs-reference) + fprime-xtce
  — official community path to our stated end-goal ground stack: auto-converts our JSON dictionary
  to XTCE; expects CCSDS TC/TM (which we already speak) over UDP (bridge serial→UDP on ground).
  Early (v0.1.3, June 2026) but active. "Yamcs someday" is now an integration task, not research.
- **[fprime-sensors](https://github.com/fprime-community/fprime-sensors)** — contains `NmeaGps`
  (reuse parsing; ours routes behind Teensy ch-2), `MpuImu`, `Bmp280`, and **`XBee` — *the*
  reference implementation of the Communication Adapter Interface for a UART packet radio**.
  Paired with the local `how-to/implement-radio-manager.md`, this is the template for a SatNOGS
  adapter. COMMS team: read both before writing anything.
- **[fpy](https://github.com/fprime-community/fpy)** — next-gen sequencing language (beyond seqgen
  `.seq`); check compatibility with our pinned GDS 4.2.1 before adopting.
- **[fprime-openmct-integration](https://github.com/fprime-community/fprime-openmct-integration)** —
  experimental; skip in favor of the Yamcs path.
- **[fprime-arduino](https://github.com/fprime-community/fprime-arduino)** /
  [fprime-baremetal-reference](https://github.com/fprime-community/fprime-baremetal-reference) — F´
  *does* run on Teensy 4.1 (default toolchain). Baremetal-Teensy decision stays right for MVP; the
  door is open if bridge firmware ever grows mission logic. **fprime-zephyr** also exists — and the
  [SatNOGS-COMMS firmware is Zephyr-based C++17](https://libre.space/projects/satnogs-comms/)
  (STM32H743 + Zynq, **CCSDS-native framing**,
  [TRL-8 UHF-TX](https://digitalcommons.usu.edu/cgi/viewcontent.cgi?params=/context/smallsat/article/5851/&path_info=SSC24_WVI_07.pdf)) —
  CCSDS-native means it pairs naturally with our `ComCcsds` stack.
- **RFM23BP: no F´ driver exists anywhere.** Our RadioHead-based custom path was and is the only
  option — nothing was wasted. RPi is fully covered by stock `Drv.Linux*` drivers already in use.

---

## Part 6 — GDS Integration of the Custom Payload Path

The repo's own research
([`archive/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md`](archive/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md))
is high quality and its conclusion is **endorsed and independently verified**: keep the channel-1
ARQ sidecar for FSR; do not move science bytes to stock `FileDownlink` over this radio.

Verified against the *installed* toolchain (not just docs): `fprime-gds` **4.2.1** supports all
five plugin categories (`framing`, `communication`, `gds_function`, `gds_app`, `data_handler`);
`DataHandlerPlugin` exposes `set_publisher`; `GdsStandardApp` exists. Framework and GDS are the
same version family — no docs/pip drift. **Caveat found:** ground-derived channel publishing
requires `--no-zmq`, which none of our launch scripts pass — verify that combo in local emulation
before building on the publisher path.

**Guiding principle:** *F´ owns intent and operator-visible state; the radio-aware layer owns bulk
bytes.* Corollary: **integrate by publishing ground truth into GDS (telemetry, derived channels) —
never by forcing bytes through GDS.**

Staged plan (each stage independently shippable; the demo never depends on the next one):

- **Stage 0 (FSR, zero new code):** dashboard XML over existing `PayloadDownlinkManager` telemetry
  (`ProgressPercent`, `PacketsMissing`, `RetryRound`, …) + mode/SOH/RSSI — judge-facing progress
  from flight truth. Receiver remains the file proof.
- **Stage 1 (small):** `gds_app` plugin auto-launches `payload_receiver.py` (+ viewer) with GDS —
  kills the three-terminal operator overhead. Laptop-only work; no bench time.
- **Stage 2 (post-`--no-zmq` check):** receiver publishes ground-truth state (holes, retry rounds,
  CRC verified, output path) into GDS as derived channels — single pane of glass.
- **Stage 3 (post-MVP, upstreamable):** (a) small PR to `fprime_gds` downlinker — don't mark holey
  transfers finished; use the END hash; (b) the "repairing FileDownlink" plugin
  (`data_handler` on `FW_PACKET_FILE` + `GdsStandardApp` driving `SendPartial`/`CalculateCrc`).
  Open a nasa/fprime-gds discussion first — the research doc is ~80 % of the design writeup.

**Goal framing for FSR:** "the file arrives verified and judges can watch it happen" — not "bytes
appear in the stock GDS File Downlink tab."

---

## Part 7 — Practices to Institutionalize

1. **Freeze-and-build:** demo hardware only ever runs release artifacts; rehearse from the tag.
2. **Test what you fly:** ≥1 full `hil`-profile rehearsal per milestone; de-fork the topology files.
3. **Evidence-gated milestones:** written pass criteria + artifacts for every "done" claim
   (already the culture — keep naming it).
4. **Dated decision records with owners:** the SDR contradiction is the case study; give
   investigations verdict lines, owners, and expiry conditions (the GO/NOGO should be one).
5. **Generated over hand-maintained:** transport constants proved it; extend to topology fragments,
   `fprime-util visualize`, dictionary-driven ground tools.
6. **Operator-input validation as a design rule:** `confirm=1` is the house style — apply to every
   state-changing command.
7. **Current docs vs. archive discipline:** active decisions live in `docs/`; dated history moves to
   `docs/archive/`.

---

## Part 8 — Sequenced Next Steps

### Freeze week (now)

- [ ] Tag `demo-fsr-rc1` + GitHub Release: ARMv6 Pi binary, dictionary JSON, both Teensy hexes,
      `SHA256SUMS`
- [ ] Fix §1.2 scheduling landmines + `ScienceManager`/`CommsManager` UTs
- [ ] Write SatNOGS GO/NOGO record: dated gates, owner, criteria (incl. MTU ≥ 128 B)
- [ ] Set the shared bench calendar
- [ ] Move `FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md` out of `archive/`; fix 5 stale runbook refs

### Weeks 1–2

- [ ] Params persistence for demo config → then Teensy WDT + verify `Restart=always`
- [ ] FPP ops pass **before** the stress test: event `throttle`, `update on change`, channel limits
- [ ] Stage-0 judge dashboard XML
- [ ] Free 2-minute CI job (transport drift + Python unittests)
- [ ] Flash spare SD card + spare Teensy hexes from the tag → bench kit

### Weeks 2–4 (v2.0 push)

- [ ] PDU HIL with the 550 mA brownout test in the procedure
- [ ] Payload EDU adapter, capture-replay-first, sim fallback documented
- [ ] Mission ops campaign driven by sequences (`cmdSeq`/fpy)
- [ ] Stage-1 `gds_app` receiver auto-launch (laptop-only task, if time allows)

### Post-FSR

- [ ] TlmPacketizer swap + SOH packet (live SOH in `hil` without flooding)
- [ ] Yamcs via fprime-yamcs/XTCE
- [ ] Upstream: GDS END-hash patch PR + repair-plugin discussion on nasa/fprime-gds
- [ ] SatNOGS adapter from the XBee/Communication-Adapter-Interface template (if GO)
- [ ] Genericize `PayloadDownlinkManager` → `ReliableBlobDownlink`
- [ ] Teensy-supervises-Pi power-cycle layer (after restart story is proven)
- [ ] FPP state machine refactor of `MissionManager`; `ComLogger`; GDS test-API HIL asserts

---

*Session artifacts: findings verified on branch `neutron_2` @ working tree of 2026-07-01, F´
v4.2.1 (submodule `a750219`), fprime-gds 4.2.1. Companion research doc:
[`archive/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md`](archive/FPRIME_FILE_DOWNLINK_RELIABILITY_RESEARCH.md).*
