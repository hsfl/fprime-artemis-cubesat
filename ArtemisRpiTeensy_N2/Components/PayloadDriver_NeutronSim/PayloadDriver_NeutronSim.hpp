#ifndef Components_PayloadDriver_NeutronSim_HPP
#define Components_PayloadDriver_NeutronSim_HPP

#include "Components/PayloadDriver_NeutronSim/PayloadDriver_NeutronSimComponentAc.hpp"

#include <chrono>
#include <string>
#include <sys/types.h>

namespace Components {

class PayloadDriver_NeutronSim final : public PayloadDriver_NeutronSimComponentBase {
  public:
    PayloadDriver_NeutronSim(const char* const compName);
    ~PayloadDriver_NeutronSim();

  private:
    struct CaptureSummary {
        U32 rows;
        U32 totalCounts;
        U32 saaRows;
        U32 productBytes;
        U32 exitStatus;
        std::string outputPath;
    };

    struct CaptureOptions {
        U32 generation = 0U;
        Components::PayloadSimulationFault fault = Components::PayloadSimulationFault::NORMAL;
        U32 delayMs = 0U;
    };

    struct PendingReply {
        bool valid = false;
        U32 requestId = 0U;
        U32 generation = 0U;
        Components::PayloadOperationKind kind = Components::PayloadOperationKind::CAPTURE;
        Components::PayloadOperationState state = Components::PayloadOperationState::FAILED;
        std::string literalEcho;
        U32 productId = 0U;
        U32 productBytes = 0U;
        Components::ScienceProductSource sourceKind = Components::ScienceProductSource::UNKNOWN;
        std::string sourcePath;
        U32 sourceCrc = 0U;
        U32 errorCode = 0U;
        std::chrono::steady_clock::time_point due{};
        bool duplicate = false;
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void simulationCaptureOptionsIn_handler(
        FwIndexType portNum,
        U32 generation,
        const Components::PayloadSimulationFault& faultMode,
        U32 delayMs
    ) override;
    void requestIn_handler(
        FwIndexType portNum,
        U32 requestId,
        U32 generation,
        U32 durationMinutes,
        U32 timeoutMs
    ) override;
    void passthroughIn_handler(
        FwIndexType portNum,
        U32 requestId,
        U32 generation,
        const Fw::StringBase& literalCommand,
        const Components::PayloadSimulationFault& faultMode,
        U32 delayMs,
        U32 timeoutMs
    ) override;

    bool launchCapture(U32 durationMinutes, const Components::PayloadSimulationFault& faultMode, U32 timeoutMs);
    void pollCapture();
    void finishCapture(U32 exitStatus, bool suppressResponse);
    void scheduleOrSendReply(
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
    );
    void emitOperationResult(
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
    );
    void setAvailability(bool available);
    static std::string getSimRoot();
    static void parseSummaryLine(const std::string& line, CaptureSummary& summary);
    static bool publishLatestCapture(const std::string& outputPath);
    static bool computeFileCrc16(const std::string& outputPath, U32& crcOut);
    static U32 parseU32(const std::string& value);

    U32 m_lastDurationMinutes;
    U32 m_lastRowsCaptured;
    U32 m_lastTotalCounts;
    U32 m_lastSaaRows;
    U32 m_lastProductBytes;
    U32 m_lastExitStatus;
    U32 m_lastProductId;
    U32 m_lastProductCrc;
    bool m_captureSlotAvailable;
    bool m_availabilityAnnounced;
    bool m_announcedAvailable;
    int m_captureOutputFd;
    int m_captureKillStage;
    int m_captureProcessStatus;
    pid_t m_capturePid;
    U32 m_captureRequestId;
    U32 m_captureGeneration;
    U32 m_captureProductId;
    U32 m_captureDurationMinutes;
    U32 m_captureTimeoutMs;
    U32 m_captureDelayMs;
    Components::PayloadSimulationFault m_captureFaultMode;
    Components::PayloadOperationKind m_captureKind;
    CaptureSummary m_captureSummary;
    std::string m_captureOutputBuffer;
    U32 m_captureOutputTotalBytes;
    std::chrono::steady_clock::time_point m_captureDeadline;
    std::chrono::steady_clock::time_point m_killDeadline;
    CaptureOptions m_nextCaptureOptions{};
    static constexpr U32 PENDING_REPLY_CAPACITY = 4U;
    PendingReply m_pendingReplies[PENDING_REPLY_CAPACITY];
};

}  // namespace Components

#endif
