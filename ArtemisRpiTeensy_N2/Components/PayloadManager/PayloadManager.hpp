#ifndef Components_PayloadManager_HPP
#define Components_PayloadManager_HPP

#include "Components/PayloadManager/PayloadManagerComponentAc.hpp"
#include <Fw/Cmd/CmdString.hpp>

#include <chrono>

namespace Components {

class PayloadManager final : public PayloadManagerComponentBase {
  public:
    PayloadManager(const char* const compName);
    ~PayloadManager();

  private:
    struct Operation {
        bool active;
        U32 requestId;
        U32 generation;
        Components::PayloadOperationKind kind;
        bool scienceRequested;
        std::chrono::steady_clock::time_point deadline;
    };

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 requestId, U32 durationMinutes) override;
    void driverOperationResultIn_handler(
        FwIndexType portNum,
        U32 requestId,
        U32 generation,
        const Components::PayloadOperationKind& kind,
        const Components::PayloadOperationState& state,
        const Fw::StringBase& literalEcho,
        U32 productId,
        U32 productBytes,
        const Components::ScienceProductSource& sourceKind,
        const Fw::StringBase& sourcePath,
        U32 sourceCrc,
        U32 errorCode
    ) override;
    void driverAvailabilityIn_handler(FwIndexType portNum, bool available) override;
    void REQUEST_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CONFIGURE_PAYLOAD_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 sampleCount, U32 periodMs) override;
    void START_PAYLOAD_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 collectionId) override;
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationMinutes) override;
    void SYNTHETIC_PASSTHROUGH_cmdHandler(
        FwOpcodeType opCode,
        U32 cmdSeq,
        U32 requestId,
        const Fw::CmdStringArg& literalCommand
    ) override;
    void CONFIGURE_SIMULATION_cmdHandler(
        FwOpcodeType opCode,
        U32 cmdSeq,
        Components::PayloadSimulationFault faultMode,
        U32 delayMs,
        U32 timeoutMs
    ) override;

    bool startCapture(U32 requestId, U32 durationMinutes, bool scienceRequested = false);
    bool startPassthrough(U32 requestId, const Fw::StringBase& literalCommand);
    void emitOperationStatus(
        U32 requestId,
        U32 generation,
        const Components::PayloadOperationKind& kind,
        const Components::PayloadOperationState& state,
        U32 productId = 0U,
        U32 productBytes = 0U,
        const Components::ScienceProductSource& sourceKind = Components::ScienceProductSource::UNKNOWN,
        const Fw::StringBase* sourcePath = nullptr,
        U32 sourceCrc = 0U,
        bool scienceRequested = false
    );
    void finishOperation(
        const Components::PayloadOperationState& state,
        const Fw::StringBase& literalEcho,
        U32 productId,
        U32 productBytes,
        const Components::ScienceProductSource& sourceKind,
        const Fw::StringBase& sourcePath,
        U32 sourceCrc,
        U32 errorCode
    );
    void rejectOperation(
        U32 requestId,
        const Components::PayloadOperationKind& kind,
        const Components::PayloadOperationState& state,
        bool scienceRequested = false
    );
    void writeStatusTelemetry();
    U32 allocateGeneration();
    U32 allocateRequestId();
    bool isValidProduct(U32 productId, U32 productBytes,
                        const Components::ScienceProductSource& sourceKind,
                        const Fw::StringBase& sourcePath, U32 sourceCrc) const;
    bool isValidCaptureDuration(U32 durationMinutes) const;
    void consumeSimulationFault();

    U32 m_lastPayloadValue;
    U32 m_lastCollectionId;
    U32 m_lastCaptureDurationMinutes;
    U32 m_sampleCount;
    U32 m_samplePeriodMs;
    U32 m_managerHeartbeat;
    U32 m_nextGeneration;
    U32 m_nextRequestId;
    U32 m_lastOperationGeneration;
    U32 m_lastCompletedGeneration;
    bool m_driverAvailable;
    Operation m_operation;
    Components::PayloadSimulationFault m_nextFaultMode;
    U32 m_nextDelayMs;
    U32 m_nextTimeoutMs;
    bool m_simulationConfigured;
};

}  // namespace Components

#endif
