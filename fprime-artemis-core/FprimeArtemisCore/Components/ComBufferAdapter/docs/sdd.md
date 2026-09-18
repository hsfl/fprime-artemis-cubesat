# Components::ComBufferAdapter

The [`Components::ComBufferAdapter`](../ComBufferAdapter.fpp) component connects
`Svc::GenericHub` to the F Prime Com framing pipeline, so that a pair of hubs can
exchange messages over a raw byte stream such as the UART between the Teensy 4.1
flight controller and the Raspberry Pi payload computer.

It carries no protocol logic of its own. It exists solely because the two halves
of that path are typed differently.

## Why this component exists

`Svc::GenericHub` expects its transport to be a *buffer driver*: something that
accepts an `Fw::Buffer` and delivers that buffer, whole, to the peer hub. Every
hub message is self-describing — a message type, a port index, a payload size,
and the payload — and the receiving hub **validates that the declared payload
size matches the size of the buffer it received**. A message that arrives split
or merged is discarded.

A UART provides no message boundaries. Bytes arrive in whatever quantity the
driver happened to read on a given tick; on the Teensy,
`Zephyr::ZephyrUartDriver` emits up to 64 bytes per `schedIn` regardless of where
message boundaries fall. The framework's `Drv::ByteStreamBufferAdapter` does not
help here: it is a pure pass-through that hands the hub whatever the driver
delivered.

So the byte stream has to be framed. F Prime already provides that:
`Svc::FprimeFramer` adds a start word, a length field, and a CRC;
`Svc::FrameAccumulator` reassembles a stream into whole frames; and
`Svc::FprimeDeframer` validates and strips the framing. Reusing them keeps the
protocol in code that is already exercised by the `ComCcsds` subtopology in both
deployments.

The obstacle is purely a type mismatch. `GenericHub`'s buffer-driver interface
uses `Fw.BufferSend`; the framing components use `Svc.ComDataWithContext`, which
pairs an `Fw::Buffer` with a `ComCfg::FrameContext`. `ComBufferAdapter` is the
converter between the two, in both directions.

```text
GenericHub  <-- Fw.BufferSend -->  ComBufferAdapter  <-- Svc.ComDataWithContext -->  framing pipeline
```

## Port catalog

### Hub-facing

These come from `import Drv.PassiveBufferDriver`, which makes this component a
buffer driver from the hub's point of view. All four are single ports, not
arrays; the handlers therefore always operate on port index 0.

| Port | Type | Direction | Purpose |
|---|---|---|---|
| `bufferIn` | `Fw.BufferSend` | sync input | Receives an outgoing hub message from `hub.toBufferDriver` |
| `bufferInReturn` | `Fw.BufferSend` | output | Returns that buffer to `hub.toBufferDriverReturn` once framing is finished with it |
| `bufferOut` | `Fw.BufferSend` | output | Delivers a received hub message to `hub.fromBufferDriver` |
| `bufferOutReturn` | `Fw.BufferSend` | sync input | Receives that buffer back from `hub.fromBufferDriverReturn` |

### Framing-pipeline-facing

| Port | Type | Direction | Purpose |
|---|---|---|---|
| `comDataOut` | `Svc.ComDataWithContext` | output | Passes an outgoing hub message to the framer |
| `comDataReturnIn` | `Svc.ComDataWithContext` | sync input | Receives ownership of that message back from the framer |
| `comDataIn` | `Svc.ComDataWithContext` | sync input | Receives a deframed hub message from the deframer |
| `comDataReturnOut` | `Svc.ComDataWithContext` | output | Returns that message to the deframer |

The component is passive. Every handler is a single port call with no queuing,
no allocation, and no state.

## Connections

Instance names below are those used in `FlightControllerDeployment`.

### Downlink

```text
pcLinkHub.toBufferDriver        -> pcLinkAdapter.bufferIn
pcLinkAdapter.comDataOut        -> pcLinkFramer.dataIn
pcLinkFramer.dataReturnOut      -> pcLinkAdapter.comDataReturnIn
pcLinkAdapter.bufferInReturn    -> pcLinkHub.toBufferDriverReturn
```

### Uplink

```text
pcLinkDeframer.dataOut          -> pcLinkAdapter.comDataIn
pcLinkAdapter.bufferOut         -> pcLinkHub.fromBufferDriver
pcLinkHub.fromBufferDriverReturn -> pcLinkAdapter.bufferOutReturn
pcLinkAdapter.comDataReturnOut  -> pcLinkDeframer.dataReturnIn
```

## Buffer ownership

This is the part that determines whether the chain works or leaks, and it is not
symmetric between the two directions.

### Outgoing

1. The hub allocates a transport buffer from `pcLinkBufferManager`, serializes
   the hub message into it, and emits it on `toBufferDriver`.
2. `bufferIn_handler` forwards that buffer to the framer and **does not release
   it**.
3. `Svc::FprimeFramer` allocates a *separate* frame buffer from its own
   `bufferAllocate` port, copies the payload into it with header and CRC, and
   returns the original buffer on `dataReturnOut`.
4. `comDataReturnIn_handler` forwards the original buffer to `bufferInReturn`,
   giving ownership back to the hub, which deallocates it.
5. The framer's own frame buffer travels on to `ComStub` and is returned along
   the `ComStub.dataReturnOut -> framer.dataReturnIn` path. It never passes
   through this component.

**The framer's return goes to the hub, not to `ComStub`.** `ComStub` returns its
own buffer separately. Wiring these two return paths the other way round
double-frees.

### Incoming

1. `Svc::FrameAccumulator` allocates a buffer to hold a complete detected frame.
2. `Svc::FprimeDeframer` validates the frame and adjusts that buffer's metadata
   to point at the payload — it does not copy or reallocate.
3. `comDataIn_handler` forwards the buffer to `bufferOut`, and **does not
   release it**. The hub, and any buffer consumer downstream of the hub, own it
   until they are finished.
4. When the hub returns it on `fromBufferDriverReturn`,
   `bufferOutReturn_handler` forwards it to `comDataReturnOut`, which walks back
   through the deframer to the accumulator's allocator.

In both directions this component holds no reference to a buffer after its
handler returns, and never allocates or deallocates.

## Frame context

`Svc.ComDataWithContext` carries a `ComCfg::FrameContext` alongside the buffer.
Hub messages are not F Prime packets and carry no APID, so this component sends
a default-constructed context outbound and ignores the context inbound.

`Svc::FprimeDeframer` attempts to read an APID from the first bytes of every
payload it deframes. For a hub message those bytes are the hub's message-type
discriminator, which is not a valid `ComCfg::Apid`. That failure is **not
fatal**: the deframer sets the context APID to `INVALID_UNINITIALIZED` and emits
the payload intact. A hub message shorter than `FwPacketDescriptorType` would
additionally raise a `PayloadTooShort` warning, which cannot occur in practice
because the hub's own header is larger than that.

## Com status

The `Svc.Framer` interface includes `comStatusIn` and `comStatusOut`
(`Fw.SuccessCondition`), used in the `ComCcsds` pipeline for `ComQueue`
backpressure. `GenericHub` has no counterpart, and `Svc::FprimeFramer` does not
gate sends on com status — its `comStatusIn_handler` only forwards the condition
onward. Those ports are therefore left unconnected on the hub chain, and this
component does not implement them.

## Configuration

None. The component has no parameters, no commands, and no configuration phases.
Sizing lives with the components around it: the transport buffer pool and the
frame accumulator ring are configured on `pcLinkBufferManager` and
`pcLinkAccumulator` in the deployment's `instances.fpp`, via the `PcLink`
constants in `FlightControllerDeploymentTopologyDefs.hpp`.

## Requirements

| Name | Description | Validation |
|---|---|---|
| PCLINK-001 | The adapter shall forward buffers received on `bufferIn` to `comDataOut` without modifying their contents. | Unit test |
| PCLINK-002 | The adapter shall forward buffers received on `comDataReturnIn` to `bufferInReturn`, returning ownership to the hub. | Unit test |
| PCLINK-003 | The adapter shall forward buffers received on `comDataIn` to `bufferOut` without modifying their contents. | Unit test |
| PCLINK-004 | The adapter shall forward buffers received on `bufferOutReturn` to `comDataReturnOut`, returning ownership to the receive chain. | Unit test |
| PCLINK-005 | The adapter shall not allocate or deallocate buffers. | Inspection |
| PCLINK-006 | The adapter shall retain no reference to a buffer after the handler that received it returns. | Inspection |

## Known limitations

- **No error reporting.** The component emits no events or telemetry. Transport
  errors are visible only where they are detected: `ComStub` and the UART driver
  for byte-stream errors, the deframer for CRC and framing errors. `GenericHub`
  itself drops driver errors, as noted in its own SDD.
- **Single link.** The ports are not arrays, so one instance serves exactly one
  hub and one framing pipeline. A second inter-computer link needs a second
  instance.
- **No flow control.** The component adds no queuing. If the framer or the UART
  driver cannot keep up, messages are dropped upstream rather than back-pressured.

## Related

- [`Svc::GenericHub` SDD](../../../../lib/fprime/Svc/GenericHub/docs/sdd.md)
- [Hub pattern](../../../../lib/fprime/docs/user-manual/design-patterns/hub-pattern.md)
- [`docs/HUB_UART_DEPLOYMENT_LINK_PLAN_2026-09-16.md`](../../../../../docs/HUB_UART_DEPLOYMENT_LINK_PLAN_2026-09-16.md) — why this transport was chosen
- [`docs/N2_DUAL_DEPLOYMENT_REFACTOR_PLAN_2026-09-17.md`](../../../../../docs/N2_DUAL_DEPLOYMENT_REFACTOR_PLAN_2026-09-17.md) — the two-computer architecture this link serves

## Change log

| Date | Description |
|---|---|
| 2026-09-17 | Initial version |
