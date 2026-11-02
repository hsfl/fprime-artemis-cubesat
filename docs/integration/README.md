# N2 hardware integration

**Pi Zero W runtime update, October 26:** `d4fe283` and its matching onboard
dictionary are deployed on `n2-obc`; reboot/autostart and stalled-process recovery
were checked. See the [current OBC deployment record](pi-zero-w/N2_OBC_DEPLOYMENT_2026-10-26.md)
for boot measurements, service tooling, limits, and rollback. The older Pi-runtime
status statements below predate this deployment. A subsequent two-Pi test paired the Pi 5 dictionary and verified PING and GUI
synthetic science capture/downlink. See the [ground deployment record](N2_GROUND_DEPLOYMENT_2026-10-21.md). Real payload/PDU and RF acceptance remain open.

**Milestone scope revised October 18:** CDR is OBC + real payload + PDU-controlled payload on/off, skipping COMMS. CDR command/telemetry uses Pi Zero W F′ TCP over the local network to Pi 5 Yamcs (deployed on Raspberry Pi OS), with operator browsers on the LAN. The direct stock ComCcsds/TCP and FileDownlink profile passed [local native acceptance](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md) on October 20, with no network mux and independent fake UART channel-2 PDU RPC. Pi runtime has since been exercised; hardware validation remains open. Use the [canonical current plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) for CDR implementation and remaining gates. FSR adds the complete SatNOGS/SDR/Yamcs chain to payload + PDU, with the previously planned nominal modes/scheduling. See [current intent](../planning/DEVELOPMENT_INTENT.md).

**Status:** local software acceptance is complete for the recorded synthetic scenarios. Dennis confirmed USB↔RS-422 payload command/telemetry, Pi Zero W/Teensy satellite split, Pi 5 Ubuntu/Yamcs LAN hosting, and Sam/Zach SatNOGS/SPI/SDR ownership on October 18. Real PLD acquisition/raw-file transfer, SatNOGS host/SPI, SDR handoff and full RF acceptance remain open. These pages record integration inputs, not operating authorization or a hardware runbook.

- [PDU payload-power integration](PDU.md): CDR on/off sequence, rail/harness/readback, readiness and data retention.
- [Payload contract needed](PAYLOAD.md): commands, response/lifecycle and raw-file boundary.
- [SatNOGS and SDR contract needed](SATNOGS.md): host packet transport and ground handoff.
- [Ground station deployment](N2_GROUND_DEPLOYMENT_2026-10-21.md): Pi 5 Raspberry Pi OS, LAN Yamcs, admin/viewer accounts and boot services. [Earlier hosting plan](GROUND_HOST.md).
- Pi Zero W reference procedures: [setup](pi-zero-w/SETUP.md), [native build](pi-zero-w/NATIVE_BUILD.md), [cross-compile](pi-zero-w/CROSS_COMPILE.md), [cross-build handoff](pi-zero-w/CROSS_COMPILE_HANDOFF.md). These describe the Artemis proxy and older target evidence; they do not deploy or validate the new Yamcs baseline.
- [Shared Artemis board/power references](../reference/artemis/README.md).

Keep simulator software acceptance, target build/runtime, wired interface tests, RF/FlatSat acceptance, and flight qualification separate. Each hardware action requires its applicable runbook and task-specific authorization.
