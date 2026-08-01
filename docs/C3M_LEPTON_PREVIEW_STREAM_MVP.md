# C3M Lepton Preview Stream MVP

## BLUF

The MVP adds a **Lepton-first live thermal preview**, not another science
downlink. It streams the newest available `80x60` U8 preview frame to the
ground on a best-effort basis. The preview and the normal C3M science transfer
are mutually exclusive.

## Contract

- The source is the real connected Lepton. The preview is `80x60` U8; it is
  not the current full-resolution Lepton `.fdp` science product.
- The satellite holds exactly one newest-frame slot. A new frame replaces the
  old one; there is no ring buffer, backlog, or replay queue.
- A received preview may be complete or partial. The ground renderer keeps
  pixel positions: missing pixels render white.
- The payload receiver shows the latest frame on both the Current dashboard and
  a larger Livestream tab. Preview pixels use an auto-ranged thermal palette;
  missing pixels remain white on partial frames.
- Preview transport sends no repair/retry requests. Loss is visible, not
  silently filled from a later frame.
- Preview traffic is distinct from Neutron 2 science products and from the
  normal C3M `.fdp` science/downlink path. It must not enter the science
  storage, CRC/reconstruction, or payload-viewer contract.
- Starting preview while science is active, or science while preview is active,
  is rejected or deferred until the active operation finishes. The operator
  must run only one of them at a time.

## One-run HIL acceptance

With a Lepton connected and the full C3M RF chain running, perform one preview
run. It passes when the ground shows an `80x60` frame, visibly marks any lost
pixels white, and the run neither requests preview retries nor starts a science
transfer. Then verify the normal science flow can be started only after preview
has stopped.

This is a one-HIL-run MVP gate, not a reliability or image-quality claim.

## Bench identity and setup

Use the normal C3M bench: connected Lepton, powered satellite Pi/Teensy and
ground Teensy, antennas attached, and the ground Teensy connected to the
operator laptop. On macOS, enumerate USB devices before flashing:

```bash
arduino-cli board list
```

- Ground Teensy upload ID: `usb:100000`.
- Re-enumerate the satellite Teensy immediately before upload; its historical
  ID is `usb:2100000` (often shortened in conversation to `usb:200`), but the
  currently attached device is authoritative.

On Windows, run the same Arduino/F' developer commands in WSL; use the WSL USB
attachment that corresponds to the same physical boards. The browser-based
ground preview remains a normal local browser surface.

For the standard C3M GDS and payload-receiver launch, see
[`EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md`](EPSCOR_C3M_LEPTON_RF_MVP_RUNBOOK.md).

## Operator quick start

From the repository root on the ground laptop:

```bash
./tools/c3m
```

This opens the F' GDS at `http://127.0.0.1:5050/` and the payload viewer at
`http://127.0.0.1:8064/`. In the GDS Commanding view, send:

```text
payloadStreamApp.START_STREAM
```

Open the viewer's **Livestream** tab for the large thermal-color view. The
Current tab retains a smaller copy. Stream status and shutdown commands are:

```text
payloadStreamApp.GET_STREAM_STATUS
payloadStreamApp.STOP_STREAM
```

Stop preview before starting the reliable `.fdp` science flow. Press `Ctrl-C`
in the `./tools/c3m` terminal to stop both ground applications cleanly.

## Accepted HIL result (2026-07-31 HST)

- The final Pi release is
  `/home/pi/artemis/releases/lepton-preview-mvp-final-20260801T002651Z`
  (binary SHA-256
  `ed443c037020228d6959d7261b99f97d93c12013b7ad787a49f2fe86f20062da`).
- Ground `usb:100000` and satellite `usb:2100000` were flashed and verified by
  their `GDS_Teensy` and `ArtemisTeensy` boot/counter signatures.
- The real Lepton produced consecutive `80x60` U8 frames with 4,800/4,800
  bytes, 200/200 fragments, and valid CRCs. Frames normally arrived about two
  seconds apart.
- A complete frame requires 200 best-effort RF records because the preview
  header leaves 24 pixel bytes per record. An occasional lost local response
  adds the five-second uploader timeout before the next fresh frame, so a
  recovered update can take roughly seven to eight seconds without retrying the
  abandoned frame.
- A lost Pi-to-Teensy local response dropped that frame after five one-second
  ticks and automatically advanced to a new frame/session. It did not resend
  or repair the abandoned preview frame.
- `missionApp.PING` and `sohApp.EMIT_SOH_SNAPSHOT` completed while preview was
  active. Preview was then stopped before science collection.
- The post-preview science regression delivered 38,482/38,482 bytes in 1,100
  packets with zero retry rounds and a matching CRC in 11.2 seconds. Manual
  decode confirmed the normal `160x120`, 19,200-pixel Lepton `.fdp` product at
  `data/c3m_20260801_002926_transfer_1/`.
