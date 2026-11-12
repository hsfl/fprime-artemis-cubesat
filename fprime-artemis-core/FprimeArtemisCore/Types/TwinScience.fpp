@ Shared request/result and full-cache staging protocol between FC and Pi.
@ No local filesystem path crosses this interface. All numeric fields use FPP
@ serialization; GenericHub supplies the logical-port and outer framing.
module TwinScience {
  enum Operation: U8 {
    CAPTURE_REQUEST = 0
    CAPTURE_ACCEPTED = 1
    CAPTURE_REJECTED = 2
    PRODUCT_COMMITTED = 3
    STAGE_REQUEST = 4
    STAGE_BEGIN = 5
    STAGE_CHUNK = 6
    STAGE_ACK = 7
    STAGE_COMMIT = 8
    CACHE_READY = 9
    ABORT = 10
    ERROR = 11
  }

  constant CHUNK_BYTES = 200
  constant MAX_PRODUCT_BYTES = 196608
  array Data = [CHUNK_BYTES] U8

  @ session changes when the FC link generation changes. requestId identifies
  @ one capture and generation identifies one attempt to populate the cache.
  @ STAGE_ACK.offset is the next byte offset the FC requires.
  struct Frame {
    operation: Operation
    session: U32
    requestId: U32
    productId: U32
    generation: U32
    offset: U32
    totalBytes: U32
    crc: U16
    length: U16
    durationSeconds: U32
    error: U8
    data: Data
  }

  port Message(frame: Frame)
  port Schedule(delaySeconds: U32) -> Fw.CmdResponse
  port Cancel() -> Fw.CmdResponse
  port Downlink() -> Fw.CmdResponse
  port Admission(allowed: bool)
}
