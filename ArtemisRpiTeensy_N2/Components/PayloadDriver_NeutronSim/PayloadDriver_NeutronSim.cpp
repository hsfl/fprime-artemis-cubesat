#include "Components/PayloadDriver_NeutronSim/PayloadDriver_NeutronSim.hpp"
#include "Components/Types/FppConstantsAc.hpp"
#include "Components/ProductId/ProductId.hpp"

#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace Components {

namespace {

constexpr const char* DEFAULT_SIM_ROOT = "../external/payload-neutron-simulation";
constexpr const char* REPO_ROOT_SIM_ROOT = "external/payload-neutron-simulation";
constexpr const char* DEFAULT_CURSOR = "/tmp/neutron_payload_sim_cursor.json";
constexpr const char* DEFAULT_OUTPUT_DIR = "/tmp/neutron_payload_captures";
constexpr const char* DEFAULT_LATEST_PAYLOAD = "/tmp/neutron_payload_captures/latest_payload.bin";
constexpr U32 SECONDS_PER_MINUTE = 60U;
constexpr U32 HANG_CHILD_SECONDS = 120U;
constexpr U32 MAX_CAPTURE_OUTPUT = 65536U;
constexpr U32 KILL_GRACE_MS = 200U;

}  // namespace

PayloadDriver_NeutronSim::PayloadDriver_NeutronSim(const char* const compName)
    : PayloadDriver_NeutronSimComponentBase(compName),
      m_lastDurationMinutes(0),
      m_lastRowsCaptured(0),
      m_lastTotalCounts(0),
      m_lastSaaRows(0),
      m_lastProductBytes(0),
      m_lastExitStatus(0),
      m_lastProductId(0),
      m_lastProductCrc(0),
      m_captureSlotAvailable(true),
      m_availabilityAnnounced(false),
      m_announcedAvailable(false),
      m_captureOutputFd(-1),
      m_captureKillStage(0),
      m_captureProcessStatus(0),
      m_capturePid(0),
      m_captureRequestId(0),
      m_captureGeneration(0),
      m_captureProductId(0U),
      m_captureDurationMinutes(0),
      m_captureTimeoutMs(0),
      m_captureDelayMs(0),
      m_captureFaultMode(Components::PayloadSimulationFault::NORMAL),
      m_captureKind(Components::PayloadOperationKind::CAPTURE),
      m_captureSummary{},
      m_captureOutputBuffer(),
      m_captureOutputTotalBytes(0U),
      m_captureDeadline(),
      m_killDeadline() {}

PayloadDriver_NeutronSim::~PayloadDriver_NeutronSim() {
    if (this->m_capturePid > 0) {
        (void)::kill(this->m_capturePid, SIGKILL);
        int status = 0;
        while (::waitpid(this->m_capturePid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    if (this->m_captureOutputFd >= 0) {
        (void)::close(this->m_captureOutputFd);
        this->m_captureOutputFd = -1;
    }
}

void PayloadDriver_NeutronSim::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadDriver_NeutronSim::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    if (this->m_capturePid > 0) {
        this->pollCapture();
    }
    const auto now = std::chrono::steady_clock::now();
    for (U32 index = 0U; index < PENDING_REPLY_CAPACITY; ++index) {
        PendingReply& pending = this->m_pendingReplies[index];
        if (pending.valid && (now >= pending.due)) {
            const PendingReply reply = pending;
            pending.valid = false;
            this->emitOperationResult(reply.requestId, reply.generation, reply.kind, reply.state,
                                      reply.literalEcho, reply.productId, reply.productBytes,
                                      reply.sourceKind, reply.sourcePath, reply.sourceCrc,
                                      reply.errorCode, reply.duplicate);
        }
    }
    this->setAvailability(this->m_captureSlotAvailable);
    this->tlmWrite_CaptureSlotAvailable(this->m_captureSlotAvailable);
}

void PayloadDriver_NeutronSim::simulationCaptureOptionsIn_handler(
    FwIndexType portNum,
    U32 generation,
    const Components::PayloadSimulationFault& faultMode,
    U32 delayMs
) {
    static_cast<void>(portNum);
    this->m_nextCaptureOptions = {generation, faultMode, delayMs};
}

void PayloadDriver_NeutronSim::requestIn_handler(
    FwIndexType portNum,
    U32 requestId,
    U32 generation,
    U32 durationMinutes,
    U32 timeoutMs
) {
    static_cast<void>(portNum);
    CaptureOptions options{};
    if (this->m_nextCaptureOptions.generation == generation) {
        options = this->m_nextCaptureOptions;
        this->m_nextCaptureOptions = {};
    }
    if (!this->m_captureSlotAvailable || (this->m_capturePid > 0)) {
        const std::string noText;
        this->emitOperationResult(requestId, generation, Components::PayloadOperationKind::CAPTURE,
                                  Components::PayloadOperationState::FAILED, noText, 0U, 0U,
                                  Components::ScienceProductSource::UNKNOWN, noText, 0U, 1U, false);
        return;
    }
    // The manager marks its slot unavailable when it admits this operation.
    // Publish the driver's matching transition before any early failure return.
    this->m_captureSlotAvailable = false;
    this->setAvailability(false);

    if (options.fault == Components::PayloadSimulationFault::FAIL ||
        durationMinutes == 0U || durationMinutes > MAX_SIMULATED_CAPTURE_DURATION_MINUTES) {
        const std::string noText;
        this->m_lastDurationMinutes = durationMinutes;
        this->m_lastExitStatus = 1U;
        this->tlmWrite_LastDurationMinutes(this->m_lastDurationMinutes);
        this->tlmWrite_LastExitStatus(this->m_lastExitStatus);
        this->log_WARNING_HI_CaptureFailed(durationMinutes, this->m_lastExitStatus);
        this->emitOperationResult(requestId, generation, Components::PayloadOperationKind::CAPTURE,
                                  Components::PayloadOperationState::FAILED, noText, 0U, 0U,
                                  Components::ScienceProductSource::UNKNOWN, noText, 0U, 1U, false);
        this->setAvailability(true);
        return;
    }

    U32 reservedProductId = 0U;
    if (!reserveProductId(reservedProductId)) {
        const std::string noText;
        this->m_lastExitStatus = 128U;
        this->tlmWrite_LastExitStatus(this->m_lastExitStatus);
        this->log_WARNING_HI_CaptureFailed(durationMinutes, this->m_lastExitStatus);
        this->emitOperationResult(requestId, generation, Components::PayloadOperationKind::CAPTURE,
                                  Components::PayloadOperationState::FAILED, noText, 0U, 0U,
                                  Components::ScienceProductSource::UNKNOWN, noText, 0U, 128U, false);
        this->setAvailability(true);
        return;
    }

    this->m_captureRequestId = requestId;
    this->m_captureGeneration = generation;
    this->m_captureProductId = reservedProductId;
    this->m_captureDurationMinutes = durationMinutes;
    this->m_captureTimeoutMs = timeoutMs;
    this->m_captureDelayMs = options.delayMs;
    this->m_captureFaultMode = options.fault;
    this->m_captureKind = Components::PayloadOperationKind::CAPTURE;
    this->m_captureSummary = CaptureSummary{};
    this->m_captureOutputBuffer.clear();
    this->m_captureOutputTotalBytes = 0U;
    this->m_captureKillStage = 0;
    this->m_captureProcessStatus = 0;
    if (!this->launchCapture(durationMinutes, options.fault, timeoutMs)) {
        const std::string noText;
        this->m_captureSlotAvailable = true;
        this->setAvailability(true);
        this->m_lastExitStatus = 127U;
        this->tlmWrite_LastExitStatus(this->m_lastExitStatus);
        this->log_WARNING_HI_CaptureFailed(durationMinutes, this->m_lastExitStatus);
        this->emitOperationResult(requestId, generation, Components::PayloadOperationKind::CAPTURE,
                                  Components::PayloadOperationState::FAILED, noText, 0U, 0U,
                                  Components::ScienceProductSource::UNKNOWN, noText, 0U, 127U, false);
        this->m_captureProductId = 0U;
    }
}

void PayloadDriver_NeutronSim::passthroughIn_handler(
    FwIndexType portNum,
    U32 requestId,
    U32 generation,
    const Fw::StringBase& literalCommand,
    const Components::PayloadSimulationFault& faultMode,
    U32 delayMs,
    U32 timeoutMs
) {
    static_cast<void>(portNum);
    static_cast<void>(timeoutMs);
    const std::string literal(literalCommand.toChar());
    const std::string noPath;
    Components::PayloadOperationState result = Components::PayloadOperationState::COMPLETED;
    U32 errorCode = 0U;
    bool delayed = false;
    bool duplicate = false;

    switch (faultMode) {
        case Components::PayloadSimulationFault::FAIL:
            result = Components::PayloadOperationState::FAILED;
            errorCode = 1U;
            break;
        case Components::PayloadSimulationFault::NO_RESPONSE:
            return;
        case Components::PayloadSimulationFault::DELAYED_RESPONSE:
            delayed = true;
            break;
        case Components::PayloadSimulationFault::DUPLICATE_RESPONSE:
            duplicate = true;
            break;
        case Components::PayloadSimulationFault::NORMAL:
            break;
        case Components::PayloadSimulationFault::HANG_CAPTURE:
        case Components::PayloadSimulationFault::DRIVER_UNAVAILABLE:
            result = Components::PayloadOperationState::FAILED;
            errorCode = 2U;
            break;
    }

    this->scheduleOrSendReply(requestId, generation, Components::PayloadOperationKind::PASSTHROUGH,
                              result, literal, 0U, 0U,
                              Components::ScienceProductSource::UNKNOWN, noPath, 0U,
                              errorCode, delayed, delayMs, duplicate);
}

bool PayloadDriver_NeutronSim::launchCapture(
    U32 durationMinutes,
    const Components::PayloadSimulationFault& faultMode,
    U32 timeoutMs
) {
    int pipeFd[2] = {-1, -1};
    if (::pipe(pipeFd) != 0) {
        return false;
    }

    std::string root;
    std::string script;
    std::string dataset;
    std::string duration;
    if (faultMode != Components::PayloadSimulationFault::HANG_CAPTURE) {
        root = getSimRoot();
        script = root + "/neutron_payload_sim.py";
        dataset = root + "/neutron_data.csv";
        duration = std::to_string(durationMinutes * SECONDS_PER_MINUTE);
    }

    const pid_t child = ::fork();
    if (child < 0) {
        (void)::close(pipeFd[0]);
        (void)::close(pipeFd[1]);
        return false;
    }
    if (child == 0) {
        (void)::close(pipeFd[0]);
        if (::dup2(pipeFd[1], STDOUT_FILENO) < 0 || ::dup2(pipeFd[1], STDERR_FILENO) < 0) {
            _exit(126);
        }
        (void)::close(pipeFd[1]);
        if (faultMode == Components::PayloadSimulationFault::HANG_CAPTURE) {
            char sleepSeconds[16];
            std::snprintf(sleepSeconds, sizeof(sleepSeconds), "%u", HANG_CHILD_SECONDS);
            char* const argv[] = {const_cast<char*>("sleep"), sleepSeconds, nullptr};
            ::execvp(argv[0], argv);
        } else {
            char* const argv[] = {
                const_cast<char*>("python3"),
                const_cast<char*>(script.c_str()),
                const_cast<char*>("capture"),
                const_cast<char*>("--duration-seconds"),
                const_cast<char*>(duration.c_str()),
                const_cast<char*>("--dataset"),
                const_cast<char*>(dataset.c_str()),
                const_cast<char*>("--cursor"),
                const_cast<char*>(DEFAULT_CURSOR),
                const_cast<char*>("--output-dir"),
                const_cast<char*>(DEFAULT_OUTPUT_DIR),
                const_cast<char*>("--end-policy"),
                const_cast<char*>("wrap"),
                const_cast<char*>("--format"),
                const_cast<char*>("kv"),
                nullptr
            };
            ::execvp(argv[0], argv);
        }
        _exit(127);
    }

    (void)::close(pipeFd[1]);
    const int currentFlags = ::fcntl(pipeFd[0], F_GETFL, 0);
    if ((currentFlags < 0) || (::fcntl(pipeFd[0], F_SETFL, currentFlags | O_NONBLOCK) != 0)) {
        (void)::kill(child, SIGKILL);
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        (void)::close(pipeFd[0]);
        return false;
    }
    this->m_capturePid = child;
    this->m_captureOutputFd = pipeFd[0];
    this->m_captureDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    this->m_lastDurationMinutes = durationMinutes;
    this->tlmWrite_LastDurationMinutes(this->m_lastDurationMinutes);
    return true;
}

void PayloadDriver_NeutronSim::pollCapture() {
    if (this->m_captureOutputFd >= 0) {
        char buffer[1024];
        ssize_t count = 0;
        U32 readBudget = 4096U;
        while ((readBudget > 0U) && ((count = ::read(this->m_captureOutputFd, buffer,
                                              std::min<U32>(sizeof(buffer), readBudget))) > 0)) {
            readBudget -= static_cast<U32>(count);
            this->m_captureOutputTotalBytes += static_cast<U32>(count);
            if (this->m_captureOutputTotalBytes > MAX_CAPTURE_OUTPUT) {
                this->m_captureSummary.exitStatus = 125U;
                (void)::kill(this->m_capturePid, SIGKILL);
                this->m_captureKillStage = 2;
                break;
            }
            if (this->m_captureOutputBuffer.size() + static_cast<std::size_t>(count) > MAX_CAPTURE_OUTPUT) {
                this->m_captureSummary.exitStatus = 125U;
                (void)::kill(this->m_capturePid, SIGKILL);
                this->m_captureKillStage = 2;
                break;
            }
            this->m_captureOutputBuffer.append(buffer, static_cast<std::size_t>(count));
            std::size_t newline = 0;
            while ((newline = this->m_captureOutputBuffer.find('\n')) != std::string::npos) {
                const std::string line = this->m_captureOutputBuffer.substr(0, newline + 1U);
                parseSummaryLine(line, this->m_captureSummary);
                this->m_captureOutputBuffer.erase(0, newline + 1U);
            }
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if ((this->m_captureKillStage == 0) && (now >= this->m_captureDeadline)) {
        (void)::kill(this->m_capturePid, SIGTERM);
        this->m_captureKillStage = 1;
        this->m_killDeadline = now + std::chrono::milliseconds(KILL_GRACE_MS);
    } else if ((this->m_captureKillStage == 1) && (now >= this->m_killDeadline)) {
        (void)::kill(this->m_capturePid, SIGKILL);
        this->m_captureKillStage = 2;
    }

    int processStatus = 0;
    const pid_t waitResult = ::waitpid(this->m_capturePid, &processStatus, WNOHANG);
    if (waitResult == 0) {
        return;
    }
    if ((waitResult < 0) && (errno != ECHILD)) {
        return;
    }
    this->m_captureProcessStatus = processStatus;
    U32 exitStatus = 0U;
    if (waitResult < 0) {
        exitStatus = 127U;
    } else if (WIFEXITED(processStatus)) {
        exitStatus = static_cast<U32>(WEXITSTATUS(processStatus));
    } else {
        exitStatus = 128U;
    }
    if (this->m_captureKillStage > 0) {
        exitStatus = 124U;
    }

    if (this->m_captureOutputFd >= 0) {
        char buffer[1024];
        ssize_t count = 0;
        U32 readBudget = 4096U;
        while ((readBudget > 0U) && ((count = ::read(this->m_captureOutputFd, buffer,
                                              std::min<U32>(sizeof(buffer), readBudget))) > 0)) {
            readBudget -= static_cast<U32>(count);
            this->m_captureOutputTotalBytes += static_cast<U32>(count);
            this->m_captureOutputBuffer.append(buffer, static_cast<std::size_t>(count));
        }
        (void)::close(this->m_captureOutputFd);
        this->m_captureOutputFd = -1;
    }
    if (!this->m_captureOutputBuffer.empty()) {
        parseSummaryLine(this->m_captureOutputBuffer, this->m_captureSummary);
        this->m_captureOutputBuffer.clear();
    }
    this->m_capturePid = 0;
    this->finishCapture(exitStatus, (this->m_captureKillStage > 0) ||
                                      (this->m_captureFaultMode == Components::PayloadSimulationFault::NO_RESPONSE) ||
                                      (this->m_captureFaultMode == Components::PayloadSimulationFault::HANG_CAPTURE));
}

void PayloadDriver_NeutronSim::finishCapture(U32 exitStatus, bool suppressResponse) {
    CaptureSummary& summary = this->m_captureSummary;
    summary.exitStatus = exitStatus;
    if ((summary.exitStatus == 0U) &&
        ((summary.outputPath.empty()) || (summary.productBytes == 0U) ||
         !publishLatestCapture(summary.outputPath))) {
        summary.exitStatus = 126U;
    }
    U32 sourceCrc = 0U;
    if ((summary.exitStatus == 0U) && !computeFileCrc16(summary.outputPath, sourceCrc)) {
        summary.exitStatus = 125U;
    }

    const U32 productId = this->m_captureProductId;
    this->m_lastDurationMinutes = this->m_captureDurationMinutes;
    this->m_lastRowsCaptured = summary.rows;
    this->m_lastTotalCounts = summary.totalCounts;
    this->m_lastSaaRows = summary.saaRows;
    this->m_lastProductBytes = summary.productBytes;
    this->m_lastExitStatus = summary.exitStatus;
    this->m_lastProductCrc = sourceCrc;
    if (summary.exitStatus == 0U) {
        this->m_lastProductId = productId;
    }

    this->tlmWrite_LastDurationMinutes(this->m_lastDurationMinutes);
    this->tlmWrite_LastRowsCaptured(this->m_lastRowsCaptured);
    this->tlmWrite_LastTotalCounts(this->m_lastTotalCounts);
    this->tlmWrite_LastSaaRows(this->m_lastSaaRows);
    this->tlmWrite_LastProductBytes(this->m_lastProductBytes);
    this->tlmWrite_LastExitStatus(this->m_lastExitStatus);
    this->tlmWrite_LastProductId(this->m_lastProductId);
    this->tlmWrite_LastProductCrc(this->m_lastProductCrc);

    this->m_captureSlotAvailable = true;
    this->setAvailability(true);
    if (summary.exitStatus == 0U) {
        this->log_ACTIVITY_HI_CaptureComplete(this->m_captureDurationMinutes,
                                              summary.rows, summary.productBytes, productId);
        if (!suppressResponse) {
            this->scheduleOrSendReply(this->m_captureRequestId, this->m_captureGeneration,
                                      Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::COMPLETED, "",
                                      productId, summary.productBytes,
                                      Components::ScienceProductSource::NEUTRON_SIM,
                                      summary.outputPath, sourceCrc, 0U,
                                      this->m_captureFaultMode == Components::PayloadSimulationFault::DELAYED_RESPONSE,
                                      this->m_captureDelayMs,
                                      this->m_captureFaultMode == Components::PayloadSimulationFault::DUPLICATE_RESPONSE);
        }
    } else {
        this->log_WARNING_HI_CaptureFailed(this->m_captureDurationMinutes, summary.exitStatus);
        if (!suppressResponse) {
            this->scheduleOrSendReply(this->m_captureRequestId, this->m_captureGeneration,
                                      Components::PayloadOperationKind::CAPTURE,
                                      Components::PayloadOperationState::FAILED, "", 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, "", 0U,
                                      summary.exitStatus, false, 0U, false);
        } else {
            this->log_WARNING_HI_CaptureWorkerReaped(this->m_captureGeneration, summary.exitStatus);
        }
    }
    this->m_captureProductId = 0U;
    this->m_captureKillStage = 0;
}

void PayloadDriver_NeutronSim::scheduleOrSendReply(
    U32 requestId,
    U32 generation,
    const Components::PayloadOperationKind& kind,
    const Components::PayloadOperationState& state,
    const std::string& literalEcho,
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const std::string& sourcePath,
    U32 sourceCrc,
    U32 errorCode,
    bool delayed,
    U32 delayMs,
    bool duplicate
) {
    if (delayed) {
        PendingReply* pendingSlot = nullptr;
        for (U32 index = 0U; index < PENDING_REPLY_CAPACITY; ++index) {
            if (!this->m_pendingReplies[index].valid) {
                pendingSlot = &this->m_pendingReplies[index];
                break;
            }
        }
        if (pendingSlot == nullptr) {
            this->emitOperationResult(requestId, generation, kind,
                                      Components::PayloadOperationState::FAILED, "", 0U, 0U,
                                      Components::ScienceProductSource::UNKNOWN, "", 0U, 3U, false);
            return;
        }
        *pendingSlot = PendingReply{
            true, requestId, generation, kind, state, literalEcho,
            productId, productBytes, sourceKind, sourcePath, sourceCrc,
            errorCode, std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs), duplicate
        };
        return;
    }
    this->emitOperationResult(requestId, generation, kind, state, literalEcho,
                              productId, productBytes, sourceKind, sourcePath,
                              sourceCrc, errorCode, duplicate);
}

void PayloadDriver_NeutronSim::emitOperationResult(
    U32 requestId,
    U32 generation,
    const Components::PayloadOperationKind& kind,
    const Components::PayloadOperationState& state,
    const std::string& literalEcho,
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const std::string& sourcePath,
    U32 sourceCrc,
    U32 errorCode,
    bool duplicate
) {
    const Fw::String echo(literalEcho.c_str());
    const Fw::String path(sourcePath.c_str());
    this->operationResponseOut_out(0, requestId, generation, kind, state, echo,
                                  productId, productBytes, sourceKind, path, sourceCrc, errorCode);
    if (duplicate) {
        this->operationResponseOut_out(0, requestId, generation, kind, state, echo,
                                      productId, productBytes, sourceKind, path, sourceCrc, errorCode);
    }
}

void PayloadDriver_NeutronSim::setAvailability(bool available) {
    this->m_captureSlotAvailable = available;
    if (!this->isConnected_availabilityOut_OutputPort(0)) {
        return;
    }
    if (this->m_availabilityAnnounced && (this->m_announcedAvailable == available)) {
        return;
    }
    this->availabilityOut_out(0, available);
    this->m_availabilityAnnounced = true;
    this->m_announcedAvailable = available;
}

std::string PayloadDriver_NeutronSim::getSimRoot() {
    const char* root = std::getenv("NEUTRON_PAYLOAD_SIM_ROOT");
    if ((root == nullptr) || (root[0] == '\0')) {
        const std::string defaultDataset = std::string(DEFAULT_SIM_ROOT) + "/neutron_data.csv";
        if (::access(defaultDataset.c_str(), R_OK) == 0) {
            return DEFAULT_SIM_ROOT;
        }
        const std::string repoRootDataset = std::string(REPO_ROOT_SIM_ROOT) + "/neutron_data.csv";
        if (::access(repoRootDataset.c_str(), R_OK) == 0) {
            return REPO_ROOT_SIM_ROOT;
        }
        return DEFAULT_SIM_ROOT;
    }
    return root;
}

void PayloadDriver_NeutronSim::parseSummaryLine(const std::string& line, CaptureSummary& summary) {
    const std::size_t separator = line.find('=');
    if (separator == std::string::npos) {
        return;
    }
    const std::string key = line.substr(0, separator);
    std::string value = line.substr(separator + 1U);
    while (!value.empty() &&
           ((value.back() == '\n') || (value.back() == '\r') || (value.back() == ' ') || (value.back() == '\t'))) {
        value.pop_back();
    }
    if (key == "rows") {
        summary.rows = parseU32(value);
    } else if (key == "total_counts") {
        summary.totalCounts = parseU32(value);
    } else if (key == "saa_rows") {
        summary.saaRows = parseU32(value);
    } else if (key == "output_bytes") {
        summary.productBytes = parseU32(value);
    } else if (key == "output_path") {
        summary.outputPath = value;
    }
}

bool PayloadDriver_NeutronSim::publishLatestCapture(const std::string& outputPath) {
    struct stat info {};
    if ((::stat(outputPath.c_str(), &info) != 0) || !S_ISREG(info.st_mode) || (info.st_size <= 0)) {
        return false;
    }
    (void)::unlink(DEFAULT_LATEST_PAYLOAD);
    return (::symlink(outputPath.c_str(), DEFAULT_LATEST_PAYLOAD) == 0);
}

bool PayloadDriver_NeutronSim::computeFileCrc16(const std::string& outputPath, U32& crcOut) {
    std::FILE* file = std::fopen(outputPath.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    U16 crc = 0xFFFFU;
    int value = 0;
    while ((value = std::fgetc(file)) != EOF) {
        crc ^= static_cast<U16>(static_cast<U8>(value)) << 8U;
        for (U8 bit = 0; bit < 8U; bit++) {
            crc = ((crc & 0x8000U) != 0U)
                ? static_cast<U16>((crc << 1U) ^ 0x1021U)
                : static_cast<U16>(crc << 1U);
        }
    }
    const bool readOk = !std::ferror(file);
    (void)std::fclose(file);
    if (!readOk) {
        return false;
    }
    crcOut = static_cast<U32>(crc);
    return true;
}

U32 PayloadDriver_NeutronSim::parseU32(const std::string& value) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if ((end == value.c_str()) || (parsed > std::numeric_limits<U32>::max())) {
        return std::numeric_limits<U32>::max();
    }
    return static_cast<U32>(parsed);
}

}  // namespace Components
