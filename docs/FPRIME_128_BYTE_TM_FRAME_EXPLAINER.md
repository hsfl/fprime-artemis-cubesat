# F Prime 128-Byte TM Frame Explainer

Date: 2026-04-28  
Audience: future Artemis/F Prime students debugging GDS telemetry over the RFM23BP link

## BLUF

The 128-byte TM frame is a demo transport setting that makes F Prime telemetry survivable over the current low-cost RFM23BP radio bridge.

It is not magic and it is not a general file-downlink solution.

The key numbers are:

```text
RFM23BP packet limit used here = 49 bytes
RF relay header = 5 bytes
Useful RF payload per packet = 44 bytes

F Prime CCSDS TM frame = 128 bytes
128-byte TM frame over RF = 3 RF packets: 44 + 44 + 40

Default F Prime CCSDS TM frame = 1024 bytes
1024-byte TM frame over RF = about 24 RF packets
```

GDS accepts a CCSDS TM frame only if the full frame arrives byte-for-byte intact and passes CRC. A 1024-byte TM frame is fragile because one dropped RF packet out of about 24 ruins the whole frame. A 128-byte TM frame lowers that burden to 3 RF packets per GDS frame.

## The Mental Model

Do not think of the radio as a long USB cable.

The ground path looks like a byte stream at the ends:

```text
fprime-gds <-> ground Teensy <-> RF <-> satellite Teensy <-> Raspberry Pi F Prime app
```

But internally, the RF hop is packetized:

```text
F Prime bytes
-> Teensy RF message
-> RF segment 0
-> RF segment 1
-> RF segment 2
-> reassembled F Prime bytes
```

F Prime and GDS care about the F Prime/CCSDS layer. The radio only cares about short RF packets. The Teensy bridge has to make those two worlds line up.

## What Is an F Prime Com Packet?

F Prime uses comm packets to move normal ground-interface data:

- commands
- command responses
- events/logs
- channelized telemetry
- parameters

Inside F Prime, these are commonly carried in `Fw::ComBuffer`. The configured maximum payload storage for that buffer is:

```text
FW_COM_BUFFER_MAX_SIZE
```

For the RF MVP:

```text
FW_COM_BUFFER_MAX_SIZE = 96
```

This means an individual F Prime command/event/telemetry comm item must fit within that configured size and its derived buffers. F Prime derives related limits from it, for example:

```text
FW_CMD_ARG_BUFFER_MAX_SIZE = FW_COM_BUFFER_MAX_SIZE - command/header overhead
FW_LOG_BUFFER_MAX_SIZE     = FW_COM_BUFFER_MAX_SIZE - event/header overhead
FW_TLM_BUFFER_MAX_SIZE     = FW_COM_BUFFER_MAX_SIZE - channel/header overhead
FW_FILE_BUFFER_MAX_SIZE    = FW_COM_BUFFER_MAX_SIZE
```

So lowering `FW_COM_BUFFER_MAX_SIZE` helps keep downlink packets small, but it also reduces the maximum size of command arguments, event arguments, telemetry values, parameter values, and file chunks. If it is too small, F Prime can assert or silently become unusable for larger data products.

## What Is a CCSDS TM Frame?

TM means telemetry. In this project, `fprime-gds` is launched with:

```sh
--framing-selection space-packet-space-data-link
```

That means the downlink path uses CCSDS-style framing. The simplified stack is:

```text
F Prime comm packet
-> CCSDS space packet
-> CCSDS TM transfer frame
-> UART bytes
-> Teensy/RF bridge
-> UART/USB bytes
-> fprime-gds
```

The TM transfer frame is the fixed-size frame that GDS validates with a frame CRC. The configured size is:

```text
ComCfg.TmFrameFixedSize
```

For the RF MVP:

```text
ComCfg.TmFrameFixedSize = 128
```

The important point: GDS is not satisfied by "most of the bytes." It needs a complete 128-byte TM frame aligned correctly and with a valid CRC.

## Example: What Is Inside One 128-Byte TM Frame?

Important distinction:

- `missionManager.PING` itself is an uplink command. It travels ground-to-Pi inside a CCSDS TC frame.
- The 128-byte TM frame is downlink telemetry. After the Pi handles `missionManager.PING`, the downlink may contain events such as `missionManager.Pong`, `cmdDisp.OpCodeDispatched`, or `cmdDisp.OpCodeCompleted`.

This example shows a single 128-byte TM frame carrying the `missionManager.Pong` event for:

```text
missionManager.PING token = 4245
missionManager.Pong token = 4245, count = 1
```

The actual live bytes will vary because frame counters, APID sequence counts, timestamps, aggregation contents, and CRC change at runtime. The layout below is still the right mental model.

Known dictionary values for this deployment:

```text
missionManager.PING opcode = 268460033 = 0x10006001
missionManager.Pong event id = 268460033 = 0x10006001
Pong args = token: U32, count: U32
FW_PACKET_LOG APID = 0x0002
SPP_IDLE_PACKET APID = 0x07FF
SpacecraftId = 0x0044
VCID = 1
```

The frame has three big regions:

| Byte range | Size | Meaning |
| ---: | ---: | --- |
| `0..5` | 6 | CCSDS TM transfer-frame header |
| `6..36` | 31 | CCSDS space packet carrying the F Prime `Pong` log packet |
| `37..125` | 89 | CCSDS idle packet fill |
| `126..127` | 2 | TM frame CRC/FECF |

Example hex dump:

```text
000: 04 42 2A 2A 18 00 00 02 C0 15 00 18 00 02 10 00
016: 60 01 00 02 00 69 EB FC 40 00 0A A3 D4 00 00 10
032: 95 00 00 00 01 07 FF C0 00 00 52 E0 E0 E0 E0 E0
048: E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0
064: E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0
080: E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0
096: E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0
112: E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 E0 9E 09
```

Byte-by-byte interpretation:

| Bytes | Hex | Meaning |
| ---: | --- | --- |
| `0..1` | `04 42` | TM Global VCID: spacecraft ID `0x0044`, VCID `1`, OCF flag `0` |
| `2` | `2A` | Master frame count example value |
| `3` | `2A` | Virtual-channel frame count example value |
| `4..5` | `18 00` | TM data field status; first packet starts at offset 0 |
| `6..7` | `00 02` | CCSDS space-packet ID; APID `0x0002` = F Prime log/event packet |
| `8..9` | `C0 15` | Space-packet sequence flags `0b11` plus example sequence count `0x0015` |
| `10..11` | `00 18` | Space-packet data length field = `24`; means `25` bytes of packet data |
| `12..13` | `00 02` | F Prime packet descriptor = `FW_PACKET_LOG` |
| `14..17` | `10 00 60 01` | F Prime event ID = `missionManager.Pong` |
| `18..19` | `00 02` | F Prime time base example, `TB_WORKSTATION_TIME` |
| `20` | `00` | F Prime time context |
| `21..24` | `69 EB FC 40` | Example timestamp seconds |
| `25..28` | `00 0A A3 D4` | Example timestamp microseconds |
| `29..32` | `00 00 10 95` | Pong argument `token = 4245` |
| `33..36` | `00 00 00 01` | Pong argument `count = 1` |
| `37..38` | `07 FF` | CCSDS idle-packet APID used to fill the rest of the TM frame |
| `39..40` | `C0 00` | Idle packet sequence flags |
| `41..42` | `00 52` | Idle packet length field for the fill region |
| `43..125` | `E0 ... E0` | Idle fill bytes |
| `126..127` | `9E 09` | Example TM frame CRC/FECF |

The RF bridge then splits this one 128-byte TM frame into three RF payload chunks:

```text
RF segment 0: TM bytes 0..43
RF segment 1: TM bytes 44..87
RF segment 2: TM bytes 88..127
```

If segment 1 is dropped, GDS cannot recover the `Pong` event from that TM frame. It will see a bad 128-byte frame and reject it with a checksum/CRC warning. That is why per-segment ACK/retry and fixed 128-byte batching mattered.

## How the 96-Byte Com Buffer Fits Inside a 128-Byte TM Frame

The project-local config says:

```text
AggregationSize = TmFrameFixedSize - 6 - 6 - 1 - 2
```

For the RF MVP:

```text
AggregationSize = 128 - 6 - 6 - 1 - 2
AggregationSize = 113 bytes
```

Plain meaning:

- `128` is the full CCSDS TM transfer frame size.
- Some bytes are reserved for CCSDS frame/packet overhead and trailer.
- The remaining aggregation payload budget is `113` bytes.
- `FW_COM_BUFFER_MAX_SIZE = 96` leaves room inside that `113` byte budget.

This is why the chosen pair is:

```text
TmFrameFixedSize = 128
FW_COM_BUFFER_MAX_SIZE = 96
```

If the comm buffer were still `512`, it would not fit cleanly inside a 128-byte TM frame. If the TM frame were still `1024`, it would fit but would be too fragile over this RF link.

## Why Not Go Lower Than 128?

Going lower can work only if the downlink content is extremely tiny.

Example:

```text
64-byte TM frame
AggregationSize = 64 - 15 = 49 bytes
```

A 49-byte aggregation payload leaves little room for normal F Prime comm packets. Some tiny heartbeat telemetry might fit, but many events, commands, command responses, string values, file packets, or future telemetry products may not.

Also, the frame size must match everywhere:

- F Prime config
- generated dictionary
- deployed Pi binary
- `fprime-gds` dictionary
- satellite Teensy fixed-frame chunk size

If one side thinks TM frames are 128 bytes and another side thinks they are 64 or 1024 bytes, GDS will lose alignment and report checksum failures.

## Why 1024 Failed Over RF

Default-sized TM frames are reasonable on a reliable transport like TCP, USB, or a strong radio modem. They are a problem here because the RFM23BP bridge splits every TM frame into many small RF packets.

With this relay:

```text
Useful RF payload per packet = 44 bytes
```

So:

```text
1024-byte TM frame / 44 bytes = about 24 RF packets
128-byte TM frame / 44 bytes = 3 RF packets
```

A CCSDS TM frame is all-or-nothing for GDS CRC validation. If one RF packet is lost from a 1024-byte TM frame, the whole 1024-byte frame fails. The bigger the TM frame, the more RF packets must survive in order.

128 bytes is a compromise:

- small enough to reduce RF packet count
- large enough to hold normal tiny command/event/telemetry packets
- fixed-size and easy for the Teensy to batch
- still compatible with GDS CCSDS decoding

## What the Teensy Bridge Has To Do

The satellite Teensy sees raw UART bytes coming from the Pi. For downlink, it must preserve complete F Prime TM frames.

Current RF MVP behavior:

```text
satellite Pi UART bytes
-> collect exactly 128 bytes
-> split into 3 RF packets
-> require per-segment ACK/retry
-> reassemble on ground Teensy
-> write exactly 128 bytes to GDS data USB
```

The "exactly 128 bytes" part matters. A stale partial chunk is not a valid TM frame. If the satellite forwards 7 leftover bytes or 51 random bytes, GDS cannot validate them as a frame and will print checksum warnings.

That is why fixed-frame mode drops stale partial chunks instead of flushing them.

## What APID Has To Do With This

APID means Application Process Identifier. In this project, it identifies the kind of CCSDS space packet inside the TM frame.

Examples from the config:

```text
FW_PACKET_COMMAND
FW_PACKET_TELEM
FW_PACKET_LOG
FW_PACKET_FILE
FW_PACKET_PACKETIZED_TLM
SPP_IDLE_PACKET
```

The TM frame is the outer fixed-size container. APID is part of the packet metadata inside the frame.

When GDS reports APID sequence warnings, that usually means:

- GDS decoded valid CCSDS frames.
- It found space packets inside them.
- The packet sequence count jumped or skipped for one APID.

That is better than checksum spam. It means the byte stream is at least frame-decodable, but packets or frames are still being dropped under load.

## What Students Should Change Together

If you change `TmFrameFixedSize`, change and rebuild all related pieces together:

1. Update F Prime config:

   ```text
   ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/ComCfg.fpp
   ```

2. Re-check comm buffer sizing:

   ```text
   ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/FpConstants.fpp
   ```

3. Update satellite Teensy fixed-frame chunk size if needed:

   ```text
   ArtemisTeensy_N2_Baremetal/firmware/satellite_teensy/satellite_teensy.ino
   ```

4. Regenerate and rebuild F Prime so the dictionary matches the binary:

   ```sh
   cd ArtemisRpiTeensy_N2
   . fprime-venv/bin/activate
   fprime-util generate -f
   fprime-util build
   ```

5. Cross-compile and redeploy the Pi binary if testing on the FlatSat.

6. Restart `fprime-gds` with the matching dictionary.

7. Reflash the Teensy if the fixed-frame chunk constant changed.

Do not change only the GDS dictionary or only the Teensy constant. Frame-size mismatch looks like RF corruption even when the radio is working.

## Demo Rules Of Thumb

For the current RFM23BP MVP:

- Keep `TmFrameFixedSize = 128`.
- Keep `FW_COM_BUFFER_MAX_SIZE = 96`.
- Keep telemetry tiny.
- Avoid continuous high-rate events.
- Avoid standard F Prime file downlink for large files.
- Use `missionManager.PING` as the smoke-test command path.
- Treat APID sequence warnings as "frames decode but loss remains."
- Treat repeated checksum warnings as "frame alignment or integrity is broken."

For real science file downlink, do not rely on this setting alone. Use a dedicated payload downlink protocol or a stronger radio/modem path with end-to-end file chunks, sequence numbers, CRCs, and replay.

## Quick Reference

Current project-owned config:

```text
ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/ComCfg.fpp
ArtemisRpiTeensy_N2/ArtemisRpiTeensyDeployment/RfMvpConfig/FpConstants.fpp
```

Current values:

```text
ComCfg.TmFrameFixedSize = 128
ComCfg.AggregationSize = 113
FW_COM_BUFFER_MAX_SIZE = 96
FW_LOG_STRING_MAX_SIZE = 80
```

Current RF split:

```text
RF_PACKET_MAX_LEN = 49
RF_SEGMENT_HEADER_LEN = 5
RF_SEGMENT_MAX_DATA = 44
128-byte TM frame = 3 RF packets
```

Current runbook:

```text
docs/RF_MVP_DEMO_RUNBOOK.md
```
