# Channel-1 science integration

Milestone M3 radio extension, implemented 2026-11-11 (HST).

The existing radio worker remains the only hardware/RadioHead owner. It serves
pending channel-0 traffic, power requests, and health before at most four science
packets per pass, checking for urgent work between packets. It then sleeps or
wakes through the existing bounded driver wait. The coordinator supplies one
application packet at a time and retains the only complete product cache.

`RadioComms` exports three additional manager output ports:

- `scienceNext`: `RadioScienceNext(ref ScienceBytes[44], ref U8 length) -> bool`.
  False means no packet. A true result must have length 1–44.
- `scienceSent`: `RadioScienceSent(bool success)`, reporting local TX completion.
  This never signals CCSDS `ComQueue` readiness or ground science receipt.
- `scienceRetry`: `RadioScienceRetry(ScienceBytes[44], U8 length)`, carrying
  reassembled channel-1 uplink data to the coordinator's retry parser.

The coordinator must protect all cache/downlink callbacks and staging calls
with one common mutex, or serialize them through one owner. The manager does not
hold its own control mutex while calling science ports.

`LinkCore` now keeps two independent RX buffers/reassembly histories and two TX
message-ID counters. `transmit()` preserves the prior channel-0 API;
`transmitChannel()` selects generated channel-specific magic and ACK policy.
Channel 0 remains A5; science uses A6. Neither channel can acknowledge, complete,
or expire the other's transfer. Channel 2 remains local and cannot cross RF.
The existing 49-byte RF packet / 5-byte segment header / 44-byte segment payload
contract remains unchanged. A ≤44-byte C3M science packet needs one RF segment.

## Validation

From this component directory (macOS first; equivalent shell under WSL2):

```sh
cmake -S test -B /tmp/artemis-radio-science-build
cmake --build /tmp/artemis-radio-science-build -j4
ctest --test-dir /tmp/artemis-radio-science-build --output-on-failure
```

`radio_link_contract` retains the existing channel-0 byte/ACK/retry/timeout
checks. `radio_channel_contract` covers A6 wire bytes, interleaved A5/A6 frames
with equal message IDs, separate duplicate/expiry state, cross-channel ACK
rejection, generated ACK policy, channel bounds, and independent ID rollover.
Full FPP generation, Zephyr builds, worker scheduling, and twin throughput are
integration checks owned by the enclosing deployment; these host tests do not
establish RF/SPI HIL or worst-case latency.

Observed result: **2/2 tests pass** with AppleClang 21, and the same **2/2 pass
with ASan + UBSan**. No hardware operation or source edits outside the assigned
radio/cache integration were performed.
