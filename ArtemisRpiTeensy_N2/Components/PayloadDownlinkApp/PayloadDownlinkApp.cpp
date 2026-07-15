#include "Components/PayloadDownlinkApp/PayloadDownlinkApp.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace Components {

namespace {
constexpr U32 MAX_BLOB_BYTES = 1024U * 1024U;
constexpr U32 HEADER_RETRANSMIT_COUNT = 3U;
constexpr U32 HEADER_REFRESH_COUNT = 1U;
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
      m_requestDisposition(REQUEST_ACCEPTED),
      m_nextProgressPercent(10),
      m_completionSummaryEventsRemaining(0),
      m_blobCrc(0),
      m_sentHeader(false),
      m_sentEnd(false),
      m_sourceReady(false),
      m_sourceBytes(0),
      m_sourcePath(),
      m_requestedSourceKind(Components::ScienceProductSource::UNKNOWN),
      m_requestedSourcePath(),
      m_expectedSourceCrc(0),
      m_runTicks(0),
      m_lastTelemetryTick(0),
      m_retryPackets{},
      m_retryCount(0),
      m_retryCursor(0),
      m_controlMailboxMutex(),
      m_controlMailbox{},
      m_controlMailboxHead(0),
      m_controlMailboxTail(0),
      m_controlMailboxCount(0),
      m_controlMailboxDrops(0),
      m_controlPacketsInvalid(0),
      m_controlMailboxHighWater(0),
      m_reportedControlMailboxDrops(0),
      m_reportedControlPacketsInvalid(0) {
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
    this->drainControlMailbox();

    const U32 packetsPerRun = LinkCfg::PAYLOAD_PACKETS_PER_RUN;
    const U32 retryPacketsPerRun = LinkCfg::PAYLOAD_RETRY_PACKETS_PER_RUN;
    const U32 totalPacketsPerRun = (packetsPerRun > retryPacketsPerRun) ? packetsPerRun : retryPacketsPerRun;
    const U32 retryBudget =
        (this->m_state == STATE_DOWNLINKING && retryPacketsPerRun >= totalPacketsPerRun)
            ? (totalPacketsPerRun / 2U)
            : retryPacketsPerRun;
    U32 packetsSentThisRun = 0;
    U32 retrySentThisRun = 0;
    while (packetsSentThisRun < totalPacketsPerRun &&
           retrySentThisRun < retryBudget &&
           this->m_retryCursor < this->m_retryCount) {
        const Components::PayloadSendStatus status =
            this->sendDataPacket(this->m_retryPackets[this->m_retryCursor]);
        if (status == Components::PayloadSendStatus::LOCAL_RETRY) {
            this->emitTelemetry();
            return;
        }
        if (status != Components::PayloadSendStatus::LOCAL_ACCEPTED) {
            this->failTransfer(6U, this->m_lastError);
            this->emitTelemetry();
            return;
        }
        this->m_retryCursor++;
        this->m_packetsMissing = this->m_retryCount - this->m_retryCursor;
        retrySentThisRun++;
        packetsSentThisRun++;
    }
    if (this->m_retryCursor >= this->m_retryCount) {
        this->m_retryCount = 0;
        this->m_retryCursor = 0;
        this->m_packetsMissing = 0;
    }

    if (this->m_state == STATE_DONE) {
        this->emitCompletionSummaryIfDue();
        this->emitTelemetry();
        return;
    }

    if (this->m_state == STATE_DOWNLINKING) {
        {
            const U32 requestedHeaders = this->m_sentHeader ? HEADER_REFRESH_COUNT : HEADER_RETRANSMIT_COUNT;
            const U32 availableSlots = totalPacketsPerRun - packetsSentThisRun;
            const U32 headerPacketsToSend =
                (requestedHeaders < availableSlots) ? requestedHeaders : availableSlots;
            for (U32 headerCount = 0; headerCount < headerPacketsToSend; ++headerCount) {
                const Components::PayloadSendStatus status = this->sendHeaderPacket();
                if (status == Components::PayloadSendStatus::LOCAL_RETRY) {
                    this->emitTelemetry();
                    return;
                }
                if (status != Components::PayloadSendStatus::LOCAL_ACCEPTED) {
                    this->failTransfer(3U, this->m_lastError);
                    this->emitTelemetry();
                    return;
                }
                packetsSentThisRun++;
            }
            if (headerPacketsToSend > 0U) {
                this->m_sentHeader = true;
            }
        }

        U32 dataSentThisRun = 0;
        while (packetsSentThisRun < totalPacketsPerRun &&
               dataSentThisRun < packetsPerRun &&
               this->m_nextPacketIndex < this->m_totalPackets) {
            const Components::PayloadSendStatus status = this->sendDataPacket(this->m_nextPacketIndex);
            if (status == Components::PayloadSendStatus::LOCAL_RETRY) {
                this->emitTelemetry();
                return;
            }
            if (status != Components::PayloadSendStatus::LOCAL_ACCEPTED) {
                this->failTransfer(6U, this->m_lastError);
                break;
            }
            this->m_nextPacketIndex++;
            this->emitProgressIfDue();
            dataSentThisRun++;
            packetsSentThisRun++;
        }

        if ((this->m_state == STATE_DOWNLINKING) && this->m_nextPacketIndex >= this->m_totalPackets &&
            !this->m_sentEnd && packetsSentThisRun < totalPacketsPerRun) {
            const Components::PayloadSendStatus status = this->sendEndPacket();
            if (status == Components::PayloadSendStatus::LOCAL_RETRY) {
                this->emitTelemetry();
                return;
            }
            if (status != Components::PayloadSendStatus::LOCAL_ACCEPTED) {
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
    const FwSizeType size = fwBuffer.getSize();
    if (!fwBuffer.isValid() || (size == 0U) || (size > LinkCfg::PAYLOAD_PACKET_MAX_BYTES)) {
        Os::ScopeLock lock(this->m_controlMailboxMutex);
        this->m_controlPacketsInvalid++;
        return;
    }

    Os::ScopeLock lock(this->m_controlMailboxMutex);
    if (this->m_controlMailboxCount >= CONTROL_MAILBOX_CAPACITY) {
        this->m_controlMailboxDrops++;
        return;
    }
    ControlPacket& slot = this->m_controlMailbox[this->m_controlMailboxTail];
    slot.size = size;
    std::memcpy(slot.data, fwBuffer.getData(), size);
    this->m_controlMailboxTail = (this->m_controlMailboxTail + 1U) % CONTROL_MAILBOX_CAPACITY;
    this->m_controlMailboxCount++;
    if (this->m_controlMailboxCount > this->m_controlMailboxHighWater) {
        this->m_controlMailboxHighWater = this->m_controlMailboxCount;
    }
}

void PayloadDownlinkApp::downlinkRequestIn_handler(FwIndexType portNum,
                                                       U32 productId,
                                                       U32 productBytes,
                                                       const Components::ScienceProductSource& sourceKind,
                                                       const Fw::StringBase& sourcePath,
                                                       U32 sourceCrc) {
    static_cast<void>(portNum);
    const std::string requestedSourcePath(sourcePath.toChar());
    if (this->m_state == STATE_DOWNLINKING) {
        if (this->activeRequestMatches(productId, productBytes, sourceKind, requestedSourcePath, sourceCrc)) {
            this->reportDuplicateRequest();
        } else {
            this->reportConflictingRequest(productId);
        }
        return;
    }
    if (!this->resetTransfer(productId, productBytes, sourceKind, requestedSourcePath, sourceCrc)) {
        return;
    }
    this->log_ACTIVITY_HI_PayloadDownlinkStarted(this->m_productId, this->m_totalBytes, this->m_totalPackets);
    this->emitTelemetry(true);
}

void PayloadDownlinkApp::START_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode,
                                                               U32 cmdSeq,
                                                               U32 productId,
                                                               U32 byteCount) {
    U32 normalizedBytes = byteCount;
    const Components::ScienceProductSource sourceKind = Components::ScienceProductSource::UNKNOWN;
    const std::string requestedSourcePath;
    if (this->m_state == STATE_DOWNLINKING) {
        if (this->activeRequestMatches(productId, normalizedBytes, sourceKind, requestedSourcePath, 0U)) {
            this->reportDuplicateRequest();
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
        } else {
            this->reportConflictingRequest(productId);
            this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
        }
        return;
    }
    if (!this->resetTransfer(productId, normalizedBytes, sourceKind, requestedSourcePath, 0U)) {
        const bool invalidSize = (normalizedBytes == 0U) || (normalizedBytes > MAX_BLOB_BYTES);
        this->cmdResponse_out(
            opCode,
            cmdSeq,
            invalidSize ? Fw::CmdResponse::VALIDATION_ERROR : Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    this->log_ACTIVITY_HI_PayloadDownlinkStarted(this->m_productId, this->m_totalBytes, this->m_totalPackets);
    this->emitTelemetry(true);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadDownlinkApp::ABORT_PAYLOAD_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->m_state = STATE_ABORTED;
    this->clearRepairWork();
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

bool PayloadDownlinkApp::activeRequestMatches(U32 productId,
                                              U32 byteCount,
                                              const Components::ScienceProductSource& sourceKind,
                                              const std::string& requestedSourcePath,
                                              U32 expectedSourceCrc) const {
    return (productId == this->m_productId) && (byteCount == this->m_totalBytes) &&
           (sourceKind == this->m_requestedSourceKind) &&
           (requestedSourcePath == this->m_requestedSourcePath) &&
           (expectedSourceCrc == this->m_expectedSourceCrc);
}

void PayloadDownlinkApp::reportDuplicateRequest() {
    this->m_requestDisposition = REQUEST_DUPLICATE;
    this->log_ACTIVITY_LO_PayloadDownlinkRequestDuplicate(
        this->m_transferId, this->m_productId, this->m_nextPacketIndex);
    this->emitStatus();
    this->emitTelemetry(true);
}

void PayloadDownlinkApp::reportConflictingRequest(U32 requestedProductId) {
    this->m_requestDisposition = REQUEST_CONFLICT;
    this->log_WARNING_LO_PayloadDownlinkRequestConflict(
        this->m_transferId, this->m_productId, requestedProductId);
    this->emitStatus();
    this->emitTelemetry(true);
}

void PayloadDownlinkApp::drainControlMailbox() {
    ControlPacket pending[CONTROL_MAILBOX_CAPACITY] = {};
    U32 pendingCount = 0U;
    U32 dropped = 0U;
    U32 invalid = 0U;
    U32 highWater = 0U;
    {
        Os::ScopeLock lock(this->m_controlMailboxMutex);
        pendingCount = this->m_controlMailboxCount;
        for (U32 i = 0U; i < pendingCount; ++i) {
            pending[i] = this->m_controlMailbox[this->m_controlMailboxHead];
            this->m_controlMailboxHead = (this->m_controlMailboxHead + 1U) % CONTROL_MAILBOX_CAPACITY;
        }
        this->m_controlMailboxCount = 0U;
        dropped = this->m_controlMailboxDrops;
        invalid = this->m_controlPacketsInvalid;
        highWater = this->m_controlMailboxHighWater;
    }

    if (dropped != this->m_reportedControlMailboxDrops) {
        this->log_WARNING_HI_PayloadControlPacketRejected(2U, dropped);
        this->m_reportedControlMailboxDrops = dropped;
    }
    if (invalid != this->m_reportedControlPacketsInvalid) {
        this->log_WARNING_HI_PayloadControlPacketRejected(1U, invalid);
        this->m_reportedControlPacketsInvalid = invalid;
    }
    this->tlmWrite_ControlMailboxDrops(dropped);
    this->tlmWrite_ControlPacketsInvalid(invalid);
    this->tlmWrite_ControlMailboxHighWater(highWater);
    for (U32 i = 0U; i < pendingCount; ++i) {
        this->handleRetryRequest(pending[i].data, pending[i].size);
        if ((this->m_state == STATE_ABORTED) || (this->m_state == STATE_ERROR)) {
            break;
        }
    }
}

void PayloadDownlinkApp::clearRepairWork() {
    this->m_retryCount = 0U;
    this->m_retryCursor = 0U;
    this->m_packetsMissing = 0U;

    Os::ScopeLock lock(this->m_controlMailboxMutex);
    this->m_controlMailboxHead = 0U;
    this->m_controlMailboxTail = 0U;
    this->m_controlMailboxCount = 0U;
}

void PayloadDownlinkApp::rejectControlPacket(U32 reason) {
    U32 rejectedTotal = 0U;
    {
        Os::ScopeLock lock(this->m_controlMailboxMutex);
        this->m_controlPacketsInvalid++;
        rejectedTotal = this->m_controlPacketsInvalid;
    }
    this->m_reportedControlPacketsInvalid = rejectedTotal;
    this->log_WARNING_HI_PayloadControlPacketRejected(reason, rejectedTotal);
    this->tlmWrite_ControlPacketsInvalid(rejectedTotal);
}

bool PayloadDownlinkApp::resetTransfer(U32 productId,
                                       U32 byteCount,
                                       const Components::ScienceProductSource& sourceKind,
                                       const std::string& preferredSourcePath,
                                       U32 expectedSourceCrc) {
    // Reserve the transfer identity and publish an active status before any
    // fallible source validation. Comms can then correlate a following ERROR
    // status instead of remaining wedged on an unlatchable terminal-first
    // report.
    this->m_state = STATE_DOWNLINKING;
    this->m_transferId++;
    if (this->m_transferId == 0U) {
        this->m_transferId = 1U;
    }
    this->m_productId = productId;
    this->m_totalBytes = byteCount;
    this->m_totalPackets = (byteCount / LinkCfg::PAYLOAD_PACKET_DATA_BYTES) +
                           ((byteCount % LinkCfg::PAYLOAD_PACKET_DATA_BYTES) != 0U ? 1U : 0U);
    this->m_nextPacketIndex = 0U;
    this->m_packetsSent = 0U;
    this->m_progressPercent = 0U;
    this->m_retryRound = 0U;
    this->m_packetsMissing = 0U;
    this->m_lastError = 0U;
    this->m_requestDisposition = REQUEST_ACCEPTED;
    this->m_nextProgressPercent = 10U;
    this->m_completionSummaryEventsRemaining = 0U;
    this->m_blobCrc = 0U;
    this->m_sentHeader = false;
    this->m_sentEnd = false;
    this->m_sourceReady = false;
    this->m_sourceBytes = 0U;
    this->m_sourcePath.clear();
    this->m_requestedSourceKind = sourceKind;
    this->m_requestedSourcePath = preferredSourcePath;
    this->m_expectedSourceCrc = expectedSourceCrc;
    this->clearRepairWork();
    this->emitStatus();

    if (byteCount == 0U) {
        this->failTransfer(8U, 0U);
        return false;
    }
    if (byteCount > MAX_BLOB_BYTES) {
        this->failTransfer(2U, byteCount);
        return false;
    }
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

    this->m_blobCrc = sourceCrc;
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
    this->tlmWrite_RequestDisposition(this->m_requestDisposition);
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

Components::PayloadSendStatus PayloadDownlinkApp::sendHeaderPacket() {
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

Components::PayloadSendStatus PayloadDownlinkApp::sendDataPacket(U32 packetIndex) {
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
        return Components::PayloadSendStatus::LOCAL_ERROR;
    }
    const FwSizeType crcOffset = 7 + remaining;
    const U16 crc = this->crc16Ccitt(this->m_packet, crcOffset);
    this->putU16(this->m_packet, crcOffset, crc);
    const Components::PayloadSendStatus status = this->sendPacket(this->m_packet, crcOffset + 2);
    if (status != Components::PayloadSendStatus::LOCAL_ACCEPTED) {
        return status;
    }
    this->m_packetsSent++;
    return Components::PayloadSendStatus::LOCAL_ACCEPTED;
}

Components::PayloadSendStatus PayloadDownlinkApp::sendEndPacket() {
    std::memset(this->m_packet, 0, sizeof(this->m_packet));
    this->m_packet[0] = LinkCfg::PAYLOAD_MAGIC_0;
    this->m_packet[1] = LinkCfg::PAYLOAD_MAGIC_1;
    this->m_packet[2] = PACKET_END;
    this->m_packet[3] = this->m_transferId;
    this->putU16(this->m_packet, 4, static_cast<U16>(this->m_totalPackets));
    this->putU16(this->m_packet, 6, this->m_blobCrc);
    return this->sendPacket(this->m_packet, 8);
}

Components::PayloadSendStatus PayloadDownlinkApp::sendPacket(const U8* data, FwSizeType size) {
    if (!this->isConnected_packetOut_OutputPort(0)) {
        this->m_lastError = 3;
        return Components::PayloadSendStatus::LOCAL_ERROR;
    }
    if (data == nullptr || size == 0 || size > LinkCfg::PAYLOAD_PACKET_MAX_BYTES) {
        this->m_lastError = 6;
        return Components::PayloadSendStatus::LOCAL_ERROR;
    }
    Fw::Buffer packet(const_cast<U8*>(data), size);
    const Components::PayloadSendStatus status = this->packetOut_out(0, packet);
    if (status == Components::PayloadSendStatus::LOCAL_RETRY) {
        this->m_lastError = 12U;
    } else if (status == Components::PayloadSendStatus::LOCAL_ERROR) {
        this->m_lastError = 13U;
    } else if (this->m_lastError == 12U) {
        this->m_lastError = 0U;
    }
    return status;
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
    if ((this->m_state != STATE_DOWNLINKING) && (this->m_state != STATE_DONE)) {
        this->rejectControlPacket(3U);
        return;
    }
    if (size < 7 || data[0] != LinkCfg::PAYLOAD_MAGIC_0 || data[1] != LinkCfg::PAYLOAD_MAGIC_1 ||
        data[2] != PACKET_RETRY_REQUEST || data[3] != this->m_transferId) {
        this->rejectControlPacket(4U);
        return;
    }

    const U16 startIndex = this->getU16(data, 4);
    const U8 bitmapBytes = data[6];
    if (static_cast<FwSizeType>(7 + bitmapBytes) > size) {
        this->rejectControlPacket(5U);
        return;
    }

    U32 requestedMissing = 0U;
    this->m_retryRound++;

    // Retain only work that has not already been sent, then add newly requested
    // packet indices. This makes separate and overlapping ground requests
    // additive and idempotent without replaying duplicate entries.
    U32 pendingCount = 0U;
    for (U32 i = this->m_retryCursor; i < this->m_retryCount; ++i) {
        this->m_retryPackets[pendingCount++] = this->m_retryPackets[i];
    }
    this->m_retryCount = pendingCount;
    this->m_retryCursor = 0;
    for (U8 byteIndex = 0; byteIndex < bitmapBytes; byteIndex++) {
        const U8 bits = data[7 + byteIndex];
        for (U8 bit = 0; bit < 8; bit++) {
            if ((bits & (1U << bit)) == 0) {
                continue;
            }
            const U32 packetIndex = static_cast<U32>(startIndex) + static_cast<U32>(byteIndex) * 8U + bit;
            if (packetIndex < this->m_totalPackets) {
                requestedMissing++;
                bool alreadyPending = false;
                for (U32 retryIndex = 0U; retryIndex < this->m_retryCount; ++retryIndex) {
                    if (this->m_retryPackets[retryIndex] == packetIndex) {
                        alreadyPending = true;
                        break;
                    }
                }
                if (alreadyPending) {
                    continue;
                }
                if (this->m_retryCount >= MAX_RETRY_PACKETS) {
                    this->m_lastError = 10U;
                    this->failTransfer(6U, this->m_lastError);
                    break;
                }
                this->m_retryPackets[this->m_retryCount] = packetIndex;
                this->m_retryCount++;
            }
        }
        if (this->m_state == STATE_ERROR) {
            break;
        }
    }
    if (this->m_state == STATE_ERROR) {
        return;
    }
    this->m_packetsMissing = this->m_retryCount;
    this->log_ACTIVITY_LO_PayloadRetryRequested(startIndex, requestedMissing);
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
    this->clearRepairWork();
    this->log_WARNING_LO_PayloadDownlinkFailed(reason, detail);
    this->emitStatus();
    this->emitTelemetry(true);
}

}  // namespace Components
