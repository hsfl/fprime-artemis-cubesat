# PayloadManager

The manager forwards collection requests, product descriptors, and readiness
between mission services and the selected payload driver.

Local twin milestone (2026-11-12): the manager accepts `cancelIn: Fw.Signal`
asynchronously and forwards `driverCancelOut: Fw.Signal`. In the Pi deployment,
PayloadLinkService drives this port and the Lepton driver's async input receives
it. Cancellation is handled outside the requesting service's guarded callback,
so the driver's terminal failure/readiness callbacks can safely return through
the manager. The driver owns cancellation completion and outstanding DpWriter
correlation; the manager does not invent a successful cancellation response.

The driver sends `cancelDoneOut` only after cancellation is fully drained. The
manager receives it through synchronous `driverCancelDoneIn` and forwards its
own `cancelDoneOut` to the service's guarded `cancelDoneIn`. This explicit
acknowledgment prevents ordinary product descriptors racing with ABORT from
releasing the old capture and allowing its queued cancel to affect a new one.

See the [Lepton component contract](../PayloadDriver_Lepton/README.md) for the
cancellation cases and focused verification.
