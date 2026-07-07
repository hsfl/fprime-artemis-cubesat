#include "Components/PayloadDownlinkApp/PayloadDownlinkApp.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace Components {

namespace {
constexpr U32 MAX_BLOB_BYTES = 1024U * 1024U;
constexpr U32 PACKETS_PER_RUN = 1;
constexpr U32 RETRY_PACKETS_PER_RUN = 1;
constexpr U32 COMPLETION_SUMMARY_EVENT_REPEATS = 3;
constexpr const char* PAYLOAD_SOURCE_ENV = "NEUTRON_PAYLOAD_DOWNLINK_FILE";
constexpr const char* DEFAULT_PAYLOAD_SOURCE = "/tmp/neutron_payload_captures/latest_payload.bin";
constexpr const char* CAPTURE_DIR = "/tmp/neutron_payload_captures";
constexpr const char* CAPTURE_PREFIX = "neutron_capture_";
constexpr const char* CAPTURE_SUFFIX = ".csv";

bool hasPrefix(const char* value, const char* prefix) {
    return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

bool hasSuffix(const char* value, const char* suffix) {
    const std::size_t valueLength = std::strlen(value);
    const std::size_t suffixLength = std::strlen(suffix);
    if (valueLength < suffixLength) {
        return false;
    }
    return std::strcmp(value + valueLength - suffixLength, suffix) == 0;
}

bool fileSizeBytes(const std::string& path, U32& bytes) {
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) {
        return false;
    }
    if (static_cast<unsigned long long>(info.st_size) > static_cast<unsigned long long>(MAX_BLOB_BYTES)) {
        return false;
    }
    bytes = static_cast<U32>(info.st_size);
    return true;
}

std::string latestCapturePath() {
    DIR* directory = ::opendir(CAPTURE_DIR);
    if (directory == nullptr) {
        return std::string();
    }

    std::string latest;
    while (dirent* entry = ::readdir(directory)) {
        const char* name = entry->d_name;
        if (!hasPrefix(name, CAPTURE_PREFIX) || !hasSuffix(name, CAPTURE_SUFFIX)) {
            continue;
        }
        const std::string candidate = std::string(CAPTURE_DIR) + "/" + name;
        if (candidate > latest) {
            latest = candidate;
        }
    }
    (void)::closedir(directory);
    return latest;
}

std::string resolvePayloadSource(U32& sourceBytes) {
    const char* envPath = std::getenv(PAYLOAD_SOURCE_ENV);
    if ((envPath != nullptr) && (envPath[0] != '\0')) {
        const std::string path(envPath);
        if (fileSizeBytes(path, sourceBytes)) {
            return path;
        }
        return std::string();
    }

    const std::string defaultPath(DEFAULT_PAYLOAD_SOURCE);
    if (fileSizeBytes(defaultPath, sourceBytes)) {
        return defaultPath;
    }

    const std::string latest = latestCapturePath();
    if (!latest.empty() && fileSizeBytes(latest, sourceBytes)) {
        return latest;
    }
    return std::string();
}
}

PayloadDownlinkApp::PayloadDownlinkApp(const char* const compName)
    : PayloadDownlinkAppComponentBase(compName),
      m_state(STATE_IDLE),
      m_transferId(0),
      m_productId(0),
      m_totalBytes(0),
      m_totalPackets(0),
      m_nextPacketIndex(0),
      m_packetsSent(0),
      m_progressPercent(0),
      m_retryRound(0),
      m_packetsMissing(0),
      m_lastError(0),
      m_nextProgressPercent(10),
      m_completionSummaryEventsRemaining(0),
      m_blobCrc(0),
      m_sentHeader(false),
      m_sentEnd(false),
      m_sourceReady(false),
      m_sourceBytes(0),
      m_sourcePath(),
      m_runTicks(0),
      m_lastTelemetryTick(0),
      m_retryPackets{},
      m_retryCount(0),
      m_retryCursor(0) {
    std::memset(this->m_packet, 0, sizeof(this->m_packet));
}

PayloadDownlinkApp::~PayloadDownlinkApp() {}

void PayloadDownlinkApp::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadDownlinkApp::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_runTicks += 1;

    U32 retrySentThisRun = 0;
    while (retrySentThisRun < RETRY_PACKETS_PER_RUN && this->m_retryCursor < this->m_retryCount) {
        if (!this->sendDataPacket(this->m_retryPackets[this->m_retryCursor])) {
            this->failTransfer(6U, this->m_lastError);
            this->emitTelemetry();
            return;
        }
        this->m_retryCursor++;
        retrySentThisRun++;
    }
    if (this->m_retryCursor >= this->m_retryCount) {
        this->m_retryCount = 0;
        this->m_retryCursor = 0;
    }

    if (this->m_state == STATE_DONE) {
        this->emitCompletionSummaryIfDue();
        this->emitTelemetry();
        return;
    }

    if (this->m_state == STATE_DOWNLINKING) {
        if (!this->m_sentHeader) {
            if (!this->sendHeaderPacket()) {
                this->failTransfer(3U, this->m_lastError);
                this->emitTelemetry();
                return;
            }
            this->m_sentHeader = true;
        }

        U32 sentThisRun = 0;
        while (sentThisRun < PACKETS_PER_RUN && this->m_nextPacketIndex < this->m_totalPackets) {
            if (!this->sendDataPacket(this->m_nextPacketIndex)) {
                this->failTransfer(6U, this->m_lastError);
                break;
            }
            this->m_nextPacketIndex++;
            this->emitProgressIfDue();
            sentThisRun++;
        }

        if ((this->m_state == STATE_DOWNLINKING) && this->m_nextPacketIndex >= this->m_totalPackets &&
            !this->m_sentEnd) {
            if (!this->sendEndPacket()) {
                this->failTransfer(3U, this->m_lastError);
                this->emitTelemetry();
                return;
            }
            this->m_sentEnd = true;
            this->m_state = STATE_DONE;
            this->m_progressPercent = 100U;
            this->m_completionSummaryEventsRemaining = COMPLETION_SUMMARY_EVENT_REPEATS;
            this->log_ACTIVITY_HI_PayloadDownlinkComplete(this->m_transferId, this->m_packetsSent);
            this->emitStatus();
        }
    }

    this->emitTelemetry();
}

void PayloadDownlinkApp::packetIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    static_cast<void>(portNum);
    if (!fwBuffer.isValid()) {
        this->m_lastError = 1;
        return;
    }
    this->handleRetryRequest(fwBuffer.getData(), fwBuffer.getSize());
}

void PayloadDownlinkApp::downlinkRequestIn_handler(FwIndexType portNum,
                                                       U32 productId,
                                                       U32 productBytes,
                                                       const Components::ScienceProductSource& sourceKind,
                                                       const Fw::StringBase& sourcePath,
                                                       U32 sourceCrc) {
    static_cast<void>(portNum);
    static_cast<void>(sourceKind);
    if (productBytes == 0U) {
        this->failTransfer(8U, 0U);
        return;
    }
    if (productBytes > MAX_BLOB_BYTES) {
        this->failTransfer(2U, productBytes);
        return;
    }
    if (!this->resetTransfer(productId, productBytes, sourcePath.toChar(), sourceCrc)) {
        return;
    }
    this->log_ACTIVITY_HI_PayloadDownlinkStarted(this->m_productId, this->m_totalBytes, this->m_totalPackets);
    this->emitStatus();
}

void PayloadDownlinkApp::START_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode,
                                                               U32 cmdSeq,
                                                               U32 productId,
                                                               U32 byteCount) {
    U32 normalizedBytes = byteCount;
    if (normalizedBytes == 0) {
        this->failTransfer(8U, 0U);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (normalizedBytes > MAX_BLOB_BYTES) {
        this->failTransfer(2U, normalizedBytes);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }

    if (!this->resetTransfer(productId, normalizedBytes, std::string(), 0U)) {
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->log_ACTIVITY_HI_PayloadDownlinkStarted(this->m_productId, this->m_totalBytes, this->m_totalPackets);
    this->emitStatus();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadDownlinkApp::ABORT_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_state = STATE_ABORTED;
    this->emitStatus();
    this->emitTelemetry(true);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadDownlinkApp::GET_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->emitTelemetry(true);
    this->log_ACTIVITY_HI_PayloadDownlinkProgress(
        this->m_transferId, this->m_progressPercent, this->m_nextPacketIndex, this->m_totalPackets);
    this->log_ACTIVITY_LO_PayloadStatus(this->m_state, this->m_packetsSent, this->m_totalPackets, this->m_lastError);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

bool PayloadDownlinkApp::resetTransfer(U32 productId,
                                           U32 byteCount,
                                           const std::string& preferredSourcePath,
                                           U32 expectedSourceCrc) {
    if (!this->prepareSource(byteCount, preferredSourcePath)) {
        const U32 reason = (this->m_lastError != 0U) ? this->m_lastError : 7U;
        this->failTransfer(reason, byteCount);
        return false;
    }
    U16 sourceCrc = 0;
    if (!this->computeSourceCrc(byteCount, sourceCrc)) {
        this->failTransfer(7U, byteCount);
        return false;
    }
    if ((expectedSourceCrc != 0U) && (static_cast<U16>(expectedSourceCrc) != sourceCrc)) {
        this->failTransfer(11U, expectedSourceCrc);
        return false;
    }

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
    this->m_progressPercent = 0;
    this->m_retryRound = 0;
    this->m_packetsMissing = 0;
    this->m_lastError = 0;
    this->m_nextProgressPercent = 10U;
    this->m_completionSummaryEventsRemaining = 0;
    this->m_blobCrc = sourceCrc;
    this->m_sentHeader = false;
    this->m_sentEnd = false;
    this->m_retryCount = 0;
    this->m_retryCursor = 0;
    return true;
}

bool PayloadDownlinkApp::prepareSource(U32 byteCount, const std::string& preferredSourcePath) {
    U32 sourceBytes = 0;
    std::string sourcePath;
    if (!preferredSourcePath.empty()) {
        if (fileSizeBytes(preferredSourcePath, sourceBytes)) {
            sourcePath = preferredSourcePath;
        }
    }
    if (sourcePath.empty()) {
        sourcePath = resolvePayloadSource(sourceBytes);
    }
    if (sourceBytes != byteCount) {
        U32 fallbackBytes = 0;
        const std::string fallbackPath = resolvePayloadSource(fallbackBytes);
        if (!fallbackPath.empty() && fallbackBytes == byteCount) {
            sourcePath = fallbackPath;
            sourceBytes = fallbackBytes;
        }
    }
    if (sourcePath.empty()) {
        this->m_sourceReady = false;
        this->m_sourceBytes = 0;
        this->m_sourcePath.clear();
        this->m_lastError = 7U;
        return false;
    }
    if (sourceBytes != byteCount) {
        this->m_sourceReady = false;
        this->m_sourceBytes = sourceBytes;
        this->m_sourcePath = sourcePath;
        this->m_lastError = 9U;
        return false;
    }

    this->m_sourceReady = true;
    this->m_sourceBytes = sourceBytes;
    this->m_sourcePath = sourcePath;
    return true;
}

void PayloadDownlinkApp::emitTelemetry(bool force) {
    const U32 telemetryPeriodTicks =
        ((this->m_state == STATE_DOWNLINKING) || (this->m_retryCount > 0U)) ? 5U : 30U;
    if (!force && ((this->m_runTicks - this->m_lastTelemetryTick) < telemetryPeriodTicks)) {
        return;
    }

    this->tlmWrite_PayloadState(this->m_state);
    this->tlmWrite_TransferId(this->m_transferId);
    this->tlmWrite_ProductId(this->m_productId);
    this->tlmWrite_TotalBytes(this->m_totalBytes);
    this->tlmWrite_TotalPackets(this->m_totalPackets);
    this->tlmWrite_PacketsSent(this->m_packetsSent);
    this->writeProgressTelemetry();
    this->tlmWrite_RetryRound(this->m_retryRound);
    this->tlmWrite_PacketsMissing(this->m_packetsMissing);
    this->tlmWrite_LastError(this->m_lastError);
    this->m_lastTelemetryTick = this->m_runTicks;
}

void PayloadDownlinkApp::emitStatus() {
    if (this->isConnected_statusOut_OutputPort(0)) {
        this->statusOut_out(
            0,
            this->m_state,
            this->m_transferId,
            this->m_productId,
            this->m_totalBytes,
            this->m_packetsSent,
            this->m_totalPackets,
            this->m_lastError);
    }
}

void PayloadDownlinkApp::writeProgressTelemetry() {
    this->tlmWrite_ProgressPercent(this->m_progressPercent);
    this->tlmWrite_ProgressPacketsSent(this->m_nextPacketIndex);
    this->tlmWrite_ProgressTotalPackets(this->m_totalPackets);
}

void PayloadDownlinkApp::emitCompletionSummaryIfDue() {
    if (this->m_completionSummaryEventsRemaining == 0U) {
        return;
    }
    this->log_ACTIVITY_HI_PayloadDownlinkProgress(
        this->m_transferId, 100U, this->m_nextPacketIndex, this->m_totalPackets);
    this->m_completionSummaryEventsRemaining--;
}

bool PayloadDownlinkApp::sendHeaderPacket() {
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
    return this->sendPacket(this->m_packet, 17);
}

bool PayloadDownlinkApp::sendDataPacket(U32 packetIndex) {
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
    if (!this->readSourceBytes(offset, &this->m_packet[7], remaining)) {
        this->m_lastError = 7U;
        return false;
    }
    const FwSizeType crcOffset = 7 + remaining;
    const U16 crc = this->crc16Ccitt(this->m_packet, crcOffset);
    this->putU16(this->m_packet, crcOffset, crc);
    if (!this->sendPacket(this->m_packet, crcOffset + 2)) {
        return false;
    }
    this->m_packetsSent++;
    return true;
}

bool PayloadDownlinkApp::sendEndPacket() {
    std::memset(this->m_packet, 0, sizeof(this->m_packet));
    this->m_packet[0] = LinkCfg::PAYLOAD_MAGIC_0;
    this->m_packet[1] = LinkCfg::PAYLOAD_MAGIC_1;
    this->m_packet[2] = PACKET_END;
    this->m_packet[3] = this->m_transferId;
    this->putU16(this->m_packet, 4, static_cast<U16>(this->m_totalPackets));
    this->putU16(this->m_packet, 6, this->m_blobCrc);
    return this->sendPacket(this->m_packet, 8);
}

bool PayloadDownlinkApp::sendPacket(const U8* data, FwSizeType size) {
    if (!this->isConnected_packetOut_OutputPort(0)) {
        this->m_lastError = 3;
        return false;
    }
    if (data == nullptr || size == 0 || size > LinkCfg::PAYLOAD_PACKET_MAX_BYTES) {
        this->m_lastError = 6;
        return false;
    }
    Fw::Buffer packet(const_cast<U8*>(data), size);
    this->packetOut_out(0, packet);
    return true;
}

void PayloadDownlinkApp::emitProgressIfDue() {
    if (this->m_totalPackets == 0U || this->m_nextProgressPercent >= 100U) {
        return;
    }
    U32 bytesSent = this->m_nextPacketIndex * LinkCfg::PAYLOAD_PACKET_DATA_BYTES;
    if (bytesSent > this->m_totalBytes) {
        bytesSent = this->m_totalBytes;
    }
    const U32 percentComplete = (bytesSent * 100U) / this->m_totalBytes;
    if (percentComplete >= this->m_nextProgressPercent) {
        U32 reportedPercent = percentComplete - (percentComplete % 10U);
        if (reportedPercent >= 100U) {
            reportedPercent = 90U;
        }
        if (reportedPercent < this->m_nextProgressPercent) {
            reportedPercent = this->m_nextProgressPercent;
        }
        this->m_progressPercent = reportedPercent;
        this->log_ACTIVITY_HI_PayloadDownlinkProgress(
            this->m_transferId, reportedPercent, this->m_nextPacketIndex, this->m_totalPackets);
        this->m_nextProgressPercent = reportedPercent + 10U;
    }
}

void PayloadDownlinkApp::handleRetryRequest(const U8* data, FwSizeType size) {
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
    this->m_retryCount = 0;
    this->m_retryCursor = 0;
    for (U8 byteIndex = 0; byteIndex < bitmapBytes; byteIndex++) {
        const U8 bits = data[7 + byteIndex];
        for (U8 bit = 0; bit < 8; bit++) {
            if ((bits & (1U << bit)) == 0) {
                continue;
            }
            const U32 packetIndex = static_cast<U32>(startIndex) + static_cast<U32>(byteIndex) * 8U + bit;
            if (packetIndex < this->m_totalPackets) {
                if (this->m_retryCount < MAX_RETRY_PACKETS) {
                    this->m_retryPackets[this->m_retryCount] = packetIndex;
                    this->m_retryCount++;
                    missing++;
                } else {
                    this->m_lastError = 10U;
                    this->failTransfer(6U, this->m_lastError);
                    break;
                }
            }
        }
        if (this->m_state == STATE_ERROR) {
            break;
        }
    }
    this->m_packetsMissing = missing;
    this->log_ACTIVITY_LO_PayloadRetryRequested(startIndex, missing);
    this->emitStatus();
}

bool PayloadDownlinkApp::readSourceBytes(U32 offset, U8* output, U32 length) const {
    if (!this->m_sourceReady || output == nullptr || offset > this->m_sourceBytes ||
        length > (this->m_sourceBytes - offset)) {
        return false;
    }
    if (length == 0U) {
        return true;
    }

    std::FILE* file = std::fopen(this->m_sourcePath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    const bool seekOk = (std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0);
    const std::size_t bytesRead = seekOk ? std::fread(output, 1, static_cast<std::size_t>(length), file) : 0U;
    (void)std::fclose(file);
    return seekOk && (bytesRead == static_cast<std::size_t>(length));
}

bool PayloadDownlinkApp::computeSourceCrc(U32 byteCount, U16& crcOut) const {
    U16 crc = 0xFFFFU;
    std::FILE* file = this->m_sourceReady ? std::fopen(this->m_sourcePath.c_str(), "rb") : nullptr;
    if (file == nullptr) {
        return false;
    }

    for (U32 i = 0; i < byteCount; i++) {
        const int value = std::fgetc(file);
        if (value == EOF) {
            (void)std::fclose(file);
            return false;
        }
        crc ^= static_cast<U16>(static_cast<U8>(value)) << 8U;
        for (U8 bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000U) != 0) {
                crc = static_cast<U16>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<U16>(crc << 1U);
            }
        }
    }
    (void)std::fclose(file);
    crcOut = crc;
    return true;
}

U16 PayloadDownlinkApp::crc16Ccitt(const U8* data, FwSizeType size) const {
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

void PayloadDownlinkApp::putU16(U8* data, FwSizeType offset, U16 value) const {
    data[offset] = static_cast<U8>(value & 0xFFU);
    data[offset + 1] = static_cast<U8>((value >> 8U) & 0xFFU);
}

void PayloadDownlinkApp::putU32(U8* data, FwSizeType offset, U32 value) const {
    data[offset] = static_cast<U8>(value & 0xFFU);
    data[offset + 1] = static_cast<U8>((value >> 8U) & 0xFFU);
    data[offset + 2] = static_cast<U8>((value >> 16U) & 0xFFU);
    data[offset + 3] = static_cast<U8>((value >> 24U) & 0xFFU);
}

U16 PayloadDownlinkApp::getU16(const U8* data, FwSizeType offset) const {
    return static_cast<U16>(data[offset]) | (static_cast<U16>(data[offset + 1]) << 8U);
}

void PayloadDownlinkApp::failTransfer(U32 reason, U32 detail) {
    this->m_lastError = reason;
    this->m_state = STATE_ERROR;
    this->log_WARNING_LO_PayloadDownlinkFailed(reason, detail);
    this->emitStatus();
    this->emitTelemetry(true);
}

}  // namespace Components
