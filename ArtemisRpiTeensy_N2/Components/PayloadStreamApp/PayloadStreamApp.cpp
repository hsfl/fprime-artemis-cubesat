#include "Components/PayloadStreamApp/PayloadStreamApp.hpp"

#include <cstring>

namespace Components {

PayloadStreamApp::PayloadStreamApp(const char* const compName)
    : PayloadStreamAppComponentBase(compName),
      m_state(STOPPED),
      m_streaming(false),
      m_waitingResponse(false),
      m_responseDeadlineTicks(0U),
      m_requestId(0U),
      m_sessionId(0U),
      m_frameSequence(0U),
      m_framesUploaded(0U),
      m_framesDropped(0U),
      m_lastResponseStatus(0U),
      m_pendingOperation(OP_BEGIN),
      m_expectedNextOffset(0U),
      m_previewData(nullptr),
      m_previewCrc(0U),
      m_request{},
      m_responseMailbox{},
      m_haveResponseMailbox(false),
      m_responseMailboxMutex() {}

PayloadStreamApp::~PayloadStreamApp() {}

void PayloadStreamApp::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadStreamApp::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    if (!this->m_waitingResponse || this->m_responseDeadlineTicks == 0U) {
        return;
    }
    this->m_responseDeadlineTicks -= 1U;
    if (this->m_responseDeadlineTicks == 0U) {
        // The old local RPC is abandoned. Never resend it: drop this source
        // frame and request a fresh preview with a new session instead.
        this->dropPreview(6U, RESPONSE_TIMEOUT_TICKS);
    }
}

void PayloadStreamApp::previewIn_handler(FwIndexType portNum, Fw::Buffer& preview) {
    static_cast<void>(portNum);
    if (!this->m_streaming || this->m_waitingResponse || !preview.isValid() ||
        preview.getSize() != PREVIEW_BYTES) {
        return;
    }

    // PayloadDriver_Lepton owns the single fixed preview buffer and does not
    // overwrite it until the next request. This queued input runs on this
    // component's active task, so stream state is serialized with commands
    // and local-RPC responses.
    this->m_previewData = preview.getData();
    this->m_sessionId = static_cast<U16>(this->m_sessionId + 1U);
    if (this->m_sessionId == 0U) {
        this->m_sessionId = 1U;
    }
    this->m_frameSequence += 1U;
    this->m_previewCrc = crc16Ccitt(this->m_previewData, PREVIEW_BYTES);
    this->m_expectedNextOffset = 0U;
    this->m_state = UPLOADING;
    if (!this->sendBegin()) {
        this->dropPreview(1U, 0U);
    }
}

void PayloadStreamApp::previewResponseIn_handler(FwIndexType portNum, Fw::Buffer& response) {
    static_cast<void>(portNum);
    if (!response.isValid() || response.getSize() != sizeof(this->m_responseMailbox)) {
        return;
    }
    {
        Os::ScopeLock lock(this->m_responseMailboxMutex);
        std::memcpy(this->m_responseMailbox, response.getData(), sizeof(this->m_responseMailbox));
        this->m_haveResponseMailbox = true;
    }
    this->responseAdvanceOut_out(0);
}

void PayloadStreamApp::responseAdvanceIn_handler(FwIndexType portNum) {
    static_cast<void>(portNum);
    U8 response[sizeof(this->m_responseMailbox)] = {};
    {
        Os::ScopeLock lock(this->m_responseMailboxMutex);
        if (!this->m_haveResponseMailbox) {
            return;
        }
        std::memcpy(response, this->m_responseMailbox, sizeof(response));
        this->m_haveResponseMailbox = false;
    }
    this->processPreviewResponse(response);
}

void PayloadStreamApp::processPreviewResponse(const U8* const data) {
    if (!this->m_waitingResponse) {
        return;
    }
    const U16 responseSession = getU16(data, 5U);
    if (data[0] != PREVIEW_TARGET || data[1] != this->m_requestId || data[3] != 8U ||
        data[4] != this->m_pendingOperation || responseSession != this->m_sessionId) {
        return;
    }

    this->m_waitingResponse = false;
    this->m_responseDeadlineTicks = 0U;
    this->m_lastResponseStatus = data[2];
    if (data[2] != RPC_STATUS_OK) {
        this->dropPreview(2U, data[2]);
        return;
    }

    const U32 receivedBytes = getU32(data, 8U);
    if ((this->m_pendingOperation == OP_BEGIN) || (this->m_pendingOperation == OP_CHUNK)) {
        if (receivedBytes != this->m_expectedNextOffset || receivedBytes > PREVIEW_BYTES) {
            this->dropPreview(3U, receivedBytes);
            return;
        }
        if (receivedBytes == PREVIEW_BYTES) {
            if (!this->sendCommit()) {
                this->dropPreview(4U, receivedBytes);
            }
        } else if (!this->sendChunk(receivedBytes)) {
            this->dropPreview(5U, receivedBytes);
        }
        return;
    }

    if (this->m_pendingOperation == OP_COMMIT) {
        this->finishPreview();
    }
}

void PayloadStreamApp::START_STREAM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    bool shouldRequest = false;
    if (!this->m_streaming) {
        this->m_streaming = true;
        this->m_state = WAITING_FOR_SOURCE;
        shouldRequest = true;
        this->log_ACTIVITY_HI_PreviewStreamStarted();
    }
    this->writeTelemetry();
    if (shouldRequest) {
        this->requestNextPreview();
    }
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadStreamApp::STOP_STREAM_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    const bool wasStreaming = this->m_streaming;
    this->m_streaming = false;
    if (this->m_waitingResponse && this->m_previewData != nullptr) {
        this->sendAbortBestEffort();
    }
    this->m_waitingResponse = false;
    this->m_responseDeadlineTicks = 0U;
    this->m_previewData = nullptr;
    this->m_state = STOPPED;
    if (wasStreaming) {
        this->log_ACTIVITY_HI_PreviewStreamStopped();
    }
    this->writeTelemetry();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadStreamApp::GET_STREAM_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->writeTelemetry();
    this->log_ACTIVITY_LO_PreviewStatus(
        this->m_state, this->m_frameSequence, this->m_framesUploaded, this->m_framesDropped);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadStreamApp::requestNextPreview() {
    if (!this->m_streaming || !this->isConnected_previewRequestOut_OutputPort(0)) {
        return;
    }
    this->m_state = WAITING_FOR_SOURCE;
    this->m_responseDeadlineTicks = 0U;
    this->previewRequestOut_out(0);
}

bool PayloadStreamApp::sendBegin() {
    // body: op, session, frame sequence, dimensions, pixel format, byte count, CRC16
    this->m_request[0] = PREVIEW_TARGET;
    this->m_request[3] = 0U;
    this->m_request[4] = OP_BEGIN;
    putU16(this->m_request, 5U, this->m_sessionId);
    putU32(this->m_request, 7U, this->m_frameSequence);
    this->m_request[11] = 80U;
    this->m_request[12] = 60U;
    this->m_request[13] = 1U;  // U8 grayscale
    putU16(this->m_request, 14U, static_cast<U16>(PREVIEW_BYTES));
    putU16(this->m_request, 16U, this->m_previewCrc);
    return this->sendRequest(18U, OP_BEGIN);
}

bool PayloadStreamApp::sendChunk(FwSizeType offset) {
    if (this->m_previewData == nullptr || offset >= PREVIEW_BYTES) {
        return false;
    }
    FwSizeType chunkBytes = PREVIEW_BYTES - offset;
    if (chunkBytes > MAX_CHUNK_BYTES) {
        chunkBytes = MAX_CHUNK_BYTES;
    }
    this->m_request[0] = PREVIEW_TARGET;
    this->m_request[3] = 0U;
    this->m_request[4] = OP_CHUNK;
    putU16(this->m_request, 5U, this->m_sessionId);
    putU16(this->m_request, 7U, static_cast<U16>(offset));
    this->m_request[9] = static_cast<U8>(chunkBytes);
    std::memcpy(&this->m_request[10], &this->m_previewData[offset], static_cast<size_t>(chunkBytes));
    this->m_expectedNextOffset = offset + chunkBytes;
    return this->sendRequest(10U + chunkBytes, OP_CHUNK);
}

bool PayloadStreamApp::sendCommit() {
    this->m_request[0] = PREVIEW_TARGET;
    this->m_request[3] = 0U;
    this->m_request[4] = OP_COMMIT;
    putU16(this->m_request, 5U, this->m_sessionId);
    return this->sendRequest(7U, OP_COMMIT);
}

void PayloadStreamApp::sendAbortBestEffort() {
    this->m_request[0] = PREVIEW_TARGET;
    this->m_request[3] = 0U;
    this->m_request[4] = OP_ABORT;
    putU16(this->m_request, 5U, this->m_sessionId);
    (void)this->sendRequest(7U, OP_ABORT);
}

bool PayloadStreamApp::sendRequest(FwSizeType size, PreviewOperation operation) {
    if (!this->isConnected_previewPacketOut_OutputPort(0) || size < 5U || size > sizeof(this->m_request)) {
        return false;
    }
    this->m_requestId = static_cast<U8>(this->m_requestId + 1U);
    if (this->m_requestId == 0U) {
        this->m_requestId = 1U;
    }
    this->m_request[1] = this->m_requestId;
    this->m_request[2] = static_cast<U8>(size - 4U);
    Fw::Buffer request(this->m_request, size);
    this->m_pendingOperation = operation;
    this->m_waitingResponse = true;
    const Components::PayloadSendStatus result = this->previewPacketOut_out(0, request);
    if (result == Components::PayloadSendStatus::LOCAL_ACCEPTED) {
        this->m_responseDeadlineTicks = RESPONSE_TIMEOUT_TICKS;
        return true;
    }
    this->m_waitingResponse = false;
    this->m_responseDeadlineTicks = 0U;
    this->m_lastResponseStatus = static_cast<U32>(result);
    return false;
}

void PayloadStreamApp::dropPreview(U32 reason, U32 detail) {
    const bool requestNext = this->m_streaming;
    this->m_waitingResponse = false;
    this->m_responseDeadlineTicks = 0U;
    this->m_previewData = nullptr;
    this->m_framesDropped += 1U;
    this->m_state = requestNext ? WAITING_FOR_SOURCE : STOPPED;
    this->log_WARNING_LO_PreviewDropped(reason, detail);
    this->writeTelemetry();
    if (requestNext) {
        // A drop abandons this frame; it does not retry or repair it. Advance
        // to a newly captured latest preview so streaming cannot dead-end.
        this->requestNextPreview();
    }
}

void PayloadStreamApp::finishPreview() {
    this->m_previewData = nullptr;
    this->m_responseDeadlineTicks = 0U;
    this->m_framesUploaded += 1U;
    this->log_ACTIVITY_LO_PreviewUploaded(this->m_frameSequence, this->m_sessionId);
    this->writeTelemetry();
    if (this->m_streaming) {
        // This runs on the active task immediately after the copied response,
        // not on a rate group.
        this->requestNextPreview();
    } else {
        this->m_state = STOPPED;
    }
}

void PayloadStreamApp::writeTelemetry() {
    this->tlmWrite_StreamState(this->m_state);
    this->tlmWrite_FrameSequence(this->m_frameSequence);
    this->tlmWrite_SessionId(this->m_sessionId);
    this->tlmWrite_FramesUploaded(this->m_framesUploaded);
    this->tlmWrite_FramesDropped(this->m_framesDropped);
    this->tlmWrite_LastResponseStatus(this->m_lastResponseStatus);
}

void PayloadStreamApp::putU16(U8* data, FwSizeType offset, U16 value) {
    data[offset] = static_cast<U8>(value & 0xFFU);
    data[offset + 1U] = static_cast<U8>((value >> 8U) & 0xFFU);
}

void PayloadStreamApp::putU32(U8* data, FwSizeType offset, U32 value) {
    data[offset] = static_cast<U8>(value & 0xFFU);
    data[offset + 1U] = static_cast<U8>((value >> 8U) & 0xFFU);
    data[offset + 2U] = static_cast<U8>((value >> 16U) & 0xFFU);
    data[offset + 3U] = static_cast<U8>((value >> 24U) & 0xFFU);
}

U16 PayloadStreamApp::getU16(const U8* data, FwSizeType offset) {
    return static_cast<U16>(data[offset]) | (static_cast<U16>(data[offset + 1U]) << 8U);
}

U32 PayloadStreamApp::getU32(const U8* data, FwSizeType offset) {
    return static_cast<U32>(data[offset]) | (static_cast<U32>(data[offset + 1U]) << 8U) |
           (static_cast<U32>(data[offset + 2U]) << 16U) | (static_cast<U32>(data[offset + 3U]) << 24U);
}

U16 PayloadStreamApp::crc16Ccitt(const U8* data, FwSizeType size) {
    U16 crc = 0xFFFFU;
    for (FwSizeType i = 0U; i < size; ++i) {
        crc ^= static_cast<U16>(data[i]) << 8U;
        for (U8 bit = 0U; bit < 8U; ++bit) {
            crc = ((crc & 0x8000U) != 0U) ? static_cast<U16>((crc << 1U) ^ 0x1021U)
                                           : static_cast<U16>(crc << 1U);
        }
    }
    return crc;
}

}  // namespace Components
