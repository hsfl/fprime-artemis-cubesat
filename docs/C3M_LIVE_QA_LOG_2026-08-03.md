# C3M Live QA Log — 2026-08-03

## Scope and safety boundary

This log records hardware-in-the-loop evidence for conference-demo readiness.
No flight, GDS, or Teensy source changes were made as part of these tests.

The transmitter load tests used the standalone `gds_tx_thermal_soak` diagnostic:

- Ground Teensy physical upload target: `usb:100000`
- 433 MHz RFM23BP transmit loop, 30 dBm, 49-byte packets
- No application-layer peer or ACK requirement
- Satellite powered off during antenna load tests

Consequently, a TX-completion timeout is a ground transmitter/RF-path result;
it is not caused by a missing satellite response. The normal triple-serial GDS
firmware was restored after every soak.

## Antenna A/B/C TX-load results

| Configuration | Measurement window | TX successes | TX failures/timeouts | SDN recoveries | Result |
| --- | --- | ---: | ---: | ---: | --- |
| Old monopole | Clean 180 s counter delta | 35,084 | 0 | 0 | Pass |
| Amazon antenna, no ground plane | Approximately 3 min; observed counters (not clean zero-time delta) | 20,276 | 133 | 44 | Fail |
| Amazon antenna, with ground plane | Clean 180 s counter delta | 13,435 | 171 | 57 | Fail |

For the grounded Amazon configuration, the exact timed counter baseline was
`(success=8,162, failures=105, timeouts=105, recoveries=35)` at
`2026-08-03T12:41:33-1000`. At `2026-08-03T12:44:33-1000`, it was
`(21,597, 276, 276, 92)`, yielding the table delta. The observed failure rate
for that timed window was 1.26% (171 / 13,606 attempted transmissions).

## Conclusion and operational disposition

The old monopole is the current qualified conference-demo antenna: it completed
35,084 back-to-back local transmissions with no timeout or recovery. The Amazon
antenna/feed installation is not qualified. Adding its ground plane did not fix
the local transmitter fault and produced 171 timeouts in the measured window.

Use the old monopole for live demonstrations until the Amazon path passes all
of the following as a separately logged, one-variable-at-a-time investigation:

1. 50-ohm dummy-load A/B transmit soak at the same power level.
2. Connector, center-contact, shield-to-ground, and antenna-mount inspection.
3. Calibrated mounted-configuration VNA/S11/SWR measurement at 433 MHz.
4. Repeated clean 180 s TX soak with zero failures, followed by live two-way
   command and payload verification.

## Current live-QA continuation

After restoring normal GDS firmware, its debug counters showed
`rf_state=READY`, `rf_fault=NONE`, and zero startup error counters. The
satellite was then booted for live command, telemetry, and payload-path QA.
Subsequent results belong in this log or a dated follow-up entry.

## Live GDS command and payload QA

### Configuration

- Date/time: 2026-08-03, local HST.
- Antenna: qualified old monopole.
- Ground software: `./tools/c3m` (F Prime GDS on channel 0 and the C3M
  payload receiver on channel 1).
- Satellite: `raspberrypi-c3m`, `artemis-fprime.service=active`, deployment
  running on `/dev/serial0`.

### Command regression and boundaries

The following safe command surface was exercised over the live GDS/RF link:

| Test | Evidence | Result |
| --- | --- | --- |
| `ENTER_BASE_MODE`, SOH snapshot, storage status, link status | Pi command-dispatched/completed events | Pass |
| `SCHEDULE_COLLECTION(0)` and `(301)` | `MissionCommandRejected`, `VALIDATION_ERROR` | Expected rejection |
| 10 alternating `PING(U32)` / `REQUEST_LINK_STATUS` pairs | All 10 PONG tokens `520001` through `520010`; all 10 status commands completed | Pass |
| Pending-collection cancel | Schedule 10 s, cancel at 8 s remaining; returned to BASE and no capture followed | Pass |
| `LINK_STATUS`, RSSI, latest/history dataset, idle payload status | Completed; RSSI valid at 0 dBm (near-field saturation) | Pass |
| `CONFIGURE_CAPTURE_DURATION(0)` and `(121)` | `ScienceCommandRejected`, `VALIDATION_ERROR` | Expected rejection |

State-changing EPS/PDU, erase, manual payload-start/abort, preview-stream, and
payload-driver-selection commands were intentionally excluded from this live
conference-demo QA because they can alter hardware state, delete evidence, or
bypass the validated mission path.

### Fresh capture and command-during-downlink gate

1. Scheduled a 5 s collection. The live Pi selected the real `LEPTON` driver
   and stored a 38,482-byte `.fdp`.
2. Requested the cached science downlink and sent `missionApp.PING(530001)`
   two seconds into the 1,100-packet transfer.
3. The Pi logged the PONG during transfer, then `PayloadDownlinkComplete` and
   `DownlinkFinished`.
4. Ground receiver result:
   [`data/c3m_20260803_225955_transfer_1/run.json`](../data/c3m_20260803_225955_transfer_1/run.json)
   reports CRC `0x467e`, 38,482/38,482 bytes, 1,100/1,100 packets, zero missing
   packets, zero receiver repair rounds, SHA-256
   `bf99c924dc31f6b34d963bcc7c6019322ee82f21b64ca8c5777b56cfaee9e285`, and a
   160 by 120 / 19,200-pixel decoded Lepton image in 11.757 s.

### Findings and demo implications

1. **Pass:** The previously unproven command-during-cached-downlink path now
   has live evidence: an interleaved command reached the Pi and the complete
   CRC-valid science file still arrived.
2. **Open RF reliability issue:** During this live session, ground counters
   accumulated two ACK timeouts/retries, two message-ID gaps, and three
   reassembly drops. There were no terminal TX failures and the science
   receiver completed without missing packets, but this is not a clean-link
   result. Keep channel 0 quiet during science downlink except for a deliberate
   proof command, and preserve GDS/Teensy counters for every demo rehearsal.
3. **Startup backlog issue:** before GDS attached to the ground channel-0 USB
   interface, its counters showed `down_q_drops=83`, `usb0_discards=83`, and a
   high-water mark of 32. These values stopped increasing after GDS attached,
   so this is recorded as an observed startup/drain risk rather than a proven
   root cause. Start GDS before generating command/event traffic and do not
   claim a clean run from a fresh boot without a fresh counter baseline.

No implementation changes were made.
