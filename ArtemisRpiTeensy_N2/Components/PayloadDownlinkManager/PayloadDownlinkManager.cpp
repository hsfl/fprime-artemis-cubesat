#include "Components/PayloadDownlinkManager/PayloadDownlinkManager.hpp"

#include <cstring>

namespace Components {

namespace {
constexpr U32 DEFAULT_BLOB_BYTES = 40368;
constexpr U32 MAX_BLOB_BYTES = 1024U * 1024U;
constexpr U32 PACKETS_PER_RUN = 4;
}

PayloadDownlinkManager::PayloadDownlinkManager(const char* const compName)
    : PayloadDownlinkManagerComponentBase(compName),
      m_state(STATE_IDLE),
      m_transferId(0),
      m_productId(0),
      m_totalBytes(0),
      m_totalPackets(0),
      m_nextPacketIndex(0),
      m_packetsSent(0),
      m_retryRound(0),
      m_packetsMissing(0),
      m_lastError(0),
      m_blobCrc(0),
      m_sentHeader(false),
      m_sentEnd(false) {
    std::memset(this->m_packet, 0, sizeof(this->m_packet));
}

PayloadDownlinkManager::~PayloadDownlinkManager() {}

void PayloadDownlinkManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadDownlinkManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);

    if (this->m_state == STATE_DOWNLINKING) {
        if (!this->m_sentHeader) {
            this->sendHeaderPacket();
            this->m_sentHeader = true;
        }

        U32 sentThisRun = 0;
        while (sentThisRun < PACKETS_PER_RUN && this->m_nextPacketIndex < this->m_totalPackets) {
            this->sendDataPacket(this->m_nextPacketIndex);
            this->m_nextPacketIndex++;
            sentThisRun++;
        }

        if (this->m_nextPacketIndex >= this->m_totalPackets && !this->m_sentEnd) {
            this->sendEndPacket();
            this->m_sentEnd = true;
            this->m_state = STATE_DONE;
            this->log_ACTIVITY_HI_PayloadDownlinkComplete(this->m_transferId, this->m_packetsSent);
        }
    }

    this->emitTelemetry();
}

void PayloadDownlinkManager::packetIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    if (!fwBuffer.isValid()) {
        this->m_lastError = 1;
        return;
    }
    this->handleRetryRequest(fwBuffer.getData(), fwBuffer.getSize());
}

void PayloadDownlinkManager::downlinkRequestIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    const U32 byteCount = (key == 0U) ? DEFAULT_BLOB_BYTES : key;
    const U32 productId = static_cast<U32>(this->m_transferId) + 1U;
    if (byteCount > MAX_BLOB_BYTES) {
        this->m_lastError = 2;
        this->m_state = STATE_ERROR;
        return;
    }
    this->resetTransfer(productId, byteCount);
    this->log_ACTIVITY_HI_PayloadDownlinkStarted(this->m_productId, this->m_totalBytes, this->m_totalPackets);
}

void PayloadDownlinkManager::START_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode,
                                                               U32 cmdSeq,
                                                               U32 productId,
                                                               U32 byteCount) {
    U32 normalizedBytes = byteCount;
    if (normalizedBytes == 0) {
        normalizedBytes = DEFAULT_BLOB_BYTES;
    }
    if (normalizedBytes > MAX_BLOB_BYTES) {
        this->m_lastError = 2;
        this->m_state = STATE_ERROR;
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }

    this->resetTransfer(productId, normalizedBytes);
    this->log_ACTIVITY_HI_PayloadDownlinkStarted(this->m_productId, this->m_totalBytes, this->m_totalPackets);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadDownlinkManager::ABORT_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_state = STATE_ABORTED;
    this->emitTelemetry();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadDownlinkManager::GET_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->emitTelemetry();
    this->log_ACTIVITY_LO_PayloadStatus(this->m_state, this->m_packetsSent, this->m_totalPackets, this->m_lastError);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadDownlinkManager::resetTransfer(U32 productId, U32 byteCount) {
    this->m_state = STATE_DOWNLINKING;
    this->m_transferId++;
    if (this->m_transferId == 0) {
        this->m_transferId = 1;
    }
    this->m_productId = productId;
    this->m_totalBytes = byteCount;
    this->m_totalPackets =
        (byteCount + LinkCfg::PAYLOAD_PACKET_DATA_BYTES - 1U) / LinkCfg::PAYLOAD_PACKET_DATA_BYTES;
    this->m_nextPacketIndex = 0;
    this->m_packetsSent = 0;
    this->m_retryRound = 0;
    this->m_packetsMissing = 0;
    this->m_lastError = 0;
    this->m_blobCrc = this->blobCrc(byteCount);
    this->m_sentHeader = false;
    this->m_sentEnd = false;
}

void PayloadDownlinkManager::emitTelemetry() {
    this->tlmWrite_PayloadState(this->m_state);
    this->tlmWrite_TransferId(this->m_transferId);
    this->tlmWrite_ProductId(this->m_productId);
    this->tlmWrite_TotalBytes(this->m_totalBytes);
    this->tlmWrite_TotalPackets(this->m_totalPackets);
    this->tlmWrite_PacketsSent(this->m_packetsSent);
    this->tlmWrite_RetryRound(this->m_retryRound);
    this->tlmWrite_PacketsMissing(this->m_packetsMissing);
    this->tlmWrite_LastError(this->m_lastError);
}

void PayloadDownlinkManager::sendHeaderPacket() {
    std::memset(this->m_packet, 0, sizeof(this->m_packet));
    this->m_packet[0] = LinkCfg::PAYLOAD_MAGIC_0;
    this->m_packet[1] = LinkCfg::PAYLOAD_MAGIC_1;
    this->m_packet[2] = PACKET_HEADER;
    this->m_packet[3] = this->m_transferId;
    this->putU32(this->m_packet, 4, this->m_productId);
    this->putU32(this->m_packet, 8, this->m_totalBytes);
    this->putU16(this->m_packet, 12, static_cast<U16>(this->m_totalPackets));
    this->m_packet[14] = static_cast<U8>(LinkCfg::PAYLOAD_PACKET_DATA_BYTES);
    this->putU16(this->m_packet, 15, this->m_blobCrc);
    this->sendPacket(this->m_packet, 17);
}

void PayloadDownlinkManager::sendDataPacket(U32 packetIndex) {
    const U32 offset = packetIndex * LinkCfg::PAYLOAD_PACKET_DATA_BYTES;
    U32 remaining = (offset < this->m_totalBytes) ? (this->m_totalBytes - offset) : 0;
    if (remaining > LinkCfg::PAYLOAD_PACKET_DATA_BYTES) {
        remaining = LinkCfg::PAYLOAD_PACKET_DATA_BYTES;
    }

    std::memset(this->m_packet, 0, sizeof(this->m_packet));
    this->m_packet[0] = LinkCfg::PAYLOAD_MAGIC_0;
    this->m_packet[1] = LinkCfg::PAYLOAD_MAGIC_1;
    this->m_packet[2] = PACKET_DATA;
    this->m_packet[3] = this->m_transferId;
    this->putU16(this->m_packet, 4, static_cast<U16>(packetIndex));
    this->m_packet[6] = static_cast<U8>(remaining);
    for (U32 i = 0; i < remaining; i++) {
        this->m_packet[7 + i] = this->blobByteAt(offset + i);
    }
    const FwSizeType crcOffset = 7 + remaining;
    const U16 crc = this->crc16Ccitt(this->m_packet, crcOffset);
    this->putU16(this->m_packet, crcOffset, crc);
    this->sendPacket(this->m_packet, crcOffset + 2);
    this->m_packetsSent++;
}

void PayloadDownlinkManager::sendEndPacket() {
    std::memset(this->m_packet, 0, sizeof(this->m_packet));
    this->m_packet[0] = LinkCfg::PAYLOAD_MAGIC_0;
    this->m_packet[1] = LinkCfg::PAYLOAD_MAGIC_1;
    this->m_packet[2] = PACKET_END;
    this->m_packet[3] = this->m_transferId;
    this->putU16(this->m_packet, 4, static_cast<U16>(this->m_totalPackets));
    this->putU16(this->m_packet, 6, this->m_blobCrc);
    this->sendPacket(this->m_packet, 8);
}

void PayloadDownlinkManager::sendPacket(const U8* data, FwSizeType size) {
    if (!this->isConnected_packetOut_OutputPort(0)) {
        this->m_lastError = 3;
        return;
    }
    if (data == nullptr || size == 0 || size > LinkCfg::PAYLOAD_PACKET_MAX_BYTES) {
        this->m_lastError = 6;
        return;
    }
    Fw::Buffer packet(const_cast<U8*>(data), LinkCfg::PAYLOAD_PACKET_MAX_BYTES);
    this->packetOut_out(0, packet);
}

void PayloadDownlinkManager::handleRetryRequest(const U8* data, FwSizeType size) {
    if (size < 7 || data[0] != LinkCfg::PAYLOAD_MAGIC_0 || data[1] != LinkCfg::PAYLOAD_MAGIC_1 ||
        data[2] != PACKET_RETRY_REQUEST || data[3] != this->m_transferId) {
        this->m_lastError = 4;
        return;
    }

    const U16 startIndex = this->getU16(data, 4);
    const U8 bitmapBytes = data[6];
    if (static_cast<FwSizeType>(7 + bitmapBytes) > size) {
        this->m_lastError = 5;
        return;
    }

    U32 missing = 0;
    this->m_retryRound++;
    for (U8 byteIndex = 0; byteIndex < bitmapBytes; byteIndex++) {
        const U8 bits = data[7 + byteIndex];
        for (U8 bit = 0; bit < 8; bit++) {
            if ((bits & (1U << bit)) == 0) {
                continue;
            }
            const U32 packetIndex = static_cast<U32>(startIndex) + static_cast<U32>(byteIndex) * 8U + bit;
            if (packetIndex < this->m_totalPackets) {
                this->sendDataPacket(packetIndex);
                missing++;
            }
        }
    }
    this->m_packetsMissing = missing;
    this->log_ACTIVITY_LO_PayloadRetryRequested(startIndex, missing);
}

U8 PayloadDownlinkManager::blobByteAt(U32 offset) const {
    return static_cast<U8>((this->m_productId + (offset * 31U) + (offset >> 8U)) & 0xFFU);
}

U16 PayloadDownlinkManager::blobCrc(U32 byteCount) const {
    U16 crc = 0xFFFFU;
    for (U32 i = 0; i < byteCount; i++) {
        crc ^= static_cast<U16>(this->blobByteAt(i)) << 8U;
        for (U8 bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    return crc;
}

U16 PayloadDownlinkManager::crc16Ccitt(const U8* data, FwSizeType size) const {
    U16 crc = 0xFFFFU;
    for (FwSizeType i = 0; i < size; i++) {
        crc ^= static_cast<U16>(data[i]) << 8U;
        for (U8 bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    return crc;
}

void PayloadDownlinkManager::putU16(U8* data, FwSizeType offset, U16 value) const {
    data[offset] = static_cast<U8>(value & 0xFFU);
    data[offset + 1] = static_cast<U8>((value >> 8U) & 0xFFU);
}

void PayloadDownlinkManager::putU32(U8* data, FwSizeType offset, U32 value) const {
    data[offset] = static_cast<U8>(value & 0xFFU);
    data[offset + 1] = static_cast<U8>((value >> 8U) & 0xFFU);
    data[offset + 2] = static_cast<U8>((value >> 16U) & 0xFFU);
    data[offset + 3] = static_cast<U8>((value >> 24U) & 0xFFU);
}

U16 PayloadDownlinkManager::getU16(const U8* data, FwSizeType offset) const {
    return static_cast<U16>(data[offset]) | (static_cast<U16>(data[offset + 1]) << 8U);
}

}  // namespace Components
