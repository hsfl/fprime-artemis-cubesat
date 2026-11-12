# Flight-controller science coordination

This passive component owns the split-platform science workflow. Every input,
command, and radio callback is guarded by the same F′ component mutex; the
allocation-free cache/downlink engines are never concurrently accessed.

MissionApp owns mission modes and forwards scheduling, cancellation, and
downlink requests. Capture admission requires BASE plus a fresh READY payload
report. Cached downlink remains available after Pi loss or power-off.

The 100 Hz run sends queued hub messages from one thread, tracks bounded
capture/staging deadlines, and publishes workflow progress. Pi paths never
cross this contract. A matched PRODUCT_COMMITTED causes an explicit stage
request. The full cache must commit with the exact length and CRC before RF
transmission can start. The radio worker requests at most one small packet at a
time; repair reads the same cached bytes, without accessing Pi.

`CONFIGURE_CAPTURE_DURATION` accepts 1–3600 seconds. A scheduled delay is
bounded to 86400 seconds. Capture timeout includes the requested acquisition
interval plus 30 seconds for frame selection and product writing. Staging has a
10-second progress timeout. Cancellation aborts incomplete staging/transmission;
valid committed bytes remain available unless explicitly replaced/reset.

Progress is reported as IDLE, SCHEDULED, CAPTURING, STAGING, CACHE_READY,
DOWNLINKING, SENT, FAILED, or CANCELLED. SENT means local transmission finished;
independent ground reconstruction and verified Yamcs storage establish receipt.

Integration testing is tracked in docs/development/ARTEMIS_LOCAL_TWIN.md.
