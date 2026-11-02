# Neutron 2 documentation

Start with the guide for your task. Final local native CDR profile and legacy Yamcs regression checks pass, with independent fixture checks and fake UART/PDU RPC. The separate [October 27 live PDU record](verification/evidence/n2-pdu-deployment-2026-10-27/README.md) covers the deployed Pi Zero W and Pi 5, nine real PDU readbacks and 3V3_1 ON/OFF. Measured voltage, the real payload sequence, RF and flight qualification remain open. CDR does not use a network mux or SatNOGS/SDR/RF; physical UART mux channel 2 remains for PDU RPC. See the [canonical plan](planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md), [runbook](operations/CDR_TCP.md), and [local evidence](verification/evidence/n2-cdr-tcp-2026-10-20/README.md).

| Your task | Start here |
| --- | --- |
| Mission ops: first-time local setup | [Local setup](operations/LOCAL_SETUP.md) |
| Mission ops: run a prepared laptop | [Daily quickstart](operations/QUICKSTART.md) |
| Mission ops: a command or display failed | [Operator troubleshooting](operations/TROUBLESHOOTING.md) |
| PDU named rail commands and readbacks | [PDU Yamcs operator guide](operations/PDU_READABILITY.md) |
| Develop or test N2 FSW | [Build and test](development/BUILD_AND_TEST.md) |
| Understand the implementation | [System architecture](architecture/SYSTEM_ARCHITECTURE.md), [mission modes](architecture/MISSION_MODES.md) |
| Connect hardware | [Integration status and contracts](integration/README.md) |
| Check what passed | [Software baseline](verification/SOFTWARE_BASELINE.md), [verification index](verification/README.md) |
| Understand priorities/open work | [Development intent](planning/DEVELOPMENT_INTENT.md), [SDD gap plan](planning/SDD_GAP_PLAN.md) |

## UNP milestones — transport selection October 20

CDR retains payload/OBC/PDU power-on/off without SatNOGS/SDR/RF. Pi Zero W F′ runs the stock TCP server; Pi 5 Yamcs connects with tcp-fast-client. ComCcsds and stock FileDownlink use the direct TCP link. Keep UART mux channel 2 for local PDU RPC. Final local checks passed with fake payload/PDU interfaces; they do not establish hardware acceptance. FSR adds SatNOGS/SDR/RF and previously planned modes/scheduling. Start with [current intent](planning/DEVELOPMENT_INTENT.md), [architecture](architecture/SYSTEM_ARCHITECTURE.md), and [PDU integration](integration/PDU.md).

## CDR implementation handoff and historical material

- [CDR TCP/Yamcs implementation plan](planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md): current status and gates; the [runbook](operations/CDR_TCP.md) has commands and procedures.
- [Historical October 7 review](N2_CLAUDE_OPUS_REVIEW_2026-10-07.md): findings apply to the reviewed snapshot, not automatically to current code.
- [Pre-refresh architecture](archive/legacy-gds-rfm23bp/SYSTEM_ARCHITECTURE_BEFORE_UNP_REFRESH.md): legacy comparison, not current N2 mission authority.
- Preliminary October 8 screenshots: [command](images/n2-pulse-science-2026-10-08/preliminary-01-command.jpg), [report](images/n2-pulse-science-2026-10-08/preliminary-02-command-report.jpg), [capture events](images/n2-pulse-science-2026-10-08/preliminary-03-capture-events.jpg). These show simulator activity; Queued/Released/Sent is not verified end-to-end completion, and a CaptureComplete event is not hardware evidence.

## Keep the mission context clear

- **Current N2:** `operations/`, `development/`, `architecture/`, `integration/`, `verification/`, and `planning/`. A document may describe planned behavior; check its status before treating it as implemented.
- **Shared Artemis hardware:** [Artemis references](reference/artemis/README.md). Board/firmware revision and measurements determine applicability.
- **C3M comparisons:** [C3M references](reference/c3m/README.md). They inform N2 engineering and do not establish N2 acceptance.
- **Original design inputs:** [SDD references](reference/sdd/README.md), [glossary](reference/GLOSSARY.md), and [ground-interface background](reference/FPRIME_GROUND_INTERFACES_PRIMER.md).
- **Old workflows and investigations:** [Archive](archive/README.md). GDS/RFM23BP demo procedures are retained there for the older baseline; current mission ops starts above.

macOS arm64 local emulation is validated. Running the full stack on Windows requires WSL and remains unvalidated; native Windows browser access to a separately configured shared server is a different operator workflow. The current local launcher binds localhost.

Maintained guides use stable names. Dated acceptance evidence remains dated and preserves the original source/run identity. No hardware or flight-readiness claim follows from a successful local software run.
