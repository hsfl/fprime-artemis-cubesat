# Ground file delivery

Current source receives stock FileDownlink packets locally before UDP. Complete files are verified on disk, uploaded to `fprimeFilesIn` through the stock Yamcs HTTP API, and read back for SHA-256 comparison. Telemetry/events continue through the dedicated Yamcs complete-packet input on UDP 50000; the stock channel fanout retains UDP 50002, and command frames return on UDP 50001. Mixed frames and packets spanning frames are decoded by the packaged F Prime CCSDS decoder. The flight FileDownlink protocol is unchanged.

This revision is for local development/verification until explicitly deployed. Host tests do not establish Pi or payload hardware acceptance.

## Components and configuration

Install matching versions of `tools/yamcs/cdr_comm_bridge.py`, `local_file_receiver.py`, `file_delivery_config.py`, `stock_downlink.py`, the shared `yamcs_client.py` authentication client, `deploy/ground/station.py`, and `n2-ground-bridge.service`. Use the project's pinned Python environment and matching flight dictionary. The obsolete native-transfer watchdog is removed; the existing authenticated HTTP client is shared directly.

`station.py` preserves operator configuration while setting the supported HTTP `maxContentLength` to at least 128 MiB and the `fprimeFilesIn` bucket to at least 2 GiB/4096 objects. Larger existing settings are retained. The local receiver defaults to 128 MiB per file and 2 GiB total spool; it stops accepting files when quota/entry capacity is exhausted. Keep sufficient free disk space and remove archived evidence deliberately. No automatic eviction occurs. No upstream JAR is patched.

The unused frame-pacing wrapper/option is removed; bulk files are received before UDP. Existing `90-n2-yamcs-udp.conf` settings remain useful for telemetry receive buffers. Increasing only `rmem_max` does not enlarge default sockets; a supported maintenance restart must reopen sockets after changing `rmem_default`. Keep the deployed station's configuration/data backups before an upgrade.

## Request a product

Read the product ID from the current capture's completion event. On the ground host, run from this repository:

```sh
tools/yamcs/downlink_stock.sh \
  --station "$HOME/yamcs-station" \
  --product-id <CURRENT_PRODUCT_ID>
```

The helper selects the product, takes its exact catalog path/size/CRC, checks that the local receiver is live and idle, and sends `FileHandling.fileDownlink.SendFile` once. It waits for a matching local receipt, then independently GETs the bucket bytes and checks size/CRC and local SHA-256. An independently known hash may be supplied with `--expected-sha256`; `--expected-file` is a local byte oracle and is never sent to the spacecraft. `--receive-dir` selects another bridge spool when testing locally.

For manual commanding: send `storageManager.SELECT_PRODUCT`, copy `ProductSelected.sourcePath`, and use it verbatim as `SendFile.sourceFileName`. Choose a fresh plain filename for `destFileName`. Keep acquisition ID, run and unique product ID in the name when available. The helper preserves acquisition-labelled source names by default. Legacy catalog paths remain valid and must not be renamed manually.

Watch the bridge log and `<station>/received-files/<destination>.receipt.json`:

1. `receiving`: a `.part` is being written; sequence, offsets and size are checked.
2. `saved_locally`: exact size and END checksum passed; a complete local file and SHA-256 are retained.
3. `available_in_yamcs`: HTTP publication and independent whole-object readback agree.

Retrieve published objects under **Storage → Buckets → fprimeFilesIn**. Native File Transfer rows, their active-transfer guard and their old watchdog are not consulted. Command OK or `FileSent` alone is not receipt.

## Failures and publication retry

Invalid, interrupted, duplicate/out-of-order, size-mismatched or checksum-invalid streams never publish. Failed `.part` files and receipts remain for diagnosis. Receiver restart fails unfinished reception; a verified finalization marker permits recovery of completed local files across the final rename/receipt crash window.

One background worker publishes files; HTTP latency/failure does not block TCP reception or telemetry forwarding. Publication failure leaves the verified local copy and a `publication_error`. Retry just the publication:

```sh
tools/yamcs/.venv/bin/python tools/yamcs/local_file_receiver.py \
  --station "$HOME/yamcs-station" \
  --retry "$HOME/yamcs-station/received-files/<destination>.receipt.json"
```

Retry first checks whether the same object already exists and matches; it refuses to overwrite a different object. It never issues a spacecraft command. A quota or HTTP size rejection requires correcting configuration/capacity before retrying.

Do not overlap spacecraft file sends. After a failed spacecraft send, preserve evidence and explicitly stop/drain the old sender before another request with a fresh destination. Stock `FileHandling.fileDownlink.Cancel` remains the flight cancellation command. Restarting ground alone does not establish that the sender stopped. Existing stale native rows are a separate maintenance concern.

## CSV status and deliberate cleanup

```sh
tools/yamcs/.venv/bin/python tools/yamcs/science_files.py --station "$HOME/yamcs-station" status
tools/yamcs/.venv/bin/python tools/yamcs/science_files.py --station "$HOME/yamcs-station" archive --destination <archive-directory>
tools/yamcs/.venv/bin/python tools/yamcs/science_files.py --station "$HOME/yamcs-station" retire --product-id <CSV_ID> --receipt <verified-CSV.receipt.json>
```

`archive` and `retire` default to preview; `--confirm` performs the displayed action. Ground archive verifies and fsyncs another copy outside the active spool before removing the originals. Flight retirement requires a matching verified CSV copy and uses normal F Prime commanding; a lost response reports UNKNOWN and requires catalog/event reconciliation. Retired IDs are never reused. The payload workflow retains one CSV per acquisition; temporary input is not a separate catalog product. Legacy files remain readable until explicitly retired.

Malformed receipts are quarantined separately. A ground disk fault disables file admission while telemetry/events/commanding continue; repair the disk and restart the bridge before receiving files again. HTTP requests have total time/size bounds; failed publication remains retryable from disk. Status reports heartbeat freshness, file admission/fault and spool/catalog headroom.

## Local verification

Run tests using the existing ground Python environment:

```sh
cd tools/yamcs
./.venv/bin/python -m unittest test_cdr_tcp_tools test_delivery_helper test_local_file_receiver test_file_delivery_config test_science_files
```

The native acceptance script uses the actual flight binary, stock SendFile command, local TCP and a fake UART/PDU. It verifies supplied fixtures, an object above 5 MiB, interrupted/reconnected delivery, retained-product retrieval, ongoing fake PDU activity, and zero native download records:

```sh
tools/yamcs/.venv/bin/python tools/yamcs/test_cdr_tcp_acceptance.py \
  --binary <NATIVE_CDR_BINARY> --dictionary <MATCHING_DICTIONARY> --http-port 0
```

Receiver tests additionally cover missing/corrupt/incomplete/reordered packets, mixed/fragmented frames, slow/offline HTTP publication, retry from disk, exclusive ownership, and crash-window recovery. Evidence belongs in ignored run/build directories, not version control.
