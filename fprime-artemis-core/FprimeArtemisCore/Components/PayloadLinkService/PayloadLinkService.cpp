#include "PayloadLinkService.hpp"
#include "FprimeArtemisCore/Types/FppConstantsAc.hpp"

#include <algorithm>
#include <cstdlib>

namespace Components {

PayloadLinkService::PayloadLinkService(const char* name) : PayloadLinkServiceComponentBase(name) {
    const char* index = std::getenv("ARTEMIS_PAYLOAD_CATALOG");
    m_catalog = index && *index ? index : "payload-link.catalog";
    const char* root = std::getenv("ARTEMIS_PAYLOAD_PRODUCT_DIR");
    m_productRoot = root && *root ? root : "./DpCat";
    ArtemisPayload::Product product;
    if (ArtemisPayload::ProductCatalog(m_catalog, m_productRoot).latest(product)) {
        m_product = product.id;
        m_bytes = product.size;
        m_crc = product.crc;
        m_path = product.path;
        m_productValid = true;
    }
}
PayloadLinkService::~PayloadLinkService() { stopStage(); }

bool PayloadLinkService::enqueue(const TwinScience::Frame& frame) {
    if (m_outCount == QUEUE_DEPTH) {
        m_queueOverflow = true;
        return false;
    }
    m_outgoing[(m_outHead + m_outCount) % QUEUE_DEPTH] = frame;
    ++m_outCount;
    return true;
}

void PayloadLinkService::messageIn_handler(FwIndexType, const TwinScience::Frame& frame) {
    if (m_inCount == QUEUE_DEPTH) {
        m_queueOverflow = true;
        return;
    }
    m_incoming[(m_inHead + m_inCount) % QUEUE_DEPTH] = frame;
    ++m_inCount;
}

void PayloadLinkService::stateIn_handler(FwIndexType, const Components::PayloadState& state) {
    m_state = state;
    m_stateAgeTicks = 0;
}

void PayloadLinkService::cancelDoneIn_handler(FwIndexType) {
    if (m_capturing && m_cancelled && m_cancelDispatched) m_cancelDonePending = true;
}

void PayloadLinkService::productIn_handler(FwIndexType, U32 productId, U32 productBytes,
                                          const Components::ScienceProductSource&,
                                          const Fw::StringBase& sourcePath, U32 sourceCrc) {
    // Engineering captures with no outstanding FC request cannot acquire the
    // ownership of a later FC request. A request is active until this callback.
    // Only the dedicated cancelDoneIn barrier can release cancellation. An
    // old success OR failure callback may already be waiting on this guard.
    if (!m_capturing || m_cancelled) return;
    m_product = productId;
    m_bytes = sourceCrc > 0xFFFFU ? 0 : productBytes;
    m_crc = static_cast<U16>(sourceCrc);
    m_path = sourcePath.toChar();
    m_productPending = true;
}

TwinScience::Frame PayloadLinkService::reply(TwinScience::Operation operation, U8 error) const {
    TwinScience::Frame frame;
    frame.set_operation(operation);
    frame.set_session(m_session);
    frame.set_requestId(m_request);
    frame.set_productId(m_product);
    frame.set_generation(m_generation);
    frame.set_totalBytes(m_bytes);
    frame.set_crc(m_crc);
    frame.set_durationSeconds(m_duration);
    frame.set_error(error);
    return frame;
}

bool PayloadLinkService::sameRequest(const TwinScience::Frame& frame) const {
    return m_haveRequest && frame.get_session() == m_session && frame.get_requestId() == m_request;
}

bool PayloadLinkService::sameStage(const TwinScience::Frame& frame) const {
    return sameRequest(frame) && frame.get_productId() == m_product && frame.get_generation() == m_generation;
}

void PayloadLinkService::stopStage() {
    m_file.close();
    m_stage = IDLE;
    m_waitTicks = 0;
    m_retries = 0;
}

void PayloadLinkService::fail(U8 error) {
    if (m_stage != IDLE) {
        m_stageOutcome = reply(TwinScience::Operation::ERROR, error);
        m_haveStageOutcome = true;
    }
    stopStage();
    ++m_errors;
    log_WARNING_HI_ScienceServiceError(error, m_request);
    enqueue(reply(TwinScience::Operation::ERROR, error));
}

void PayloadLinkService::startStage(const TwinScience::Frame& frame) {
    const auto reject = [this, &frame](U8 error) {
        TwinScience::Frame rejected = frame;
        rejected.set_operation(TwinScience::Operation::ERROR);
        rejected.set_error(error);
        enqueue(rejected);
    };
    if (m_capturing || !frame.get_productId() || frame.get_session() == 0 ||
        frame.get_requestId() == 0 || frame.get_generation() == 0) {
        reject(m_capturing ? BUSY : BAD_REQUEST);
        return;
    }
    if (m_haveRequest && frame.get_session() == m_session &&
        (frame.get_requestId() < m_request ||
         (frame.get_requestId() == m_request && frame.get_generation() < m_generation))) {
        reject(STALE);
        return;
    }
    if (m_haveStageOutcome && frame.get_session() == m_stageOutcome.get_session() &&
        frame.get_requestId() == m_stageOutcome.get_requestId() &&
        frame.get_productId() == m_stageOutcome.get_productId() &&
        frame.get_generation() == m_stageOutcome.get_generation()) {
        enqueue(m_stageOutcome);  // A lost final reply cannot restart a completed stage.
        return;
    }
    if (m_stage != IDLE) {
        if (sameStage(frame)) enqueue(m_lastSent);  // Lost request or response: do not reopen/restart the file.
        else {
            reject(BUSY);
        }
        return;
    }
    // Look up any committed ID, even after an accepted newer capture failed.
    // Neither the remote request nor a directory scan supplies a file path.
    ArtemisPayload::Product product;
    if (!ArtemisPayload::ProductCatalog(m_catalog, m_productRoot).lookup(frame.get_productId(), product)) {
        reject(FILE_IO);
        return;
    }
    m_product = product.id; m_bytes = product.size; m_crc = product.crc; m_path = product.path;
    m_productValid = true;
    // A rebooted FC can explicitly restage with a fresh link session/request.
    m_session = frame.get_session();
    m_request = frame.get_requestId();
    m_generation = frame.get_generation();
    m_haveRequest = true;
    if (!m_file.open(product)) { fail(FILE_IO); return; }
    m_offset = 0;
    m_expectedOffset = 0;
    m_stage = SEND_BEGIN;
}

void PayloadLinkService::process(const TwinScience::Frame& frame) {
    switch (frame.get_operation()) {
        case TwinScience::Operation::CAPTURE_REQUEST: {
            if (sameRequest(frame)) {
                TwinScience::Frame response = reply(TwinScience::Operation::CAPTURE_ACCEPTED);
                if (frame.get_durationSeconds() != m_duration || m_captureError != NONE) {
                    response.set_operation(TwinScience::Operation::CAPTURE_REJECTED);
                    response.set_error(m_captureError != NONE ? m_captureError : BAD_REQUEST);
                }
                enqueue(response);
                if (m_productValid && frame.get_durationSeconds() == m_duration)
                    enqueue(reply(TwinScience::Operation::PRODUCT_COMMITTED));
                return;
            }
            U8 error = NONE;
            if (frame.get_session() == 0 || frame.get_requestId() == 0 ||
                frame.get_durationSeconds() == 0 || frame.get_durationSeconds() > 3600)
                error = BAD_REQUEST;
            else if (m_capturing || m_stage != IDLE) error = BUSY;
            else if (m_haveRequest && frame.get_session() == m_session && frame.get_requestId() < m_request)
                error = STALE;
            else if (m_state != Components::PayloadState::READY || m_stateAgeTicks > STATE_MAX_AGE_TICKS) error = NOT_READY;
            if (error != NONE) {
                TwinScience::Frame rejected = frame;
                rejected.set_operation(TwinScience::Operation::CAPTURE_REJECTED);
                rejected.set_error(error);
                enqueue(rejected);
                return;
            }
            m_session = frame.get_session();
            m_request = frame.get_requestId();
            m_generation = frame.get_generation();
            m_duration = frame.get_durationSeconds();
            m_haveRequest = true;
            m_capturing = true;
            m_cancelled = false;
            m_cancelDispatched = false;
            m_cancelDonePending = false;
            m_captureError = NONE;
            m_capturePending = true;
            m_productPending = false;
            m_productValid = false;
            m_product = 0;
            m_bytes = 0;
            ++m_requests;
            enqueue(reply(TwinScience::Operation::CAPTURE_ACCEPTED));
            break;
        }
        case TwinScience::Operation::STAGE_REQUEST: startStage(frame); break;
        case TwinScience::Operation::STAGE_ACK:
            if (!sameStage(frame)) return;  // A stale peer/generation cannot advance this file.
            if (m_stage != WAIT_BEGIN && m_stage != WAIT_CHUNK) return;
            if (frame.get_offset() < m_expectedOffset) return;  // Duplicate old ACK.
            if (frame.get_offset() != m_expectedOffset || frame.get_error() != NONE) { fail(BAD_ACK); return; }
            m_offset = m_expectedOffset;
            m_waitTicks = 0;
            m_retries = 0;
            m_stage = m_offset == m_bytes ? SEND_COMMIT : SEND_CHUNK;
            break;
        case TwinScience::Operation::CACHE_READY:
            if (sameStage(frame) && m_stage == WAIT_COMMIT && frame.get_error() == NONE) {
                m_stageOutcome = reply(TwinScience::Operation::CACHE_READY);
                m_stageOutcome.set_offset(m_bytes);
                m_haveStageOutcome = true;
                stopStage();
                log_ACTIVITY_HI_ScienceCacheReady(m_product, m_generation);
            }
            break;
        case TwinScience::Operation::ABORT:
            if (sameRequest(frame)) {
                if (m_stage != IDLE) {
                    m_stageOutcome = reply(TwinScience::Operation::ERROR, ABORTED);
                    m_haveStageOutcome = true;
                }
                stopStage();
                m_cancelPending = m_capturing && !m_cancelled;
                if (m_cancelPending) m_productPending = false;
                m_cancelled = m_capturing;
                m_captureError = ABORTED;
                m_capturePending = false;
                enqueue(reply(TwinScience::Operation::ERROR, ABORTED));
            }
            break;
        case TwinScience::Operation::ERROR:
            if (sameStage(frame)) stopStage();
            break;
        default: break;
    }
}

void PayloadLinkService::advanceStage() {
    if (m_stage == IDLE) return;
    if (m_stage == WAIT_BEGIN || m_stage == WAIT_CHUNK || m_stage == WAIT_COMMIT) {
        if (++m_waitTicks < RETRY_TICKS) return;
        m_waitTicks = 0;
        if (m_retries++ == MAX_RETRIES) { fail(TIMEOUT); return; }
        enqueue(m_lastSent);
        return;
    }
    TwinScience::Frame frame = reply(TwinScience::Operation::STAGE_BEGIN);
    if (m_stage == SEND_BEGIN) {
        m_expectedOffset = 0;
        m_stage = WAIT_BEGIN;
    } else if (m_stage == SEND_CHUNK) {
        const U32 count = std::min<U32>(TwinScience::CHUNK_BYTES, m_bytes - m_offset);
        unsigned char bytes[TwinScience::CHUNK_BYTES];
        if (!m_file.read(m_offset, static_cast<U16>(count), bytes)) { fail(FILE_IO); return; }
        TwinScience::Data data;
        for (U32 i = 0; i < count; ++i) data[i] = static_cast<U8>(bytes[i]);
        frame.set_operation(TwinScience::Operation::STAGE_CHUNK);
        frame.set_offset(m_offset);
        frame.set_length(static_cast<U16>(count));
        frame.set_data(data);
        m_expectedOffset = m_offset + count;
        m_stage = WAIT_CHUNK;
    } else {
        frame.set_operation(TwinScience::Operation::STAGE_COMMIT);
        frame.set_offset(m_bytes);
        m_stage = WAIT_COMMIT;
    }
    m_lastSent = frame;
    m_waitTicks = 0;
    m_retries = 0;
    enqueue(frame);
}

void PayloadLinkService::run_handler(FwIndexType, U32) {
    if (m_stateAgeTicks <= STATE_MAX_AGE_TICKS) ++m_stateAgeTicks;
    if (m_queueOverflow) {
        m_queueOverflow = false;
        m_inCount = 0;
        m_outCount = 0;
        m_capturePending = false;
        m_cancelPending = m_capturing && !m_cancelled;
        if (m_cancelPending) m_productPending = false;
        m_cancelled = m_capturing;
        m_captureError = QUEUE_FULL;
        fail(QUEUE_FULL);
    }
    if (m_cancelDonePending) {
        m_cancelDonePending = false;
        m_cancelDispatched = false;
        m_capturing = false;
        m_productPending = false;
        m_productValid = false;
    }
    // One inbound message per tick bounds work, and all outgoing serial frames
    // originate here. Other hub producers use the shared transport mutex.
    if (m_inCount != 0) {
        const TwinScience::Frame frame = m_incoming[m_inHead];
        m_inHead = (m_inHead + 1) % QUEUE_DEPTH;
        --m_inCount;
        process(frame);
    }
    if (m_capturePending) {
        m_capturePending = false;
        captureOut_out(0, m_duration);
    }
    if (m_cancelPending) {
        m_cancelPending = false;
        m_cancelDispatched = true;
        if (isConnected_cancelOut_OutputPort(0)) cancelOut_out(0);
    }
    if (m_productPending) {
        m_productPending = false;
        m_capturing = false;
        m_productValid = !m_cancelled && m_product != 0 && m_bytes > 0 &&
                         m_bytes <= TwinScience::MAX_PRODUCT_BYTES && !m_path.empty();
        if (!m_productValid) {
            m_captureError = m_cancelled ? ABORTED : CAPTURE_FAILED;
            fail(m_captureError);
        }
        else {
            ArtemisPayload::Product product;
            product.id = m_product; product.size = m_bytes; product.crc = m_crc; product.path = m_path;
            if (!ArtemisPayload::ProductCatalog(m_catalog, m_productRoot).remember(product)) {
                m_productValid = false;
                m_captureError = FILE_IO;
                fail(FILE_IO);
            } else {
                m_product = product.id;
                m_path = product.path;
                ++m_committed;
                enqueue(reply(TwinScience::Operation::PRODUCT_COMMITTED));
                log_ACTIVITY_HI_ScienceProductCommitted(m_product, m_bytes);
            }
        }
    }
    advanceStage();
    if (m_outCount != 0) {
        const TwinScience::Frame frame = m_outgoing[m_outHead];
        m_outHead = (m_outHead + 1) % QUEUE_DEPTH;
        --m_outCount;
        messageOut_out(0, frame);
    }
    // These are update-on-change channels, so idle 100 Hz ticks are cheap.
    tlmWrite_CaptureRequests(m_requests);
    tlmWrite_CommittedProducts(m_committed);
    tlmWrite_StagedBytes(m_offset);
    tlmWrite_ProtocolErrors(m_errors);
}
}
