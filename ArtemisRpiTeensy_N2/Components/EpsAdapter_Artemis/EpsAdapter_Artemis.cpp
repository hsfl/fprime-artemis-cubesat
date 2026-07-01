#include "Components/EpsAdapter_Artemis/EpsAdapter_Artemis.hpp"

#include <cstring>

namespace Components {

EpsAdapter_Artemis::EpsAdapter_Artemis(const char* const compName)
    : EpsAdapter_ArtemisComponentBase(compName),
      m_sequence(0),
      m_pendingRequestId(0),
      m_pendingOpcode(0),
      m_pendingRequestTicks(0),
      m_requestPending(false),
      m_lastRequest(Components::EpsRequest::GET_SUMMARY_STATUS),
      m_protocolVersion(0),
      m_outputBitmap(0),
      m_resetCause(0),
      m_faultBitmap(0),
      m_uptimeSeconds(0),
      m_capabilities(0),
      m_lastPduStatus(0),
      m_lastOpcode(0),
      m_transportFailureCount(0) {
    std::memset(this->m_localPacket, 0, sizeof(this->m_localPacket));
}

EpsAdapter_Artemis::~EpsAdapter_Artemis() {}

void EpsAdapter_Artemis::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void EpsAdapter_Artemis::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    if (this->m_requestPending) {
        this->m_pendingRequestTicks += 1U;
        if (this->m_pendingRequestTicks >= PDU_REQUEST_TIMEOUT_TICKS) {
            this->timeoutPendingRequest();
        }
    }
    this->writeTelemetry();
}

void EpsAdapter_Artemis::requestIn_handler(
    FwIndexType portNum,
    const Components::EpsRequest& request,
    U8 outputId,
    U8 state,
    U16 durationMs
) {
    static_cast<void>(portNum);
    this->m_lastRequest = request;
    const U8 opcode = mapRequestToOpcode(request);

    U8 payload[PDU_V2_MAX_PAYLOAD_LEN] = {};
    U8 payloadLen = 0;
    if (request == Components::EpsRequest::GET_OUTPUT_STATE) {
        payload[0] = outputId;
        payloadLen = 1;
    } else if (request == Components::EpsRequest::SET_OUTPUT_STATE) {
        payload[0] = outputId;
        payload[1] = state;
        payloadLen = 2;
    } else if (request == Components::EpsRequest::POWER_CYCLE_OUTPUT) {
        payload[0] = outputId;
        payload[1] = static_cast<U8>(durationMs & 0xFFU);
        payload[2] = static_cast<U8>((durationMs >> 8U) & 0xFFU);
        payloadLen = PDU_V2_POWER_CYCLE_REQ_LEN;
    } else if (request == Components::EpsRequest::SET_CHARGER_STATE) {
        payload[0] = state;
        payloadLen = PDU_V2_SET_CHARGER_STATE_REQ_LEN;
    }

    if (!this->sendPduRequest(opcode, payload, payloadLen)) {
        const U8 linkState = this->m_requestPending ? LINK_BUSY : LINK_NOT_CONFIGURED;
        const U8 status = this->m_requestPending ? 0xFDU : 0xFEU;
        this->m_transportFailureCount += 1U;
        this->emitStatus(Components::HealthState::UNKNOWN, linkState, status, opcode);
        this->log_WARNING_LO_PduRequestFailed(request, status);
        this->writeTelemetry();
        return;
    }

    this->emitStatus(Components::HealthState::UNKNOWN, LINK_REQUEST_QUEUED, 0, opcode);
    this->log_ACTIVITY_LO_PduRequestQueued(request, this->m_pendingRequestId);
    this->writeTelemetry();
}

bool EpsAdapter_Artemis::sendPduRequest(U8 opcode, const U8* payload, U8 payloadLen) {
    if (payloadLen > PDU_V2_MAX_PAYLOAD_LEN) {
        return false;
    }
    if (this->m_requestPending || !this->isConnected_teensyRequestOut_OutputPort(0)) {
        return false;
    }

    const U8 seq = ++this->m_sequence;
    U8 pduFrame[PDU_V2_MAX_FRAME_LEN] = {};
    pduFrame[0] = PDU_V2_SOF;
    pduFrame[1] = PDU_V2_VERSION;
    pduFrame[2] = PDU_V2_MSG_REQUEST;
    pduFrame[3] = opcode;
    pduFrame[4] = seq;
    pduFrame[5] = 0;
    pduFrame[6] = payloadLen;
    if (payloadLen > 0U) {
        std::memcpy(&pduFrame[PDU_V2_HEADER_LEN], payload, payloadLen);
    }
    const U32 crcInputLen = 6U + payloadLen;
    const U16 crc = crc16Ccitt(&pduFrame[1], crcInputLen);
    const U32 crcIndex = PDU_V2_HEADER_LEN + payloadLen;
    pduFrame[crcIndex] = static_cast<U8>(crc & 0xFFU);
    pduFrame[crcIndex + 1U] = static_cast<U8>((crc >> 8U) & 0xFFU);

    const U32 frameLen = PDU_V2_HEADER_LEN + payloadLen + PDU_V2_CRC_LEN;
    const U32 packetLen = LOCAL_HEADER_LEN + frameLen;
    if (packetLen > sizeof(this->m_localPacket)) {
        return false;
    }
    this->m_localPacket[0] = LinkCfg::TEENSY_TARGET_PDU;
    this->m_localPacket[1] = seq;
    this->m_localPacket[2] = static_cast<U8>(frameLen);
    this->m_localPacket[3] = 0;
    std::memcpy(&this->m_localPacket[LOCAL_HEADER_LEN], pduFrame, frameLen);

    this->m_pendingRequestId = seq;
    this->m_pendingOpcode = opcode;
    this->m_pendingRequestTicks = 0;
    this->m_requestPending = true;
    Fw::Buffer localRequest(this->m_localPacket, packetLen);
    this->teensyRequestOut_out(0, localRequest);
    return true;
}

void EpsAdapter_Artemis::teensyResponseIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    if (!fwBuffer.isValid() || fwBuffer.getSize() < LOCAL_HEADER_LEN) {
        this->m_transportFailureCount += 1U;
        return;
    }

    const U8* data = fwBuffer.getData();
    const U8 target = data[0];
    const U8 requestId = data[1];
    const U8 localStatus = data[2];
    const U8 payloadLen = data[3];
    if ((target != LinkCfg::TEENSY_TARGET_PDU) || !this->m_requestPending ||
        (requestId != this->m_pendingRequestId) ||
        (fwBuffer.getSize() < static_cast<FwSizeType>(LOCAL_HEADER_LEN + payloadLen))) {
        this->m_transportFailureCount += 1U;
        if ((target == LinkCfg::TEENSY_TARGET_PDU) && this->m_requestPending &&
            (requestId == this->m_pendingRequestId)) {
            this->m_requestPending = false;
            this->m_pendingRequestTicks = 0;
            this->emitStatus(Components::HealthState::UNKNOWN, LINK_ERROR, 0xFCU, this->m_pendingOpcode);
            this->writeTelemetry();
        }
        return;
    }

    if (localStatus != LinkCfg::TEENSY_STATUS_OK) {
        this->m_requestPending = false;
        this->m_pendingRequestTicks = 0;
        this->m_transportFailureCount += 1U;
        this->m_lastPduStatus = localStatus;
        this->m_lastOpcode = this->m_pendingOpcode;
        this->emitStatus(Components::HealthState::UNKNOWN, LINK_ERROR, localStatus, this->m_pendingOpcode);
        this->log_WARNING_LO_PduRequestFailed(this->m_lastRequest, localStatus);
        this->writeTelemetry();
        return;
    }

    PduResponse response = {};
    const bool parsed = this->parsePduResponse(
        &data[LOCAL_HEADER_LEN],
        payloadLen,
        this->m_pendingRequestId,
        this->m_pendingOpcode,
        response);
    this->m_requestPending = false;
    this->m_pendingRequestTicks = 0;
    if (!parsed) {
        this->m_transportFailureCount += 1U;
        this->emitStatus(Components::HealthState::UNKNOWN, LINK_ERROR, 0xFBU, this->m_pendingOpcode);
        this->log_WARNING_LO_PduRequestFailed(this->m_lastRequest, 0xFBU);
        this->writeTelemetry();
        return;
    }

    this->processResponse(this->m_lastRequest, response);
    this->writeTelemetry();
}

bool EpsAdapter_Artemis::parsePduResponse(
    const U8* frame,
    U32 frameLen,
    U8 expectedSeq,
    U8 expectedOpcode,
    PduResponse& response
) {
    if ((frame == nullptr) || (frameLen < (PDU_V2_HEADER_LEN + PDU_V2_CRC_LEN)) ||
        (frameLen > PDU_V2_MAX_FRAME_LEN)) {
        return false;
    }
    const U8 payloadLen = frame[6];
    const U32 expectedLen = PDU_V2_HEADER_LEN + payloadLen + PDU_V2_CRC_LEN;
    if ((payloadLen > PDU_V2_MAX_PAYLOAD_LEN) || (frameLen != expectedLen)) {
        return false;
    }
    const U16 expectedCrc = static_cast<U16>(frame[expectedLen - 2U]) |
                            static_cast<U16>(frame[expectedLen - 1U] << 8U);
    const U16 actualCrc = crc16Ccitt(&frame[1], expectedLen - 3U);
    if (actualCrc != expectedCrc) {
        return false;
    }
    if ((frame[0] != PDU_V2_SOF) || (frame[1] != PDU_V2_VERSION) || (frame[2] != PDU_V2_MSG_RESPONSE) ||
        (frame[3] != expectedOpcode) || (frame[4] != expectedSeq)) {
        return false;
    }
    response.opcode = frame[3];
    response.status = frame[5];
    response.payloadLen = payloadLen;
    if (response.payloadLen > 0U) {
        std::memcpy(response.payload, &frame[PDU_V2_HEADER_LEN], response.payloadLen);
    }
    return true;
}

void EpsAdapter_Artemis::processResponse(const Components::EpsRequest& request, const PduResponse& response) {
    this->m_lastPduStatus = response.status;
    this->m_lastOpcode = response.opcode;

    Components::HealthState health =
        (response.status == PDU_V2_STATUS_OK) ? Components::HealthState::OK : Components::HealthState::WARN;
    if ((request == Components::EpsRequest::GET_SUMMARY_STATUS) &&
        (response.status == PDU_V2_STATUS_OK) &&
        (response.payloadLen >= PDU_V2_SUMMARY_STATUS_LEN)) {
        this->m_outputBitmap = static_cast<U16>(response.payload[0]) | static_cast<U16>(response.payload[1] << 8U);
        this->m_resetCause = response.payload[2];
        this->m_faultBitmap = response.payload[3];
        this->m_uptimeSeconds = readLe32(&response.payload[4]);
        this->m_capabilities = response.payload[8];
        this->m_protocolVersion = PDU_V2_VERSION;
        if (this->m_faultBitmap != 0U) {
            health = Components::HealthState::WARN;
        }
    } else if ((request == Components::EpsRequest::GET_PROTOCOL_INFO) &&
               (response.status == PDU_V2_STATUS_OK) &&
               (response.payloadLen >= PDU_V2_PROTOCOL_INFO_LEN)) {
        this->m_protocolVersion = response.payload[0];
        this->m_capabilities = response.payload[1];
    } else if ((request == Components::EpsRequest::PING) &&
               (response.status == PDU_V2_STATUS_OK) &&
               (response.payloadLen >= PDU_V2_PING_LEN)) {
        this->m_protocolVersion = response.payload[0];
    }

    this->emitStatus(health, LINK_PROTOCOL_OK, response.status, response.opcode);
    if (response.status == PDU_V2_STATUS_OK) {
        this->log_ACTIVITY_LO_PduRequestHandled(request, response.status);
    } else {
        this->log_WARNING_LO_PduRequestFailed(request, response.status);
    }
}

void EpsAdapter_Artemis::emitStatus(Components::HealthState health, U8 linkState, U8 pduStatus, U8 opcode) {
    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(
            0,
            health,
            linkState,
            this->m_protocolVersion,
            this->m_outputBitmap,
            this->m_resetCause,
            this->m_faultBitmap,
            this->m_uptimeSeconds,
            this->m_capabilities,
            pduStatus,
            opcode);
    }
}

void EpsAdapter_Artemis::writeTelemetry() {
    this->tlmWrite_LastRequest(this->m_lastRequest);
    this->tlmWrite_LastPduStatus(this->m_lastPduStatus);
    this->tlmWrite_LastPduOpcode(this->m_lastOpcode);
    this->tlmWrite_PduProtocolVersion(this->m_protocolVersion);
    this->tlmWrite_PduOutputBitmap(this->m_outputBitmap);
    this->tlmWrite_PduFaultBitmap(this->m_faultBitmap);
    this->tlmWrite_PduUptimeSeconds(this->m_uptimeSeconds);
    this->tlmWrite_TransportFailureCount(this->m_transportFailureCount);
    this->tlmWrite_PendingRequestTicks(this->m_pendingRequestTicks);
}

void EpsAdapter_Artemis::timeoutPendingRequest() {
    const U8 timedOutRequestId = this->m_pendingRequestId;
    const U8 timedOutOpcode = this->m_pendingOpcode;
    this->m_requestPending = false;
    this->m_pendingRequestTicks = 0;
    this->m_transportFailureCount += 1U;
    this->m_lastPduStatus = LinkCfg::TEENSY_STATUS_TIMEOUT;
    this->m_lastOpcode = timedOutOpcode;
    this->emitStatus(
        Components::HealthState::UNKNOWN,
        LINK_ERROR,
        LinkCfg::TEENSY_STATUS_TIMEOUT,
        timedOutOpcode);
    this->log_WARNING_LO_PduRequestTimedOut(this->m_lastRequest, timedOutRequestId);
    this->log_WARNING_LO_PduRequestFailed(this->m_lastRequest, LinkCfg::TEENSY_STATUS_TIMEOUT);
}

U16 EpsAdapter_Artemis::crc16Ccitt(const U8* data, U32 length) {
    U16 crc = 0xFFFFU;
    for (U32 index = 0; index < length; ++index) {
        crc ^= static_cast<U16>(data[index] << 8U);
        for (U32 bit = 0; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    return crc;
}

U32 EpsAdapter_Artemis::readLe32(const U8* data) {
    return static_cast<U32>(data[0]) |
           (static_cast<U32>(data[1]) << 8U) |
           (static_cast<U32>(data[2]) << 16U) |
           (static_cast<U32>(data[3]) << 24U);
}

U8 EpsAdapter_Artemis::mapRequestToOpcode(const Components::EpsRequest& request) {
    switch (request.e) {
        case Components::EpsRequest::PING:
            return PDU_V2_OP_PING;
        case Components::EpsRequest::GET_PROTOCOL_INFO:
            return PDU_V2_OP_GET_PROTOCOL_INFO;
        case Components::EpsRequest::GET_OUTPUT_STATE:
            return PDU_V2_OP_GET_OUTPUT_STATE;
        case Components::EpsRequest::SET_OUTPUT_STATE:
            return PDU_V2_OP_SET_OUTPUT_STATE;
        case Components::EpsRequest::POWER_CYCLE_OUTPUT:
            return PDU_V2_OP_POWER_CYCLE_OUTPUT;
        case Components::EpsRequest::GET_CHARGER_STATUS:
            return PDU_V2_OP_GET_CHARGER_STATUS;
        case Components::EpsRequest::SET_CHARGER_STATE:
            return PDU_V2_OP_SET_CHARGER_STATE;
        case Components::EpsRequest::GET_SUMMARY_STATUS:
        default:
            return PDU_V2_OP_GET_SUMMARY_STATUS;
    }
}

}  // namespace Components
