# Persistent Yamcs ground station

The Linux ground Pi uses the community F′–Yamcs file receiver and packaged communication adapters. The path is `StorageManager → F′ FileDownlink → TCP → ground bridge → loopback UDP → Yamcs fprimeFilesIn`. F′ sends START/DATA/END file packets; the community receiver checks its modular checksum and writes the reconstructed object. No Yamcs core, F′ framework, community integration JAR, or flight topology patch is required.

The project bridge spaces TM frames by at least **20 ms** (at most 50 frames/s), scheduled from the preceding write's completion so delays cannot cause catch-up bursts. The separate telemetry splitter still re-injects channel packets on UDP port 50002. Both UDP intakes need sufficient receive buffers. The earlier 200 ms pacing-only retry timed out with 14 main-link drops; it is not acceptance evidence.

## Install receive-buffer settings

These settings apply to the Linux ground host, including newly opened sockets belonging to other processes. They are not flight settings. The 4 MiB default supplies queue capacity during Yamcs processing pauses; increasing only `rmem_max` does not enlarge default sockets. Existing sockets must be reopened.

Before installation, record the running values below, save any existing `/etc/sysctl.d/90-n2-yamcs-udp.conf`, and back up the installed ground service, helper, and station YAML files for rollback:

```sh
cat /proc/sys/net/core/rmem_default /proc/sys/net/core/rmem_max
```

From the ground checkout root, generate the installed configuration from the supplied minimum targets, preserving larger running values:

```sh
sudo python3 - <<'PY'
from pathlib import Path

template = Path("deploy/ground/90-n2-yamcs-udp.conf")
settings = {}
for line in template.read_text().splitlines():
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    name, value = (part.strip() for part in line.split("=", 1))
    running = int((Path("/proc/sys") / name.replace(".", "/")).read_text())
    settings[name] = max(running, int(value))
settings["net.core.rmem_max"] = max(settings.values())
Path("/etc/sysctl.d/90-n2-yamcs-udp.conf").write_text(
    "# Neutron 2 Yamcs UDP buffers; larger existing values preserved.\n"
    + "".join(f"{name} = {value}\n" for name, value in settings.items())
)
PY
sudo /sbin/sysctl -p /etc/sysctl.d/90-n2-yamcs-udp.conf
```

Preserve the live station's other settings when updating `config/etc/yamcs.fprime-project.yaml`: set the existing `FprimeFilePacketService.args.downloadTimeoutMs` to `30000`. This timeout covers waiting for START, not stalls after START. Install the matching `cdr_comm_bridge.py` and `n2-ground-bridge.service` (20 ms), plus the updated helper and its project authentication client.

During an operator-approved ground maintenance window, stop the bridge, restart Yamcs, then start the bridge after Yamcs has opened its intake sockets:

```sh
sudo systemctl stop n2-ground-bridge
sudo systemctl daemon-reload
sudo systemctl restart n2-yamcs
ss -uanm '( sport = :50000 or sport = :50002 )'
sudo systemctl start n2-ground-bridge
journalctl -u n2-ground-bridge -n 15 --no-pager
```

Require both sockets to appear with `skmem` receive limits (`rb`) of at least `4194304`, and the bridge log to show `interval=20.000ms`. Reopening sockets after changing the OS default is essential. Record drop counters from both `/proc/net/udp` and `/proc/net/udp6` before and after an acceptance transfer; hexadecimal ports `C350` and `C352` are 50000 and 50002. A new socket has a new inode and counter baseline.

## Request and verify a retained CSV

The existing `stock_downlink.py` resolves the product through `storageManager.SELECT_PRODUCT`, then creates one DOWNLOAD using the stock Yamcs File Transfer API. It uses `FprimeFilePacketService`, the `fprimeFilesIn` bucket, and a unique destination preserving the selected source's `.csv`/`.bin` extension. The service issues the stock `FileHandling.fileDownlink.SendFile` command. No payload command, conversion, or acquisition is sent by the helper.

Take `csvProductId` from the current TX's `RawTxProductsCommitted` event (or the consuming deployment's CSV completion event). On the ground Pi, enter that ID when prompted. Do not reuse an earlier test's ID, size, or hash:

```sh
printf 'CSV product ID from the current completion event: '
read -r CSV_PRODUCT_ID
tools/yamcs/.venv/bin/python tools/yamcs/stock_downlink.py \
  --station "$HOME/yamcs-station" \
  --product-id "$CSV_PRODUCT_ID" \
  --destination "science-product-${CSV_PRODUCT_ID}-$(date -u +%Y%m%dT%H%M%SZ).csv"
```

The helper obtains the selected product's actual path, size, and CRC from its catalog event. Add `--expected-sha256` only when you have an independent hash for that same product. It reads the saved station authentication without printing credentials, serializes helper requests, refuses a nonterminal download before selection, and never retries a possibly submitted request. Its defaults are 30 seconds without progress and 300 seconds overall. Each invocation uses a fresh destination unless an explicit unique `--destination` is supplied.

Monitor **File Transfer** in Yamcs. Retrieve completed objects under **Storage → Buckets → fprimeFilesIn**. The helper independently GETs the completed bucket object, verifies its size and the selected product's CRC16, and compares SHA-256 when supplied. It saves received bytes and a JSON receipt with the product/transfer identity, destination, timing, and integrity results. A stock COMPLETED state or flight `FileSent` event alone does not establish independently verified receipt. The station's disk mirror is secondary; bucket receipt is the acceptance source.

New StorageManager commits use `product_<ID>.csv` for CSV sources and `product_<ID>.bin` for binary/opaque input. The helper preserves recognized source extensions in its generated destination. Existing products are not renamed: legacy CSV products can retain `.bin` paths, which is why the CSV workflow explicitly supplies a fresh `.csv` destination. Treat `ProductSelected.sourcePath` as authoritative and never rename catalog files manually. Retain both onboard BIN and CSV products.

This corrects filenames, not the stock bucket MIME type: the community file receiver still stores objects as `application/octet-stream`. No upstream MIME-handling change is included.

## Failure and manual recovery

`transfer_watchdog.py` only observes transfer progress and posts `TransferStalled` warnings in Yamcs **Events** (source `n2-ground-transfer-watchdog`) and the `n2-yamcs` journal after 30 seconds idle or 300 seconds overall. It does not cancel, resend, restart, or alter the stock transfer status. Stock transfers can remain RUNNING after a stall because the receiver has no active-transfer timeout or supported cancellation.

On helper timeout, failed transfer, or integrity mismatch, keep its receipt and logs. Do not immediately send another download: DATA/END packets have no transfer ID, and an old sender or queued tail can contaminate the next attempt. An operator must stop the previous F′ file sender using the stock FileDownlink cancellation procedure, confirm the sender is no longer active and its queued data has drained, and clear stale ground receiver state during maintenance before retrying. Restarting ground services alone does not prove the flight sender stopped. A stale helper lock may be removed only after confirming its process is gone and this recovery is complete. Always use a fresh destination. No automatic recovery command is issued by the helper or observer.

For rollback, restore the saved ground files and previous persistent sysctl file (or remove the newly introduced file if none existed). Restore **both recorded running** `net.core.rmem_default` and `net.core.rmem_max` values with `/sbin/sysctl -w`; removing the file alone does not restore running settings. Repeat the ground restart sequence to reopen sockets with those values. Keep the station data, buckets, received files, and transfer evidence.

## Acceptance

Reuse existing ground-tool checks, then trigger three sequential helper invocations for the CSV ID selected from the current capture. For each, require stock COMPLETED, helper PASS, the selected product's exact size/CRC (plus an independent source SHA-256 when available), and zero added drops on both UDP intake sockets. Confirm telemetry continues and the observer remains informational. Preserve all three receipts. If acceptance fails, retain evidence and report the bottleneck; do not introduce an upstream patch or automatic resend. This qualifies the tested CSV only, not maximum-size files or higher throughput.

### Confirmed live result — November 12, 2026

Historical evidence only: the three sequential product 6 downloads passed (Yamcs IDs 2, 3, 4), each matching 605,968 bytes, catalog CRC16 21453, and source SHA-256 `3e100c0f9141cc97e0b7f52b59364ce6e52d521f103751b4e81a2bea72c1e54b`. These values identify that bench product, not a future capture. Both UDP intakes had zero additional drops during each fresh transfer, and ordinary telemetry continued after the last one. Dennis also confirmed a manual `FileHandling.fileDownlink.SendFile` download. An earlier stale sender was cancelled with stock FileDownlink.Cancel and a manual ground-bridge reconnect before these fresh trials. No upstream code or flight restart was needed.

These results establish the exercised payload command/telemetry, acquisition-return/storage, and CSV downlink happy path. Physical PDU-to-payload power integration and broader fault/recovery qualification remain open. The new storage extension behavior requires deploying the updated StorageManager; these successful transfers used the legacy product 6 path.
