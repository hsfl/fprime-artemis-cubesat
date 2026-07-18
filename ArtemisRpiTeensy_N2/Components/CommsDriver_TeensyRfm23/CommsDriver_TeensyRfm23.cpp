#include "Components/CommsDriver_TeensyRfm23/CommsDriver_TeensyRfm23.hpp"

#include <cstring>

namespace Components {

CommsDriver_TeensyRfm23::CommsDriver_TeensyRfm23(const char* const compName)
    : CommsDriver_TeensyRfm23ComponentBase(compName),
      m_sequence(0),
      m_pendingRequestId(0),
      m_pendingOperation(Components::RadioOperation::STATUS),
      m_pendingEnabled(0),
      m_pendingRequestTicks(0),
      m_requestPending(false),
      m_requestCount(0),
      m_rpcFailureCount(0),
      m_rejectedResponseCount(0),
      m_radioState(Components::RadioState::OFF),
      m_radioFault(Components::RadioFault::NONE),
      m_bootFlags(0),
      m_rssiValid(0),
      m_rssiDbm(0),
      m_rssiAgeMs(LinkCfg::TEENSY_RF_RSSI_AGE_UNKNOWN_MS),
      m_initAttempts(0),
      m_rfRxPackets(0),
      m_rfTxPackets(0),
      m_rfTxDrops(0) {
    std::memset(this->m_localPacket, 0, sizeof(this->m_localPacket));
}

CommsDriver_TeensyRfm23::~CommsDriver_TeensyRfm23() {}

void CommsDriver_TeensyRfm23::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void CommsDriver_TeensyRfm23::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    if (!this->m_requestPending) {
        return;
    }
    this->m_pendingRequestTicks += 1U;
    this->tlmWrite_PendingRequestTicks(this->m_pendingRequestTicks);
    if (this->m_pendingRequestTicks >= REQUEST_TIMEOUT_TICKS) {
        this->timeoutPendingRequest();
    }
}

void CommsDriver_TeensyRfm23::requestIn_handler(FwIndexType portNum,
                                                const Components::RadioOperation& operation,
                                                U8 enabled) {
    static_cast<void>(portNum);
    this->m_requestCount += 1U;

    if ((operation != Components::RadioOperation::STATUS &&
         operation != Components::RadioOperation::SET_ENABLED) ||
        (operation == Components::RadioOperation::SET_ENABLED && enabled > 1U)) {
        this->m_rpcFailureCount += 1U;
        this->emitStatus(operation, Components::RadioRpcResult::BAD_REQUEST);
        this->log_WARNING_LO_RadioRequestFailed(operation, Components::RadioRpcResult::BAD_REQUEST);
        return;
    }

    if (this->m_requestPending) {
        this->m_rpcFailureCount += 1U;
        this->emitStatus(operation, Components::RadioRpcResult::BUSY);
        this->log_WARNING_LO_RadioRequestFailed(operation, Components::RadioRpcResult::BUSY);
        return;
    }

    if (!this->sendRequest(operation, enabled)) {
        this->m_rpcFailureCount += 1U;
        this->emitStatus(operation, Components::RadioRpcResult::NOT_CONFIGURED);
        this->log_WARNING_LO_RadioRequestFailed(operation, Components::RadioRpcResult::NOT_CONFIGURED);
    }
}

bool CommsDriver_TeensyRfm23::sendRequest(const Components::RadioOperation& operation, U8 enabled) {
    if (!this->isConnected_teensyRequestOut_OutputPort(0)) {
        return false;
    }

    this->m_sequence += 1U;
    if (this->m_sequence == 0U) {
        this->m_sequence = 1U;
    }
    const U8 payloadLen = (operation == Components::RadioOperation::STATUS) ? 1U : 2U;
    this->m_localPacket[0] = LinkCfg::TEENSY_TARGET_RF_STATUS;
    this->m_localPacket[1] = this->m_sequence;
    this->m_localPacket[2] = payloadLen;
    this->m_localPacket[3] = 0U;
    this->m_localPacket[4] = (operation == Components::RadioOperation::STATUS)
                                 ? LinkCfg::TEENSY_RF_OP_STATUS
                                 : LinkCfg::TEENSY_RF_OP_SET_ENABLED;
    this->m_localPacket[5] = enabled;

    this->m_pendingRequestId = this->m_sequence;
    this->m_pendingOperation = operation;
    this->m_pendingEnabled = enabled;
    this->m_pendingRequestTicks = 0U;
    this->m_requestPending = true;
    this->tlmWrite_RequestPending(1U);
    this->tlmWrite_PendingRequestTicks(0U);

    Fw::Buffer localRequest(this->m_localPacket, LOCAL_HEADER_LEN + payloadLen);
    this->teensyRequestOut_out(0, localRequest);
    this->log_ACTIVITY_LO_RadioRequestQueued(operation, this->m_pendingRequestId);
    return true;
}

void CommsDriver_TeensyRfm23::teensyResponseIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    if (!fwBuffer.isValid() || fwBuffer.getSize() < LOCAL_HEADER_LEN) {
        this->rejectResponse(1U, 0U, false);
        return;
    }

    const U8* data = fwBuffer.getData();
    const U8 requestId = data[1];
    if (data[0] != LinkCfg::TEENSY_TARGET_RF_STATUS) {
        this->rejectResponse(2U, requestId, false);
        return;
    }
    if (!this->m_requestPending) {
        this->rejectResponse(3U, requestId, false);
        return;
    }
    if (requestId != this->m_pendingRequestId) {
        this->rejectResponse(4U, requestId, false);
        return;
    }

    const U8 localStatus = data[2];
    const U8 payloadLen = data[3];
    const FwSizeType expectedSize = static_cast<FwSizeType>(LOCAL_HEADER_LEN + payloadLen);
    if (fwBuffer.getSize() != expectedSize) {
        this->rejectResponse(5U, requestId, true);
        return;
    }

    // Protocol-level errors may not contain an operation payload. They still
    // correlate to and complete the pending request, but do not overwrite the
    // last factual radio status.
    if (payloadLen == 0U) {
        const Components::RadioOperation operation = this->m_pendingOperation;
        Components::RadioRpcResult result = mapLocalStatus(localStatus);
        // OK and TARGET_ERROR are factual only when the operation-specific
        // body is present. Without that body, reporting either result would
        // let CommsApp mistake cached defaults for a fresh hardware state.
        if (result == Components::RadioRpcResult::OK ||
            result == Components::RadioRpcResult::TARGET_ERROR) {
            result = Components::RadioRpcResult::BAD_RESPONSE;
        }
        this->clearPending();
        this->m_rpcFailureCount += 1U;
        this->emitStatus(operation, result);
        this->log_WARNING_LO_RadioRequestFailed(operation, result);
        return;
    }

    bool parsed = false;
    if (this->m_pendingOperation == Components::RadioOperation::STATUS) {
        parsed = this->parseStatusResponse(data, fwBuffer.getSize());
    } else if (this->m_pendingOperation == Components::RadioOperation::SET_ENABLED) {
        parsed = this->parseSetEnabledResponse(data, fwBuffer.getSize());
    }
    if (!parsed) {
        this->rejectResponse(6U, requestId, true);
        return;
    }

    const Components::RadioOperation operation = this->m_pendingOperation;
    const Components::RadioRpcResult result = mapLocalStatus(localStatus);
    this->clearPending();
    if (result != Components::RadioRpcResult::OK) {
        this->m_rpcFailureCount += 1U;
        this->log_WARNING_LO_RadioRequestFailed(operation, result);
    }
    this->emitStatus(operation, result);
}

bool CommsDriver_TeensyRfm23::parseStatusResponse(const U8* data, FwSizeType size) {
    if (size != static_cast<FwSizeType>(LOCAL_HEADER_LEN + STATUS_RESPONSE_PAYLOAD_LEN) ||
        data[3] != STATUS_RESPONSE_PAYLOAD_LEN ||
        data[4] != LinkCfg::TEENSY_RF_OP_STATUS ||
        !validState(data[25]) || !validFault(data[26]) || data[28] > 1U) {
        return false;
    }

    const Components::RadioState previousState = this->m_radioState;
    this->m_rssiDbm = static_cast<I16>(readLe16(&data[5]));
    this->m_rfRxPackets = readLe32(&data[13]);
    this->m_rfTxPackets = readLe32(&data[17]);
    this->m_rfTxDrops = readLe32(&data[21]);
    this->m_radioState = static_cast<Components::RadioState::T>(data[25]);
    this->m_radioFault = static_cast<Components::RadioFault::T>(data[26]);
    this->m_bootFlags = data[27];
    this->m_rssiValid = data[28];
    this->m_rssiAgeMs = readLe32(&data[29]);
    this->m_initAttempts = readLe32(&data[33]);

    if (previousState != this->m_radioState) {
        this->log_ACTIVITY_HI_RadioStateChanged(previousState, this->m_radioState, this->m_radioFault);
    }
    return true;
}

bool CommsDriver_TeensyRfm23::parseSetEnabledResponse(const U8* data, FwSizeType size) {
    if (size != static_cast<FwSizeType>(LOCAL_HEADER_LEN + SET_ENABLED_RESPONSE_PAYLOAD_LEN) ||
        data[3] != SET_ENABLED_RESPONSE_PAYLOAD_LEN ||
        data[4] != LinkCfg::TEENSY_RF_OP_SET_ENABLED ||
        data[5] != this->m_pendingEnabled ||
        !validState(data[6]) || !validFault(data[7])) {
        return false;
    }

    const Components::RadioState previousState = this->m_radioState;
    this->m_radioState = static_cast<Components::RadioState::T>(data[6]);
    this->m_radioFault = static_cast<Components::RadioFault::T>(data[7]);
    if (previousState != this->m_radioState) {
        this->log_ACTIVITY_HI_RadioStateChanged(previousState, this->m_radioState, this->m_radioFault);
    }
    return true;
}

void CommsDriver_TeensyRfm23::emitStatus(const Components::RadioOperation& operation,
                                         const Components::RadioRpcResult& result) {
    this->tlmWrite_LastOperation(operation);
    this->tlmWrite_RequestCount(this->m_requestCount);
    this->tlmWrite_RequestPending(this->m_requestPending ? 1U : 0U);
    this->tlmWrite_PendingRequestTicks(this->m_pendingRequestTicks);
    this->tlmWrite_RpcFailureCount(this->m_rpcFailureCount);
    this->tlmWrite_RejectedResponseCount(this->m_rejectedResponseCount);
    this->tlmWrite_RadioState(this->m_radioState);
    this->tlmWrite_RadioFault(this->m_radioFault);
    this->tlmWrite_RadioBootFlags(this->m_bootFlags);
    this->tlmWrite_RadioInitAttempts(this->m_initAttempts);
    this->tlmWrite_RssiValid(this->m_rssiValid);
    this->tlmWrite_RssiDbm(this->m_rssiDbm);
    this->tlmWrite_RssiAgeMs(this->m_rssiAgeMs);
    this->tlmWrite_RfRxPackets(this->m_rfRxPackets);
    this->tlmWrite_RfTxPackets(this->m_rfTxPackets);
    this->tlmWrite_RfTxDrops(this->m_rfTxDrops);

    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(0,
                            operation,
                            result,
                            this->m_radioState,
                            this->m_radioFault,
                            this->m_bootFlags,
                            this->m_rssiValid,
                            this->m_rssiDbm,
                            this->m_rssiAgeMs,
                            this->m_initAttempts,
                            this->m_rfRxPackets,
                            this->m_rfTxPackets,
                            this->m_rfTxDrops);
    }
    if (this->isConnected_rfRxCountOut_OutputPort(0)) {
        this->rfRxCountOut_out(0, this->m_rfRxPackets);
    }
}

void CommsDriver_TeensyRfm23::rejectResponse(U32 reason, U8 receivedId, bool clearPendingRequest) {
    const U8 expectedId = this->m_requestPending ? this->m_pendingRequestId : 0U;
    const Components::RadioOperation operation = this->m_pendingOperation;
    this->m_rejectedResponseCount += 1U;
    this->m_rpcFailureCount += 1U;
    this->log_WARNING_LO_RadioResponseRejected(reason, receivedId, expectedId);
    if (clearPendingRequest) {
        this->clearPending();
        this->emitStatus(operation, Components::RadioRpcResult::BAD_RESPONSE);
        this->log_WARNING_LO_RadioRequestFailed(operation, Components::RadioRpcResult::BAD_RESPONSE);
    } else {
        this->tlmWrite_RpcFailureCount(this->m_rpcFailureCount);
        this->tlmWrite_RejectedResponseCount(this->m_rejectedResponseCount);
    }
}

void CommsDriver_TeensyRfm23::clearPending() {
    this->m_requestPending = false;
    this->m_pendingRequestTicks = 0U;
    this->tlmWrite_RequestPending(0U);
    this->tlmWrite_PendingRequestTicks(0U);
}

void CommsDriver_TeensyRfm23::timeoutPendingRequest() {
    const Components::RadioOperation operation = this->m_pendingOperation;
    const U8 requestId = this->m_pendingRequestId;
    this->clearPending();
    this->m_rpcFailureCount += 1U;
    this->emitStatus(operation, Components::RadioRpcResult::TIMEOUT);
    this->log_WARNING_LO_RadioRequestTimedOut(operation, requestId);
    this->log_WARNING_LO_RadioRequestFailed(operation, Components::RadioRpcResult::TIMEOUT);
}

bool CommsDriver_TeensyRfm23::validState(U8 state) {
    return state == LinkCfg::TEENSY_RF_STATE_OFF || state == LinkCfg::TEENSY_RF_STATE_READY;
}

bool CommsDriver_TeensyRfm23::validFault(U8 fault) {
    return fault == LinkCfg::TEENSY_RF_FAULT_NONE ||
           fault == LinkCfg::TEENSY_RF_FAULT_INIT_FAILED ||
           fault == LinkCfg::TEENSY_RF_FAULT_WATCHDOG_RESET ||
           fault == LinkCfg::TEENSY_RF_FAULT_LOCAL_TX;
}

Components::RadioRpcResult CommsDriver_TeensyRfm23::mapLocalStatus(U8 status) {
    switch (status) {
        case LinkCfg::TEENSY_STATUS_OK:
            return Components::RadioRpcResult::OK;
        case LinkCfg::TEENSY_STATUS_BAD_REQUEST:
            return Components::RadioRpcResult::BAD_REQUEST;
        case LinkCfg::TEENSY_STATUS_BUSY:
            return Components::RadioRpcResult::BUSY;
        case LinkCfg::TEENSY_STATUS_TIMEOUT:
            return Components::RadioRpcResult::TIMEOUT;
        case LinkCfg::TEENSY_STATUS_TARGET_ERROR:
            return Components::RadioRpcResult::TARGET_ERROR;
        default:
            return Components::RadioRpcResult::BAD_RESPONSE;
    }
}

U16 CommsDriver_TeensyRfm23::readLe16(const U8* data) {
    return static_cast<U16>(data[0]) | (static_cast<U16>(data[1]) << 8U);
}

U32 CommsDriver_TeensyRfm23::readLe32(const U8* data) {
    return static_cast<U32>(data[0]) |
           (static_cast<U32>(data[1]) << 8U) |
           (static_cast<U32>(data[2]) << 16U) |
           (static_cast<U32>(data[3]) << 24U);
}

}  // namespace Components
