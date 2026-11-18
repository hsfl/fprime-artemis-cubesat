# CDR TCP and stock file transfer

Use the [current plan](../planning/CDR_TCP_YAMCS_IMPLEMENTATION_PLAN.md) for scope. This profile uses stock `Drv.TcpServer`, ComCcsds and FileDownlink, with no network mux. Physical UART mux channel 2 remains the Teensy/PDU RPC interface. [Local acceptance passed October 20](../verification/evidence/n2-cdr-tcp-2026-10-20/README.md); Pi and hardware acceptance remain separate.

## Native development

From the repository root on macOS:

```sh
./tools/yamcs/build_cdr_tcp.sh
```

The native executable and matching dictionary are under `ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/`. Linux/WSL native artifacts use `Linux` instead of `Darwin`; the original Pi Zero W needs a separate ARMv6 build. Never deploy the native Mac executable to the Pi.

The spacecraft process accepts `-a <listen-ipv4> -p <tcp-port> -d <uart-device>`. Defaults are `127.0.0.1:50000` and `/dev/serial0`. For a TCP-only laptop smoke check, `-d /dev/null` explicitly disables UART. The automated acceptance below supplies a fake UART/PDU instead.

## Ground-only Yamcs

Run this against an already running CDR spacecraft process, using its matching JSON dictionary:

```sh
./tools/yamcs/run_cdr_tcp.sh --dictionary <matching-dictionary.json> --address 127.0.0.1 --port 50000
```

This starts the pinned Yamcs runtime and a separate bridge using stock F′ TCP and CCSDS decoding. File packets are verified into local files before UDP, then a background worker publishes complete files to the bucket using HTTP and independently verifies readback. Nonfile packets use a dedicated complete-packet input on UDP50000; stock channel fanout retains UDP50002, and uplink uses UDP50001. No upstream framework/JAR is patched. See [ground configuration and recovery](../../deploy/ground/README.md). It launches no spacecraft process or custom channel-1 receiver. Each run has its own config, database, local files, receipts and manifest under ignored `yamcs/run/`. Open `http://127.0.0.1:8090` and select `fprime-project` / `realtime`; use `--http-port` for another listener. Ctrl-C stops both owned process groups.

The CDR configuration sets the EventManager queue to 128 entries, retaining short diagnostic/command bursts that could overflow the stock ten-entry droppable queue. Other core queue sizes and priorities remain unchanged.

For eventual Pi 5 LAN use, select the Zero W's reachable IPv4 address and the appropriate Yamcs `--http-bind`. Host deployment and access configuration remain separate work. The pinned Yamcs UDP receive links bind wildcard sockets; the bridge destinations stay on loopback. The launcher records this dependency limitation rather than claiming an unsupported bind setting.

## File transfer

### Yamcs operator sequence

Use the matching flight dictionary and select the retained **CSV** product ID from the capture completion event. For the private hardware command/capture sequence, use the consuming deployment's operator runbook; this public profile owns storage and downlink only.

1. In **Commanding**, issue `storageManager.SELECT_PRODUCT` with `productId = <CSV_ID>`.
2. Copy `sourcePath` from the matching `storageManager.ProductSelected` event. This is the spacecraft-side path, not a path on the ground Pi.
3. Issue `FileHandling.fileDownlink.SendFile` with `sourceFileName = <exact sourcePath>` and `destFileName = <fresh name>.csv`. Do not include quotes in the Yamcs argument fields. Use a different destination for every attempt.
4. Watch the bridge log and `<station>/received-files/<destination>.receipt.json`: **receiving → saved locally → available in Yamcs**. Retrieve the published object at **Storage → Buckets → fprimeFilesIn → <destination>**. The verified local file remains available independently of Yamcs. Native File Transfer rows are not part of this workflow.

Use values from the current capture, not an earlier test's product ID:

```text
storageManager.SELECT_PRODUCT
  productId: <CSV_ID from the current capture completion event>

FileHandling.fileDownlink.SendFile
  sourceFileName: <sourcePath returned by ProductSelected>
  destFileName: <fresh name for this CSV and attempt>.csv
```

Product IDs, paths, sizes and checksums change with each capture. Copy them from current commit/selection events. The CSV payload workflow retains acquisition ID/run as `daq_<ID>_run_<RUN>_product_<PRODUCT_ID>.csv`; generic products and legacy N2P1 products remain supported. Never rename catalog files manually. Use the helper for independent catalog size/CRC and local SHA-256 checks, plus an optional independent source hash for the particular product.

### Verified helper

Instead of issuing `SendFile` manually, request one transfer with the ground helper:

```sh
./tools/yamcs/downlink_stock.sh --station <station-dir> --source-file source/sample.csv --expected-file <local-integrity-oracle.csv>
./tools/yamcs/downlink_stock.sh --station <station-dir> --product-id <stored-product-id>
```

The source path is passed unchanged to F′; a relative source is relative to the spacecraft process's working directory. Stock source/destination names are limited to 100 UTF-8 bytes in this dictionary. The local oracle is used only for comparison and is never uploaded into Yamcs. Source-path mode requires a local oracle or explicit expected size plus SHA-256/CRC; product mode obtains size/CRC from a fresh `StorageManager.ProductSelected` event. `--expected-file` can also supply a separate SHA/byte oracle in product mode.

The CSV helper checks the live idle disk receiver, issues stock SendFile once, waits for the matching local receipt, independently GETs the bucket object and saves PASS/FAIL evidence. It never queries or creates native transfer records. Use the persistent station directory or the launcher's run directory with `--station`; `--receive-dir` selects a custom spool. Saved local authentication is used when present. New labelled source basenames retain acquisition ID/run and unique product ID in the generated destination. Give legacy CSV sources an explicit fresh `.csv` destination if their catalog path ends in `.bin`.

If publication fails, retry the verified local receipt with `local_file_receiver.py --station <station-dir> --url <local-yamcs-url> --retry <receipt.json>`. This sends no spacecraft command. A mismatched existing bucket object is never overwritten. Incomplete/invalid `.part` files never publish. `FileSent` or command OK alone is not delivery proof. Before a fresh spacecraft request after interruption, stop/drain the old sender using the [ground recovery procedure](../../deploy/ground/README.md); automatic spacecraft retries are not performed.

The default simulator dictionary exposes collection-only `scienceApp.SCIENCE_CAPTURE(durationMinutes)` and accepts 1 or 2 minutes. The simulator generates the corresponding 60 or 120 seconds of exposure data. A build with `NEUTRON2_CONFIGURED_CAPTURE=ON` exposes `scienceApp.CONFIGURE_CAPTURE_DURATION(durationMinutes)` followed by `scienceApp.SCIENCE_CAPTURE()`. The private hardware backend accepts finite durations of 1–216 minutes. Wait for the configuration command to complete before issuing capture. Use the dictionary from the selected build and wait for the matching `ScienceProductCommitted` event before running the stock helper.

The public startup parameter `scienceApp.CAPTURE_DURATION_MINUTES` defaults to 1 minute and has a new parameter ID. Set and save it again if you previously customized the seconds parameter. Scheduled collection delays still use seconds.

Existing `COLLECT_AND_DOWNLINK`/`VERIFIED_COLLECT_AND_DOWNLINK` workflows belong to the separate custom channel-1 baseline. CDR rejects those commands before capture or token allocation.

## Reuse in another CDR deployment

Add the public F′ project root to the consuming project's F′ library
locations. Its `library.cmake` registers the shared components. Register
the selected payload manager and driver in the consuming project.

- Include `Top/instances.shared.fppi` inside the
  `ArtemisRpiTeensyDeployment` module and register `Top/instances.cdr_tcp.fpp`
  for the TCP instance and rate-group indices.
- Include `Top/topology.cdr_tcp.shared.fppi` inside the topology and add the
  payload instance membership and bindings. The shared wiring expects
  `payloadManager.run`, `payloadManager.operationNoticeOut`, and
  `payloadManager.sohStatusOut`.
- Register `CdrTcpConfig` and retain the CDR topology dependencies on it,
  `Fw_Logger`, and `Drv_TcpServer`. The public
  `Top/topology.cdr_tcp.fpp` is the simulator entry point.

## Local acceptance

```sh
./tools/yamcs/.venv/bin/python tools/yamcs/test_cdr_tcp_acceptance.py \
  --binary ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/bin/ArtemisRpiTeensyDeployment \
  --dictionary ArtemisRpiTeensy_N2/build-artifacts/Darwin/ArtemisRpiTeensyDeployment/dict/ArtemisRpiTeensyDeploymentTopologyDictionary.json
```

This starts an actual native F′ process and Yamcs on loopback with a fake PTY/PDU, uses both supplied sample files without rewriting them, and tests receipt integrity, a CSV above 5 MiB, reconnect/failure behavior, actual HTTP outage/disk-only retry, zero native download records and concurrent channel-2 RPC. It opens no physical serial device or radio. Results and failed attempts are retained under `yamcs/run/cdr-acceptance-*`; a successful native run is not two-Pi or real payload/PDU acceptance.
