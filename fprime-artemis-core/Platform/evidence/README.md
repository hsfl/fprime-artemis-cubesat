# Teensy target memory evidence

Both fresh Teensy41 target builds linked on 2026-11-11 HST with F′ v4.4.2
(f22ce8f), fprime-zephyr f97eaaf, Zephyr v4.3.0, SDK 0.17.4. The companion JSON
files identify each ELF by SHA-256. No physical board was flashed or exercised.

The external builds are under the workspace's ignored
`.workspace-local/artemis-twin-target-build/{rfm23,usb}`. Full build logs are
`/tmp/artemis-twin-target-build.log` and `/tmp/artemis-twin-target-usb-build.log`.
The link script proves one 196,608-byte NOLOAD cache at 0x20280000. Cache validity
is initialized independently; retained bytes are not trusted after reset.

The main 512 KiB RAM region and OCRAM2 name alias the same addresses. Do not add
them twice. The cache uses 192 KiB of the separately partitioned 256 KiB OCRAM.
128 KiB DTCM and 128 KiB ITCM complete the physical 1 MiB inventory. Static main
RAM includes the reserved dynamic-thread stack pool; malloc requests during
startup/operations still consume the remaining main address space. These JSON
numbers do not establish live free heap, stack peaks or hardware readiness.

To repeat from the repository root, supply an installed Zephyr SDK, a Python
environment with the pinned framework requirements + west + Zephyr base tools,
and an external build directory:

```sh
python3 fprime-artemis-core/Platform/build_target.py \
  --sdk /path/to/zephyr-sdk-0.17.4 \
  --tools-bin /path/to/target-venv/bin \
  --build-dir /path/outside/source/teensy-rfm23 --ground-link RFM23
```

Repeat with a separate build directory and `--ground-link USB`. The final command
runs the ELF memory audit. This helper performs only configuration and linking.
