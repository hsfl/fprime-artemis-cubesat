# Neutron 2 Payload Viewer

BLUF: this is the ground-side GUI for reconstructed Neutron 2 neutron-count
payload CSV files, including CSV bytes that arrive in a `.bin` downlink product.

It is intentionally separate from `fprime-gds`:

- `fprime-gds` shows commands, events, telemetry, and transfer progress.
- The payload receiver/downlink helper reconstructs the opaque payload blob into
  a file.
- This viewer opens neutron-count CSV content from either `.csv` files or `.bin`
  payload files and gives operators a quick science-data review screen.

## Run

From the repo root:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py
```

On Windows PowerShell:

```powershell
py -3 ground-station\neutron2-payload-viewer\neutron2_payload_viewer.py
```

Default behavior:

- scans the OS temp capture directory:
  - macOS/Linux: `/tmp/neutron_payload_captures`
  - Windows: `%TEMP%\neutron_payload_captures`
- opens `http://127.0.0.1:8062`
- shows newest supported payload first
- watches for new `.csv` and `.bin` products and automatically opens the newest
  file when it contains neutron-count CSV content
- always reminds operators that capture files are run artifacts to archive/delete
  as needed
- warns more strongly when old downlink CSVs are accumulating

Open one file directly:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --file /path/to/reconstructed/neutron_capture.csv
```

Downlinked `.bin` that contains CSV bytes works the same way:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py \
  --file /tmp/neutron_payload_captures/latest_payload.bin
```

Windows:

```powershell
py -3 ground-station\neutron2-payload-viewer\neutron2_payload_viewer.py `
  --file C:\path\to\reconstructed\neutron_capture.csv
```

Run without opening a browser automatically:

```bash
python3 ground-station/neutron2-payload-viewer/neutron2_payload_viewer.py --no-open
```

Windows:

```powershell
py -3 ground-station\neutron2-payload-viewer\neutron2_payload_viewer.py --no-open
```

## What Operators See

- rows captured
- total neutron counts
- mean and max count rate
- SAA row count and background row count
- SAA mean versus background mean
- counts-vs-time plot with SAA intervals highlighted
- first rows table for sanity checking the file
- bottom data notes explaining CSV fields, summary metrics, and how to interpret
  the SAA/background regions

## File Format

The viewer expects CSV content with:

```csv
t_s,counts,flag
0,8,SAA
1,8,SAA
```

This matches `external/payload-neutron-simulation/neutron_data.csv` and the capture files emitted by the RPi-hosted simulator.

The downlink transport is filetype-agnostic: it moves bytes and includes a
`product_id`, but it does not currently include a filename, extension, MIME type,
or schema tag. The viewer therefore treats `.bin` as a possible CSV container and
sniffs the decoded content for the Neutron 2 CSV columns.

## Compatibility

- macOS: Python 3.10+ with Safari/Chrome/Firefox.
- Windows: Python 3.10+ with Edge/Chrome/Firefox.
- Dependencies: Python standard library only. No `pip install` step.
- Network use: local loopback only, default `127.0.0.1:8062`.

## File Cleanup

Downlink/reconstructed payloads should be treated as run artifacts. The payload
simulator allocates incrementing filenames and will not overwrite an existing
CSV. If a base filename already exists, the next file becomes:

```text
neutron_capture_YYYYMMDDTHHMMSSZ_00000_00600_001.csv
neutron_capture_YYYYMMDDTHHMMSSZ_00000_00600_002.csv
```

The default capture folder is an OS temp location, but automatic cleanup is not
an operations guarantee. Treat these files as run artifacts. The viewer shows a
persistent cleanup reminder, and when the folder gets crowded it shows a stronger
cleanup notice. For simulator products on the RPi, operators can use:

```text
StorageManager.REMOVE_OLD_DATASETS(confirm=1)
```

For ground laptop files, archive or delete old payloads from the capture directory
after the demo/test run.
