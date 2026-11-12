#include "ScienceCoordinator.hpp"
#include "Platform/CacheStorage.hpp"
namespace Components {
ScienceCoordinator::ScienceCoordinator(const char* name)
    : ScienceCoordinatorComponentBase(name), cache(Artemis::productCacheStorage, Artemis::PRODUCT_CACHE_BYTES), downlink(cache) {}
void ScienceCoordinator::configure(U32 value) { session = value ? value : 1; cache.setSession(session); }
TwinScience::Frame ScienceCoordinator::make(TwinScience::Operation operation) const {
    TwinScience::Frame frame;
    frame.set_operation(operation); frame.set_session(session); frame.set_requestId(request);
    frame.set_generation(generation); frame.set_productId(product); frame.set_totalBytes(bytes);
    frame.set_crc(crc); frame.set_durationSeconds(duration);
    return frame;
}
void ScienceCoordinator::queue(const TwinScience::Frame& frame) {
    if (count == outbox.size()) { fail(100); return; }
    outbox[(head + count) % outbox.size()] = frame; ++count;
}
bool ScienceCoordinator::matching(const TwinScience::Frame& frame) const {
    return frame.get_session() == session && frame.get_requestId() == request;
}
Artemis::Science::ProductKey ScienceCoordinator::key(const TwinScience::Frame& frame) const {
    return {frame.get_session(), frame.get_generation(), frame.get_productId(), frame.get_totalBytes(), frame.get_crc()};
}
void ScienceCoordinator::transition(ScienceJobState next) {
    if (state != next) { state = next; log_ACTIVITY_HI_ScienceProgress(state, product, bytes); }
    publish();
}
void ScienceCoordinator::publish() {
    tlmWrite_JobState(state); tlmWrite_ProductId(product); tlmWrite_ProductBytes(bytes);
    tlmWrite_CacheReceivedBytes(cache.receivedBytes()); tlmWrite_CacheValid(cache.valid());
    tlmWrite_CachedProductId(cache.valid() ? cache.key().productId : 0);
    tlmWrite_CachedProductBytes(cache.valid() ? cache.key().totalBytes : 0);
    tlmWrite_TransferId(downlink.transferId()); tlmWrite_TxPackets(sentPackets);
}
void ScienceCoordinator::fail(U32 reason) {
    if (cache.state() == Artemis::Science::CacheState::RECEIVING) (void)cache.abort(cache.key());
    head = 0; count = 1; outbox[0] = make(TwinScience::Operation::ABORT);
    log_WARNING_HI_ScienceFailed(reason); transition(ScienceJobState::FAILED);
}
void ScienceCoordinator::admissionIn_handler(FwIndexType, bool value) {
    admitted = value;
    if (!value && state == ScienceJobState::SCHEDULED) cancelIn_handler(0);
}
void ScienceCoordinator::payloadStateIn_handler(FwIndexType, const PayloadState& value) { payloadState = value; payloadAge = 0; }
Fw::CmdResponse ScienceCoordinator::scheduleIn_handler(FwIndexType, U32 seconds) {
    if (!admitted || payloadState != PayloadState::READY || payloadAge > 300) return Fw::CmdResponse::EXECUTION_ERROR;
    if (seconds > 86400) return Fw::CmdResponse::VALIDATION_ERROR;
    if (state == ScienceJobState::SCHEDULED || state == ScienceJobState::CAPTURING || state == ScienceJobState::STAGING || downlink.active()) return Fw::CmdResponse::BUSY;
    downlink.stop();  // Retire the previous transfer's delayed repair lease.
    restaging = false;
    ++request; if (!request) ++request;
    due = ticks + seconds * 100; transition(ScienceJobState::SCHEDULED); return Fw::CmdResponse::OK;
}
Fw::CmdResponse ScienceCoordinator::cancelIn_handler(FwIndexType) {
    downlink.stop();
    if (cache.state() == Artemis::Science::CacheState::RECEIVING) (void)cache.abort(cache.key());
    head = 0; count = 0;
    queue(make(TwinScience::Operation::ABORT)); transition(ScienceJobState::CANCELLED); return Fw::CmdResponse::OK;
}
Fw::CmdResponse ScienceCoordinator::downlinkIn_handler(FwIndexType) {
    if (state == ScienceJobState::SCHEDULED || state == ScienceJobState::CAPTURING || state == ScienceJobState::STAGING) return Fw::CmdResponse::BUSY;
    if (downlink.active()) return Fw::CmdResponse::BUSY;
    if (downlink.start() != Artemis::Science::Status::OK) return Fw::CmdResponse::EXECUTION_ERROR;
    product = cache.key().productId; bytes = cache.key().totalBytes; crc = cache.key().crc16;
    consecutiveTxFailures = 0;
    transition(ScienceJobState::DOWNLINKING); return Fw::CmdResponse::OK;
}
void ScienceCoordinator::run_handler(FwIndexType, U32) {
    ++ticks; if (payloadAge < 100000) ++payloadAge;
    if (state == ScienceJobState::SCHEDULED && static_cast<I32>(ticks - due) >= 0) {
        if (!admitted || payloadAge > 300 || payloadState != PayloadState::READY) { fail(101); }
        else { product = 0; bytes = 0; crc = 0; queue(make(TwinScience::Operation::CAPTURE_REQUEST)); deadline = ticks + (duration + 30) * 100; transition(ScienceJobState::CAPTURING); }
    }
    if ((state == ScienceJobState::CAPTURING || state == ScienceJobState::STAGING) && static_cast<I32>(ticks - deadline) >= 0) {
        if (state == ScienceJobState::STAGING) (void)cache.abort(cache.key());
        queue(make(TwinScience::Operation::ABORT)); fail(102);
    }
    if (state == ScienceJobState::DOWNLINKING && !downlink.active()) transition(ScienceJobState::SENT);
    if (count) { const auto frame = outbox[head]; head = (head + 1) % outbox.size(); --count; messageOut_out(0, frame); }
    if (ticks % 100 == 0) publish();
}
void ScienceCoordinator::messageIn_handler(FwIndexType, const TwinScience::Frame& frame) {
    if (!matching(frame)) return;
    using Op = TwinScience::Operation;
    const auto op = frame.get_operation();
    if (op == Op::CAPTURE_REJECTED || op == Op::ERROR) {
        if (state == ScienceJobState::CAPTURING || state == ScienceJobState::STAGING) fail(frame.get_error());
        return;
    }
    if (op == Op::CAPTURE_ACCEPTED) return;
    if (op == Op::PRODUCT_COMMITTED) {
        if (state != ScienceJobState::CAPTURING) return;
        product = frame.get_productId(); bytes = frame.get_totalBytes(); crc = frame.get_crc();
        if (!product || !bytes || bytes > Artemis::PRODUCT_CACHE_BYTES) { fail(103); return; }
        ++generation; if (!generation) ++generation;
        queue(make(Op::STAGE_REQUEST)); deadline = ticks + 1000; transition(ScienceJobState::STAGING); return;
    }
    if (state != ScienceJobState::STAGING && !(op == Op::STAGE_COMMIT && cache.valid())) return;
    if (frame.get_generation() != generation || frame.get_productId() != product) return;
    if (restaging && op == Op::STAGE_BEGIN) {
        if (!frame.get_totalBytes() || frame.get_totalBytes() > Artemis::PRODUCT_CACHE_BYTES) { fail(103); return; }
        bytes = frame.get_totalBytes(); crc = frame.get_crc(); restaging = false;
    }
    if (frame.get_totalBytes() != bytes || frame.get_crc() != crc) return;
    if (op == Op::STAGE_COMMIT && cache.valid() && state != ScienceJobState::STAGING) {
        auto reply = make(Op::CACHE_READY); reply.set_offset(cache.receivedBytes()); queue(reply); return;
    }
    auto result = Artemis::Science::Status::BAD_ARGUMENT;
    if (op == Op::STAGE_BEGIN) result = cache.begin(key(frame));
    else if (op == Op::STAGE_CHUNK) {
        U8 data[200]; for (unsigned i = 0; i < 200; ++i) data[i] = frame.get_data()[i];
        result = cache.append(key(frame), frame.get_offset(), data, frame.get_length());
    } else if (op == Op::STAGE_COMMIT) result = cache.commit(key(frame));
    else return;
    if (result != Artemis::Science::Status::OK) { queue(make(Op::ERROR)); fail(200 + static_cast<U32>(result)); return; }
    auto reply = make(op == Op::STAGE_COMMIT ? Op::CACHE_READY : Op::STAGE_ACK);
    reply.set_offset(cache.receivedBytes()); queue(reply); deadline = ticks + 1000;
    if (op == Op::STAGE_COMMIT) transition(ScienceJobState::CACHE_READY);
}
bool ScienceCoordinator::radioNext_handler(FwIndexType, ScienceBytes& data, U8& length) {
    Artemis::Science::Packet packet;
    if (!downlink.nextPacket(packet)) return false;
    length = static_cast<U8>(packet.length);
    for (unsigned i = 0; i < length; ++i) data[i] = packet.bytes[i];
    return true;
}
void ScienceCoordinator::radioSent_handler(FwIndexType, bool success) {
    downlink.packetSent(success);
    if (success) { ++sentPackets; consecutiveTxFailures = 0; }
    else if (++consecutiveTxFailures >= 4) { downlink.stop(); fail(301); }
}
void ScienceCoordinator::radioRetry_handler(FwIndexType, const ScienceBytes& data, U8 length) {
    if (length > 44) return;
    U8 bytesIn[44]; for (unsigned i = 0; i < length; ++i) bytesIn[i] = data[i];
    if (downlink.acceptRetry(bytesIn, length)) transition(ScienceJobState::DOWNLINKING);
}
void ScienceCoordinator::CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType op, U32 seq, U32 seconds) {
    if (!seconds || seconds > 3600) { cmdResponse_out(op, seq, Fw::CmdResponse::VALIDATION_ERROR); return; }
    if (state == ScienceJobState::SCHEDULED || state == ScienceJobState::CAPTURING || state == ScienceJobState::STAGING) { cmdResponse_out(op, seq, Fw::CmdResponse::BUSY); return; }
    duration = seconds; cmdResponse_out(op, seq, Fw::CmdResponse::OK);
}
void ScienceCoordinator::REQUEST_STATUS_cmdHandler(FwOpcodeType op, U32 seq) { publish(); cmdResponse_out(op, seq, Fw::CmdResponse::OK); }
void ScienceCoordinator::RESTAGE_PRODUCT_cmdHandler(FwOpcodeType op, U32 seq, U32 id) {
    if (!id || payloadAge > 300 || payloadState == PayloadState::UNKNOWN) { cmdResponse_out(op, seq, Fw::CmdResponse::EXECUTION_ERROR); return; }
    if (state == ScienceJobState::SCHEDULED || state == ScienceJobState::CAPTURING || state == ScienceJobState::STAGING || downlink.active()) { cmdResponse_out(op, seq, Fw::CmdResponse::BUSY); return; }
    downlink.stop(); ++request; if (!request) ++request; ++generation; if (!generation) ++generation;
    product = id; bytes = 0; crc = 0; restaging = true;
    queue(make(TwinScience::Operation::STAGE_REQUEST)); deadline = ticks + 1000;
    transition(ScienceJobState::STAGING); cmdResponse_out(op, seq, Fw::CmdResponse::OK);
}
}
