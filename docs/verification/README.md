# N2 software verification

Start with [the accepted baseline](SOFTWARE_BASELINE.md). Recorded host tests do not establish real payload, target hardware, RF or flight acceptance; the separate October 27 record covers a bounded physical PDU bench test.

| Record | Scope |
| --- | --- |
| [October 27 PDU deployment](evidence/n2-pdu-deployment-2026-10-27/README.md) | Pi Zero W, Pi 5/Yamcs and real PDU version, nine named readbacks and 3V3_1 ON/OFF; no measured voltage or payload acceptance |
| [October 26 PDU local acceptance](evidence/n2-pdu-readability-2026-10-26/README.md) | Native F′/Yamcs with fake PDU, 42 component tests and malformed, mismatch, timeout and recovery checks |
| [October 20 CDR TCP acceptance](evidence/n2-cdr-tcp-2026-10-20/README.md) | Direct stock FileDownlink/TCP with real local Yamcs, exact payload sample readback and independent fake UART/PDU RPC |
| [Completion and storage](COMPLETION_STORAGE.md) | Verified delivery stages, persistent raw products, six October 9 scenarios |
| [Synthetic pulse acceptance](SYNTHETIC_PULSE_ACCEPTANCE.md) | Simulated raw samples and ground regression views |
| [October 9 evidence](evidence/n2-software-2026-10-09/README.md) | Original selected acceptance artifacts |
| [October 14 packaging evidence](evidence/n2-packaging-2026-10-14/README.md) | Sender sequence correction and fresh regression/restart checks |

Evidence files retain their original bytes and historical source/run paths. Those paths are provenance, not the current documentation layout; links from evidence READMEs point to the maintained guides. Moving the snapshot does not change the original acceptance scope or hashes.
