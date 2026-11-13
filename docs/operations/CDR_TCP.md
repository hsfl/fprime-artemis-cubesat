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

This starts Yamcs and a separate ground bridge using the packaged stock F′ TCP client, CCSDS framing and PacketBridge. A small UDP adapter wrapper spaces outgoing TM frames by at least 20 ms (configurable with `--frame-interval-ms`) without catch-up bursts. The persistent ground Pi also uses at least 4 MiB UDP receive buffers; see [ground installation and rollback](../../deploy/ground/README.md). FileDownlink and the TCP wire format stay stock. It does not launch a simulated spacecraft, science viewer, or custom channel-1 receiver. Open `http://127.0.0.1:8090` and select `fprime-project` / `realtime`. Each run gets its own config, database, received-file mirror and manifest under ignored `yamcs/run/`. Ctrl-C stops both owned process groups.

For eventual Pi 5 LAN use, select the Zero W's reachable IPv4 address and the appropriate Yamcs `--http-bind`. Host deployment and access configuration remain separate work. The pinned Yamcs UDP receive links bind wildcard sockets; the bridge destinations stay on loopback. The launcher records this dependency limitation rather than claiming an unsupported bind setting.

## File transfer

### Yamcs operator sequence

Use the matching flight dictionary and select the retained **CSV** product ID from the capture completion event. For the private hardware command/capture sequence, use the consuming deployment's operator runbook; this public profile owns storage and downlink only.

1. In **Commanding**, issue `storageManager.SELECT_PRODUCT` with `productId = <CSV_ID>`.
2. Copy `sourcePath` from the matching `storageManager.ProductSelected` event. This is the spacecraft-side path, not a path on the ground Pi.
3. Issue `FileHandling.fileDownlink.SendFile` with `sourceFileName = <exact sourcePath>` and `destFileName = <fresh name>.csv`. Do not include quotes in the Yamcs argument fields. Use a different destination for every attempt.
4. Wait for **File Transfer → COMPLETED**. Retrieve the file at **Storage → Buckets → fprimeFilesIn → <destination>**. On the persistent ground station it is also mirrored at `/home/n2-fprime-yamcs/yamcs-station/received-files/<destination>`.

Use values from the current capture, not an earlier test's product ID:

```text
storageManager.SELECT_PRODUCT
  productId: <CSV_ID from the current capture completion event>

FileHandling.fileDownlink.SendFile
  sourceFileName: <sourcePath returned by ProductSelected>
  destFileName: <fresh name for this CSV and attempt>.csv
```

Product IDs, paths, sizes, and checksums change with each capture. Copy them from the matching current commit/selection events. Older CSV sources may have historical `.bin` extensions; future CSV commits use `.csv` after deploying the filename update. Do not rename old catalog files manually. A manual COMPLETED status is the stock receiver result; use the helper below for independent size/CRC verification and an optional expected SHA-256 for that specific product.

### Verified helper

Instead of issuing `SendFile` manually, request one transfer with the ground helper:

```sh
./tools/yamcs/downlink_stock.sh --source-file source/sample.csv --expected-file <local-integrity-oracle.csv>
./tools/yamcs/downlink_stock.sh --product-id <stored-product-id>
```

The source path is passed unchanged to F′; a relative source is relative to the spacecraft process's working directory. Stock source/destination names are limited to 100 UTF-8 bytes in this dictionary. The local oracle is used only for comparison and is never uploaded into Yamcs. Source-path mode requires a local oracle or explicit expected size plus SHA-256/CRC; product mode obtains size/CRC from a fresh `StorageManager.ProductSelected` event. `--expected-file` can also supply a separate SHA/byte oracle in product mode.

The helper requests one download through Yamcs's stock File Transfer API, waits for its transfer ID to complete, independently downloads the reconstructed `fprimeFilesIn` object, and saves it with PASS/FAIL evidence. On the persistent station, pass `--station ~/yamcs-station` to use saved local authentication. Product mode defaults to a unique destination preserving the selected source's recognized `.csv` or `.bin` extension (otherwise `.bin`). New StorageManager commits retain CSV as `product_<ID>.csv` and binary/opaque data as `product_<ID>.bin`. Existing legacy CSVs can retain their `.bin` source names; supply an explicit `.csv` `--destination` for those. Always copy `ProductSelected.sourcePath` exactly. A reported receive SHA is source-verified only when an independent expected SHA/oracle was supplied. `FileSent` or command OK alone is not delivery proof. Only one helper runs per station/service, and an existing nonterminal download prevents a new request. Missing files, missing/partial receipt or mismatched integrity fail; stock transfer has no selective retry/resume guarantee. Timeouts do not cancel the stock receiver or resend automatically. Before a manual retry, stop the old sender and clear/drain its transport state as described in the [ground recovery procedure](../../deploy/ground/README.md), then use a fresh destination.

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

This starts an actual native F′ process and Yamcs on loopback with a fake PTY/PDU, uses both supplied sample files without rewriting them, and tests receipt integrity, reconnect/failure behavior and concurrent channel-2 RPC. It opens no physical serial device or radio. Results and failed attempts are retained under `yamcs/run/cdr-acceptance-*`; a successful native run is not two-Pi or real payload/PDU acceptance.
