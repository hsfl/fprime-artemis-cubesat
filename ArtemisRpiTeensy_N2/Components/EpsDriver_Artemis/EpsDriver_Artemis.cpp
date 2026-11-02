#include "Components/EpsDriver_Artemis/EpsDriver_Artemis.hpp"

#include <cstring>

namespace Components {

namespace {

constexpr U8 REQUEST_STATUS_BUSY = 0xFDU;
constexpr U8 REQUEST_STATUS_NOT_CONFIGURED = 0xFEU;
constexpr U8 OUTPUT_IDS[PDU_V2_OUTPUT_COUNT] = {
    PDU_OUTPUT_3V3_1,
    PDU_OUTPUT_3V3_2,
    PDU_OUTPUT_5V_1,
    PDU_OUTPUT_5V_2,
    PDU_OUTPUT_5V_3,
    PDU_OUTPUT_12V,
    PDU_OUTPUT_VBATT,
    PDU_OUTPUT_BURN1,
    PDU_OUTPUT_BURN2,
};

bool outputBitIndex(U8 outputId, U8& bitIndex) {
    for (U8 index = 0; index < PDU_V2_OUTPUT_COUNT; ++index) {
        if (OUTPUT_IDS[index] == outputId) {
            bitIndex = index;
            return true;
        }
    }
    return false;
}

bool updateOutputBitmap(U16& bitmap, U8 outputId, U8 state) {
    U8 bitIndex = 0;
    if ((state > 1U) || !outputBitIndex(outputId, bitIndex)) {
        return false;
    }
    const U16 bit = static_cast<U16>(1U << bitIndex);
    bitmap = (state == 0U) ? static_cast<U16>(bitmap & static_cast<U16>(~bit)) :
                             static_cast<U16>(bitmap | bit);
    return true;
}

bool isLowerHex(U8 value) {
    return ((value >= static_cast<U8>('0')) && (value <= static_cast<U8>('9'))) ||
           ((value >= static_cast<U8>('a')) && (value <= static_cast<U8>('f')));
}

const char* errorName(Components::PduErrorOrigin origin, U8 code) {
    if (origin == Components::PduErrorOrigin::TEENSY_RPC) {
        switch (code) {
            case 1: return "BAD_REQUEST";
            case 2: return "BUSY";
            case 3: return "TIMEOUT";
            case 4: return "TARGET_ERROR";
            default: return "UNKNOWN";
        }
    }
    if (origin == Components::PduErrorOrigin::PDU) {
        switch (code) {
            case 1: return "BAD_OPCODE";
            case 2: return "BAD_LENGTH";
            case 3: return "BAD_PARAM";
            case 4: return "HW_FAULT";
            case 5: return "NOT_IMPLEMENTED";
            case 6: return "BUSY";
            default: return "UNKNOWN";
        }
    }
    switch (code) {
        case 0xF9: return "READBACK_MISMATCH";
        case 0xFA: return "MALFORMED_PAYLOAD";
        case 0xFB: return "MALFORMED_FRAME";
        case 0xFD: return "BUSY";
        case 0xFE: return "NOT_CONFIGURED";
        default: return "UNKNOWN";
    }
}

Components::PduRail railFromId(U8 id) {
    U8 index = 0;
    if ((id != PDU_V2_OUTPUT_ALL) && !outputBitIndex(id, index)) {
        return Components::PduRail::UNKNOWN;
    }
    return Components::PduRail(static_cast<Components::PduRail::T>(id));
}

Components::PduRailState railState(U16 bitmap, U8 index) {
    return (bitmap & static_cast<U16>(1U << index)) ? Components::PduRailState::ON : Components::PduRailState::OFF;
}

}  // namespace

EpsDriver_Artemis::EpsDriver_Artemis(const char* const compName)
    : EpsDriver_ArtemisComponentBase(compName),
      m_sequence(0),
      m_pendingRequestId(0),
      m_pendingOpcode(0),
      m_pendingRequestTicks(0),
      m_requestPending(false),
      m_lastRequest(Components::EpsRequest::GET_SUMMARY_STATUS),
      m_pendingRequest(Components::EpsRequest::GET_SUMMARY_STATUS),
      m_pendingOutputId(0),
      m_pendingState(0),
      m_protocolVersion(0),
      m_outputBitmap(0),
      m_railKnownMask(0),
      m_railFreshMask(0),
      m_railAgeTicks{},
      m_resetCause(0),
      m_faultBitmap(0),
      m_uptimeSeconds(0),
      m_capabilities(0),
      m_firmwareMajor(0),
      m_firmwareMinor(0),
      m_firmwarePatch(0),
      m_buildFlags(0),
      m_gitHash("000000000000"),
      m_lastPduStatus(0),
      m_lastOpcode(0),
      m_transportFailureCount(0) {}

EpsDriver_Artemis::~EpsDriver_Artemis() {}

void EpsDriver_Artemis::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void EpsDriver_Artemis::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    bool timeout = false;
    {
        Os::ScopeLock lock(this->m_stateMutex);
        for (U8 i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) {
            const U16 bit = static_cast<U16>(1U << i);
            if ((this->m_railKnownMask & bit) != 0U) {
                if (this->m_railAgeTicks[i] < 0xFFFFFFFFU) { ++this->m_railAgeTicks[i]; }
                if (this->m_railAgeTicks[i] > RAIL_FRESH_TICKS) { this->m_railFreshMask &= static_cast<U16>(~bit); }
            }
        }
        if (this->m_requestPending) {
            this->m_pendingRequestTicks += 1U;
            timeout = this->m_pendingRequestTicks >= PDU_REQUEST_TIMEOUT_TICKS;
        }
    }
    if (timeout) {
        this->timeoutPendingRequest();
    }
    this->writeTelemetry();
    this->publishRailSnapshot();
}

void EpsDriver_Artemis::requestIn_handler(
    FwIndexType portNum,
    const Components::EpsRequest& request,
    U8 outputId,
    U8 state,
    U16 durationMs
) {
    static_cast<void>(portNum);
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

    const U8 admissionStatus = this->sendPduRequest(request, outputId, state, opcode, payload, payloadLen);
    if (admissionStatus == 0U) {
        return;
    }

    const U8 linkState = (admissionStatus == REQUEST_STATUS_BUSY) ? LINK_BUSY : LINK_NOT_CONFIGURED;
    {
        Os::ScopeLock lock(this->m_stateMutex);
        this->m_transportFailureCount += 1U;
        this->m_lastPduStatus = admissionStatus;
        this->m_lastOpcode = opcode;
        if (admissionStatus != REQUEST_STATUS_BUSY) { this->m_railFreshMask = 0; }
    }
    this->emitStatus(Components::HealthState::UNKNOWN, linkState, admissionStatus, opcode, request);
    this->log_WARNING_LO_PduRequestFailed(request, admissionStatus);
    this->log_WARNING_HI_PduOperationError(request, railFromId(outputId), Components::PduErrorOrigin::DRIVER,
                                           Fw::String(errorName(Components::PduErrorOrigin::DRIVER, admissionStatus)), admissionStatus, 0);
    this->writeTelemetry();
    this->publishRailSnapshot();
}

U8 EpsDriver_Artemis::sendPduRequest(
    Components::EpsRequest request,
    U8 outputId,
    U8 state,
    U8 opcode,
    const U8* payload,
    U8 payloadLen
) {
    if (payloadLen > PDU_V2_MAX_PAYLOAD_LEN) {
        return DRIVER_STATUS_MALFORMED_PAYLOAD;
    }

    U8 localPacket[LOCAL_MAX_PACKET_LEN] = {};
    U32 packetLen = 0;
    U8 sequence = 0;
    {
        Os::ScopeLock lock(this->m_stateMutex);
        if (this->m_requestPending) {
            return REQUEST_STATUS_BUSY;
        }
        if (!this->isConnected_teensyRequestOut_OutputPort(0)) {
            return REQUEST_STATUS_NOT_CONFIGURED;
        }

        sequence = ++this->m_sequence;
        U8 pduFrame[PDU_V2_MAX_FRAME_LEN] = {};
        pduFrame[0] = PDU_V2_SOF;
        pduFrame[1] = PDU_V2_VERSION;
        pduFrame[2] = PDU_V2_MSG_REQUEST;
        pduFrame[3] = opcode;
        pduFrame[4] = sequence;
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
        packetLen = LOCAL_HEADER_LEN + frameLen;
        if (packetLen > sizeof(localPacket)) {
            return DRIVER_STATUS_MALFORMED_PAYLOAD;
        }
        localPacket[0] = LinkCfg::TEENSY_TARGET_PDU;
        localPacket[1] = sequence;
        localPacket[2] = static_cast<U8>(frameLen);
        localPacket[3] = 0;
        std::memcpy(&localPacket[LOCAL_HEADER_LEN], pduFrame, frameLen);

        // Request identity changes only after the request is admitted.
        this->m_pendingRequestId = sequence;
        this->m_pendingOpcode = opcode;
        this->m_pendingRequest = request;
        this->m_pendingOutputId = outputId;
        this->m_pendingState = state;
        this->m_pendingRequestTicks = 0;
        this->m_requestPending = true;
        this->m_lastRequest = request;
    }

    // Queue the admission notice before handing off the packet; the RX path can
    // respond synchronously in tests and must never overtake this notification.
    this->emitStatus(Components::HealthState::UNKNOWN, LINK_REQUEST_QUEUED, 0, opcode, request);
    this->log_ACTIVITY_LO_PduRequestQueued(request, sequence);
    this->writeTelemetry();
    Fw::Buffer localRequest(localPacket, packetLen);
    this->teensyRequestOut_out(0, localRequest);
    return 0;
}

void EpsDriver_Artemis::teensyResponseIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    Components::EpsRequest request = Components::EpsRequest::GET_SUMMARY_STATUS;
    U8 requestId = 0;
    U8 opcode = 0;
    U8 status = 0;
    U8 linkState = LINK_ERROR;
    Components::HealthState health = Components::HealthState::UNKNOWN;
    bool report = false;
    bool handled = false;
    U8 outputId = 0;
    U8 desired = 0;
    U16 bitmap = 0;
    U16 previousBitmap = 0, previousKnown = 0;
    U8 protocol = 0, major = 0, minor = 0, patch = 0, flags = 0;
    Fw::String gitHash;
    Components::PduErrorOrigin origin = Components::PduErrorOrigin::DRIVER;

    {
        Os::ScopeLock lock(this->m_stateMutex);
        previousBitmap = this->m_outputBitmap;
        previousKnown = this->m_railKnownMask;
        if (!fwBuffer.isValid() || (fwBuffer.getSize() < LOCAL_HEADER_LEN)) {
            this->m_transportFailureCount += 1U;
            if (this->m_requestPending) {
                request = this->m_pendingRequest;
                outputId = this->m_pendingOutputId;
                desired = this->m_pendingState;
                requestId = this->m_pendingRequestId;
                opcode = this->m_pendingOpcode;
                this->m_requestPending = false;
                this->m_pendingRequestTicks = 0;
                this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_FRAME;
                this->m_lastOpcode = opcode;
                status = DRIVER_STATUS_MALFORMED_FRAME;
                report = true;
            }
        } else {
            const U8* data = fwBuffer.getData();
            const U8 target = data[0];
            const U8 responseId = data[1];
            const U8 localStatus = data[2];
            const U8 payloadLen = data[3];
            const bool matchingPending = this->m_requestPending &&
                                         (target == LinkCfg::TEENSY_TARGET_PDU) &&
                                         (responseId == this->m_pendingRequestId);
            if (!matchingPending) {
                this->m_transportFailureCount += 1U;
            } else {
                request = this->m_pendingRequest;
                outputId = this->m_pendingOutputId;
                desired = this->m_pendingState;
                requestId = this->m_pendingRequestId;
                opcode = this->m_pendingOpcode;
                this->m_requestPending = false;
                this->m_pendingRequestTicks = 0;
                report = true;

                if (fwBuffer.getSize() != static_cast<FwSizeType>(LOCAL_HEADER_LEN + payloadLen)) {
                    this->m_transportFailureCount += 1U;
                    status = DRIVER_STATUS_MALFORMED_FRAME;
                    this->m_lastPduStatus = status;
                    this->m_lastOpcode = opcode;
                } else if (localStatus != LinkCfg::TEENSY_STATUS_OK) {
                    origin = Components::PduErrorOrigin::TEENSY_RPC;
                    this->m_transportFailureCount += 1U;
                    status = localStatus;
                    this->m_lastPduStatus = localStatus;
                    this->m_lastOpcode = opcode;
                } else {
                    PduResponse response = {};
                    const bool parsed = this->parsePduResponse(
                        &data[LOCAL_HEADER_LEN],
                        payloadLen,
                        requestId,
                        opcode,
                        response);
                    if (!parsed) {
                        this->m_transportFailureCount += 1U;
                        status = DRIVER_STATUS_MALFORMED_FRAME;
                        this->m_lastPduStatus = status;
                        this->m_lastOpcode = opcode;
                    } else if (response.status != PDU_V2_STATUS_OK) {
                        origin = Components::PduErrorOrigin::PDU;
                        status = response.status;
                        this->m_lastPduStatus = response.status;
                        this->m_lastOpcode = response.opcode;
                        linkState = LINK_PROTOCOL_OK;
                        health = Components::HealthState::WARN;
                        handled = true;
                    } else if (!this->processResponse(request, response)) {
                        this->m_transportFailureCount += 1U;
                        status = this->m_lastPduStatus;
                        this->m_lastOpcode = opcode;
                        if (status == DRIVER_STATUS_READBACK_MISMATCH) {
                            health = Components::HealthState::WARN;
                        }
                    } else {
                        status = response.status;
                        this->m_lastPduStatus = response.status;
                        this->m_lastOpcode = response.opcode;
                        linkState = LINK_PROTOCOL_OK;
                        health = (this->m_faultBitmap != 0U) ?
                                     Components::HealthState::WARN : Components::HealthState::OK;
                        handled = true;
                    }
                }
            }
        }
        if (report && !handled && status != DRIVER_STATUS_READBACK_MISMATCH) {
            this->m_railFreshMask = 0;
        }
        bitmap = this->m_outputBitmap;
        protocol = this->m_protocolVersion;
        major = this->m_firmwareMajor;
        minor = this->m_firmwareMinor;
        patch = this->m_firmwarePatch;
        flags = this->m_buildFlags;
        gitHash = this->m_gitHash;
    }

    if (!report) {
        this->writeTelemetry();
        return;
    }
    this->emitStatus(health, linkState, status, opcode, request);
    if (handled && (status == PDU_V2_STATUS_OK)) {
        this->log_ACTIVITY_LO_PduRequestHandled(request, status);
        if (request == Components::EpsRequest::GET_OUTPUT_STATE) {
            for (U8 i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) {
                if ((outputId == PDU_V2_OUTPUT_ALL) || (outputId == OUTPUT_IDS[i])) {
                    this->log_ACTIVITY_HI_PduRailReadback(request, railFromId(OUTPUT_IDS[i]), railState(bitmap, i), requestId);
                }
            }
        } else if ((request == Components::EpsRequest::SET_OUTPUT_STATE) ||
                   (request == Components::EpsRequest::POWER_CYCLE_OUTPUT)) {
            U8 index = 0;
            if (outputBitIndex(outputId, index)) {
                if (request == Components::EpsRequest::SET_OUTPUT_STATE) {
                    this->log_ACTIVITY_HI_PduRailSetResult(railFromId(outputId),
                        desired ? Components::PduDesiredState::ON : Components::PduDesiredState::OFF, railState(bitmap, index), requestId);
                } else {
                    this->log_ACTIVITY_HI_PduCycleStarted(railFromId(outputId), railState(bitmap, index), requestId);
                }
            }
        } else if (request == Components::EpsRequest::GET_PROTOCOL_INFO) {
            this->log_ACTIVITY_HI_PduInfoReadback(protocol, major, minor, patch, flags, gitHash, requestId);
        } else if (request == Components::EpsRequest::GET_SUMMARY_STATUS) {
            const U16 changed = static_cast<U16>((bitmap ^ previousBitmap) & previousKnown);
            for (U8 i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) {
                if ((changed & static_cast<U16>(1U << i)) != 0U) {
                    this->log_ACTIVITY_HI_PduRailReadback(request, railFromId(OUTPUT_IDS[i]), railState(bitmap, i), requestId);
                }
            }
        }
    } else {
        this->log_WARNING_LO_PduRequestFailed(request, status);
        this->log_WARNING_HI_PduOperationError(request, railFromId(outputId), origin,
                                               Fw::String(errorName(origin, status)), status, requestId);
        U8 index = 0;
        if ((request == Components::EpsRequest::SET_OUTPUT_STATE) && (status == DRIVER_STATUS_READBACK_MISMATCH) &&
            outputBitIndex(outputId, index)) {
            this->log_WARNING_HI_PduRailSetMismatch(railFromId(outputId),
                desired ? Components::PduDesiredState::ON : Components::PduDesiredState::OFF, railState(bitmap, index), requestId);
        }
    }
    static_cast<void>(requestId);
    this->writeTelemetry();
    this->publishRailSnapshot();
}

bool EpsDriver_Artemis::parsePduResponse(
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

bool EpsDriver_Artemis::processResponse(const Components::EpsRequest& request, const PduResponse& response) {
    this->m_lastPduStatus = response.status;
    this->m_lastOpcode = response.opcode;
    if (response.status != PDU_V2_STATUS_OK) {
        return true;
    }

    if (request == Components::EpsRequest::GET_SUMMARY_STATUS) {
        if (response.payloadLen != PDU_V2_SUMMARY_STATUS_LEN) {
            this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
            return false;
        }
        this->m_outputBitmap = static_cast<U16>(response.payload[0]) |
                               static_cast<U16>(response.payload[1] << 8U);
        this->m_resetCause = response.payload[2];
        this->m_faultBitmap = response.payload[3];
        this->m_uptimeSeconds = readLe32(&response.payload[4]);
        this->m_capabilities = response.payload[8];
        this->m_protocolVersion = PDU_V2_VERSION;
        this->observeRails(static_cast<U16>((1U << PDU_V2_OUTPUT_COUNT) - 1U));
        return true;
    }

    if (request == Components::EpsRequest::GET_PROTOCOL_INFO) {
        if ((response.payloadLen != PDU_V2_PROTOCOL_INFO_LEN) &&
            (response.payloadLen != PDU_V2_PROTOCOL_INFO_EXT_LEN)) {
            this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
            return false;
        }
        const U8 protocolVersion = response.payload[0];
        const U8 capabilities = response.payload[1];
        const U8 firmwareMajor = response.payload[4];
        const U8 firmwareMinor = response.payload[5];
        const U8 firmwarePatch = response.payload[6];
        U8 buildFlags = 0;
        Fw::String gitHash("000000000000");
        if (response.payloadLen == PDU_V2_PROTOCOL_INFO_EXT_LEN) {
            const U8 flags = response.payload[PDU_V2_PROTOCOL_INFO_FLAGS_OFFSET];
            const U8 knownAndDirty = PDU_V2_PROTOCOL_INFO_FLAG_HASH_KNOWN | PDU_V2_PROTOCOL_INFO_FLAG_DIRTY;
            if ((flags & static_cast<U8>(~knownAndDirty)) != 0U) {
                this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
                return false;
            }
            char hash[PDU_V2_PROTOCOL_INFO_HASH_LEN + 1U] = {};
            const bool hashKnown = (flags & PDU_V2_PROTOCOL_INFO_FLAG_HASH_KNOWN) != 0U;
            for (U8 index = 0; index < PDU_V2_PROTOCOL_INFO_HASH_LEN; ++index) {
                const U8 value = response.payload[PDU_V2_PROTOCOL_INFO_HASH_OFFSET + index];
                if (hashKnown && !isLowerHex(value)) {
                    this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
                    return false;
                }
                if (!hashKnown && (value != 0U) && (value != static_cast<U8>('0'))) {
                    this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
                    return false;
                }
                hash[index] = hashKnown ? static_cast<char>(value) : '0';
            }
            buildFlags = flags;
            gitHash = hash;
        }
        this->m_protocolVersion = protocolVersion;
        this->m_capabilities = capabilities;
        this->m_firmwareMajor = firmwareMajor;
        this->m_firmwareMinor = firmwareMinor;
        this->m_firmwarePatch = firmwarePatch;
        this->m_buildFlags = buildFlags;
        this->m_gitHash = gitHash;
        return true;
    }

    if (request == Components::EpsRequest::PING) {
        if (response.payloadLen != PDU_V2_PING_LEN) {
            this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
            return false;
        }
        this->m_protocolVersion = response.payload[0];
        return true;
    }

    if ((request == Components::EpsRequest::GET_OUTPUT_STATE) && (this->m_pendingOutputId == PDU_V2_OUTPUT_ALL)) {
        if ((response.payloadLen != (2U + PDU_V2_OUTPUT_COUNT)) ||
            (response.payload[0] != PDU_V2_OUTPUT_ALL) || (response.payload[1] != PDU_V2_OUTPUT_COUNT)) {
            this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
            return false;
        }
        U16 bitmap = 0;
        for (U8 index = 0; index < PDU_V2_OUTPUT_COUNT; ++index) {
            if (!updateOutputBitmap(bitmap, OUTPUT_IDS[index], response.payload[2U + index])) {
                this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
                return false;
            }
        }
        this->m_outputBitmap = bitmap;
        this->observeRails(static_cast<U16>((1U << PDU_V2_OUTPUT_COUNT) - 1U));
        return true;
    }

    if ((request == Components::EpsRequest::GET_OUTPUT_STATE) ||
        (request == Components::EpsRequest::SET_OUTPUT_STATE) ||
        (request == Components::EpsRequest::POWER_CYCLE_OUTPUT)) {
        if ((response.payloadLen != 2U) || (response.payload[0] != this->m_pendingOutputId) ||
            (response.payload[1] > 1U) ||
            !updateOutputBitmap(this->m_outputBitmap, response.payload[0], response.payload[1])) {
            this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
            return false;
        }
        U8 index = 0;
        if (outputBitIndex(response.payload[0], index)) { this->observeRails(static_cast<U16>(1U << index)); }
        if (((request == Components::EpsRequest::SET_OUTPUT_STATE) &&
             (response.payload[1] != this->m_pendingState)) ||
            ((request == Components::EpsRequest::POWER_CYCLE_OUTPUT) && (response.payload[1] != 0U))) {
            this->m_lastPduStatus = DRIVER_STATUS_READBACK_MISMATCH;
            return false;
        }
        return true;
    }

    if ((request == Components::EpsRequest::GET_CHARGER_STATUS) ||
        (request == Components::EpsRequest::SET_CHARGER_STATE)) {
        if (response.payloadLen != PDU_V2_CHARGER_STATUS_RESP_LEN) {
            this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
            return false;
        }
        return true;
    }

    this->m_lastPduStatus = DRIVER_STATUS_MALFORMED_PAYLOAD;
    return false;
}

void EpsDriver_Artemis::emitStatus(
    Components::HealthState health,
    U8 linkState,
    U8 pduStatus,
    U8 opcode,
    Components::EpsRequest request
) {
    if (!this->isConnected_statusOut_OutputPort(0)) {
        return;
    }
    U8 protocolVersion = 0;
    U16 outputBitmap = 0;
    U8 resetCause = 0;
    U8 faultBitmap = 0;
    U32 uptimeSeconds = 0;
    U8 capabilities = 0;
    U8 firmwareMajor = 0;
    U8 firmwareMinor = 0;
    U8 firmwarePatch = 0;
    U8 buildFlags = 0;
    Fw::String gitHash;
    {
        Os::ScopeLock lock(this->m_stateMutex);
        protocolVersion = this->m_protocolVersion;
        outputBitmap = this->m_outputBitmap;
        resetCause = this->m_resetCause;
        faultBitmap = this->m_faultBitmap;
        uptimeSeconds = this->m_uptimeSeconds;
        capabilities = this->m_capabilities;
        firmwareMajor = this->m_firmwareMajor;
        firmwareMinor = this->m_firmwareMinor;
        firmwarePatch = this->m_firmwarePatch;
        buildFlags = this->m_buildFlags;
        gitHash = this->m_gitHash;
    }
    this->statusOut_out(
        0,
        health,
        linkState,
        protocolVersion,
        outputBitmap,
        resetCause,
        faultBitmap,
        uptimeSeconds,
        capabilities,
        pduStatus,
        opcode,
        request,
        firmwareMajor,
        firmwareMinor,
        firmwarePatch,
        buildFlags,
        gitHash);
}

void EpsDriver_Artemis::writeTelemetry() {
    Components::EpsRequest lastRequest = Components::EpsRequest::GET_SUMMARY_STATUS;
    U8 lastPduStatus = 0;
    U8 lastOpcode = 0;
    U8 protocolVersion = 0;
    U16 outputBitmap = 0;
    U8 faultBitmap = 0;
    U32 uptimeSeconds = 0;
    U32 transportFailureCount = 0;
    U32 pendingRequestTicks = 0;
    U8 firmwareMajor = 0;
    U8 firmwareMinor = 0;
    U8 firmwarePatch = 0;
    U8 buildFlags = 0;
    Fw::String gitHash;
    {
        Os::ScopeLock lock(this->m_stateMutex);
        lastRequest = this->m_lastRequest;
        lastPduStatus = this->m_lastPduStatus;
        lastOpcode = this->m_lastOpcode;
        protocolVersion = this->m_protocolVersion;
        outputBitmap = this->m_outputBitmap;
        faultBitmap = this->m_faultBitmap;
        uptimeSeconds = this->m_uptimeSeconds;
        transportFailureCount = this->m_transportFailureCount;
        pendingRequestTicks = this->m_pendingRequestTicks;
        firmwareMajor = this->m_firmwareMajor;
        firmwareMinor = this->m_firmwareMinor;
        firmwarePatch = this->m_firmwarePatch;
        buildFlags = this->m_buildFlags;
        gitHash = this->m_gitHash;
    }
    this->tlmWrite_LastRequest(lastRequest);
    this->tlmWrite_LastPduStatus(lastPduStatus);
    this->tlmWrite_LastPduOpcode(lastOpcode);
    this->tlmWrite_PduProtocolVersion(protocolVersion);
    this->tlmWrite_PduOutputBitmap(outputBitmap);
    this->tlmWrite_PduFaultBitmap(faultBitmap);
    this->tlmWrite_PduUptimeSeconds(uptimeSeconds);
    this->tlmWrite_TransportFailureCount(transportFailureCount);
    this->tlmWrite_PendingRequestTicks(pendingRequestTicks);
    this->tlmWrite_PduFirmwareMajor(firmwareMajor);
    this->tlmWrite_PduFirmwareMinor(firmwareMinor);
    this->tlmWrite_PduFirmwarePatch(firmwarePatch);
    this->tlmWrite_PduBuildFlags(buildFlags);
    this->tlmWrite_PduGitHash(gitHash);
}

// Called only while m_stateMutex is held, after the full reply was validated.
void EpsDriver_Artemis::observeRails(U16 mask) {
    this->m_railKnownMask |= mask;
    this->m_railFreshMask |= mask;
    for (U8 i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) {
        if ((mask & static_cast<U16>(1U << i)) != 0U) { this->m_railAgeTicks[i] = 0; }
    }
}

void EpsDriver_Artemis::publishRailSnapshot() {
    if (!this->isConnected_railSnapshotOut_OutputPort(0)) { return; }
    U16 bitmap, known, fresh;
    U32 age = 0;
    {
        Os::ScopeLock lock(this->m_stateMutex);
        bitmap = this->m_outputBitmap;
        known = this->m_railKnownMask;
        fresh = this->m_railFreshMask;
        for (U8 i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) {
            if ((known & static_cast<U16>(1U << i)) && this->m_railAgeTicks[i] > age) { age = this->m_railAgeTicks[i]; }
        }
    }
    this->railSnapshotOut_out(0, bitmap, known, fresh, age);
}

void EpsDriver_Artemis::timeoutPendingRequest() {
    Components::EpsRequest request = Components::EpsRequest::GET_SUMMARY_STATUS;
    U8 requestId = 0;
    U8 opcode = 0;
    U8 outputId = 0;
    {
        Os::ScopeLock lock(this->m_stateMutex);
        if (!this->m_requestPending || (this->m_pendingRequestTicks < PDU_REQUEST_TIMEOUT_TICKS)) {
            return;
        }
        request = this->m_pendingRequest;
        requestId = this->m_pendingRequestId;
        opcode = this->m_pendingOpcode;
        outputId = this->m_pendingOutputId;
        this->m_requestPending = false;
        this->m_pendingRequestTicks = 0;
        this->m_transportFailureCount += 1U;
        this->m_lastPduStatus = LinkCfg::TEENSY_STATUS_TIMEOUT;
        this->m_lastOpcode = opcode;
        this->m_railFreshMask = 0;
    }
    this->emitStatus(
        Components::HealthState::UNKNOWN,
        LINK_ERROR,
        LinkCfg::TEENSY_STATUS_TIMEOUT,
        opcode,
        request);
    this->log_WARNING_LO_PduRequestTimedOut(request, requestId);
    this->log_WARNING_LO_PduRequestFailed(request, LinkCfg::TEENSY_STATUS_TIMEOUT);
    this->log_WARNING_HI_PduOperationError(request, railFromId(outputId), Components::PduErrorOrigin::TEENSY_RPC,
        Fw::String("TIMEOUT"), LinkCfg::TEENSY_STATUS_TIMEOUT, requestId);
    this->publishRailSnapshot();
}

U16 EpsDriver_Artemis::crc16Ccitt(const U8* data, U32 length) {
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

U32 EpsDriver_Artemis::readLe32(const U8* data) {
    return static_cast<U32>(data[0]) |
           (static_cast<U32>(data[1]) << 8U) |
           (static_cast<U32>(data[2]) << 16U) |
           (static_cast<U32>(data[3]) << 24U);
}

U8 EpsDriver_Artemis::mapRequestToOpcode(const Components::EpsRequest& request) {
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
