#include "Components/PayloadManager/PayloadManager.hpp"
#include "Components/Types/FppConstantsAc.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace Components {

namespace {
constexpr U32 DEFAULT_OPERATION_TIMEOUT_MS = 5000U;
constexpr U32 MIN_OPERATION_TIMEOUT_MS = 1000U;
constexpr U32 MAX_OPERATION_TIMEOUT_MS = 8000U;
constexpr U32 MAX_SIMULATION_DELAY_MS = 8000U;
constexpr U32 REPLY_IGNORED_STALE = 1U;
constexpr U32 REPLY_IGNORED_DUPLICATE = 2U;
constexpr U32 INVALID_CAPTURE_DURATION = 1U;
constexpr U32 DRIVER_RESULT_INVALID_PRODUCT = 2U;
constexpr U32 CONFIG_REJECTED_NOT_LOCAL = 1U;
constexpr U32 CONFIG_REJECTED_BUSY = 2U;
constexpr U32 CONFIG_REJECTED_RANGE = 3U;
constexpr U32 CONFIG_REJECTED_MODE = 4U;

Fw::String emptyString() {
    return Fw::String("");
}
}  // namespace

PayloadManager::PayloadManager(const char* const compName)
    : PayloadManagerComponentBase(compName),
      m_lastPayloadValue(0),
      m_lastCollectionId(0),
      m_lastCaptureDurationMinutes(DEFAULT_CAPTURE_DURATION_MINUTES),
      m_sampleCount(1),
      m_samplePeriodMs(1000),
      m_managerHeartbeat(0),
      m_nextGeneration(1),
      m_nextRequestId(1),
      m_lastOperationGeneration(0),
      m_lastCompletedGeneration(0),
      m_driverAvailable(true),
      m_operation{false, 0, 0, Components::PayloadOperationKind::CAPTURE, false,
                  std::chrono::steady_clock::time_point{}},
      m_nextFaultMode(Components::PayloadSimulationFault::NORMAL),
      m_nextDelayMs(1000),
      m_nextTimeoutMs(DEFAULT_OPERATION_TIMEOUT_MS),
      m_simulationConfigured(false) {}

PayloadManager::~PayloadManager() {}

void PayloadManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void PayloadManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_managerHeartbeat += 1;
    if (this->m_operation.active && (std::chrono::steady_clock::now() >= this->m_operation.deadline)) {
        const Fw::String path = emptyString();
        this->finishOperation(
            Components::PayloadOperationState::TIMED_OUT,
            path,
            0U,
            0U,
            Components::ScienceProductSource::UNKNOWN,
            path,
            0U,
            0U
        );
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        const Components::HealthState health =
            (this->m_lastPayloadValue == 0U) ? Components::HealthState::UNKNOWN : Components::HealthState::OK;
        this->sohStatusOut_out(0, health, this->m_lastPayloadValue);
    }
    this->tlmWrite_LastPayloadValue(this->m_lastPayloadValue);
    this->tlmWrite_LastCollectionId(this->m_lastCollectionId);
    this->tlmWrite_LastCaptureDurationMinutes(this->m_lastCaptureDurationMinutes);
    this->tlmWrite_SampleCount(this->m_sampleCount);
    this->tlmWrite_SamplePeriodMs(this->m_samplePeriodMs);
    this->tlmWrite_ManagerHeartbeat(this->m_managerHeartbeat);
    this->writeStatusTelemetry();
}

void PayloadManager::requestIn_handler(FwIndexType portNum, U32 requestId, U32 durationMinutes) {
    static_cast<void>(portNum);
    const U32 callerId = (requestId == 0U) ? this->allocateRequestId() : requestId;
    this->m_lastCollectionId = callerId;
    static_cast<void>(this->startCapture(callerId, durationMinutes, true));
}

void PayloadManager::driverOperationResultIn_handler(
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
) {
    static_cast<void>(portNum);
    if (!this->m_operation.active || (this->m_operation.generation != generation) ||
        (this->m_operation.requestId != requestId) || (this->m_operation.kind != kind)) {
        const U32 reason = (generation == this->m_lastCompletedGeneration) ? REPLY_IGNORED_DUPLICATE : REPLY_IGNORED_STALE;
        this->log_WARNING_LO_PayloadReplyIgnored(requestId, generation, reason);
        return;
    }

    if (std::chrono::steady_clock::now() >= this->m_operation.deadline) {
        const Fw::String path = emptyString();
        this->finishOperation(Components::PayloadOperationState::TIMED_OUT, path, 0U, 0U,
                              Components::ScienceProductSource::UNKNOWN, path, 0U, 0U);
        this->log_WARNING_LO_PayloadReplyIgnored(requestId, generation, REPLY_IGNORED_STALE);
        return;
    }

    if (state == Components::PayloadOperationState::COMPLETED &&
        kind == Components::PayloadOperationKind::CAPTURE &&
        !this->isValidProduct(productId, productBytes, sourceKind, sourcePath, sourceCrc)) {
        this->finishOperation(
            Components::PayloadOperationState::FAILED,
            literalEcho,
            0U,
            0U,
            Components::ScienceProductSource::UNKNOWN,
            emptyString(),
            0U,
            DRIVER_RESULT_INVALID_PRODUCT
        );
        return;
    }

    if ((state != Components::PayloadOperationState::COMPLETED) &&
        (state != Components::PayloadOperationState::FAILED)) {
        this->log_WARNING_LO_PayloadReplyIgnored(requestId, generation, REPLY_IGNORED_STALE);
        return;
    }
    this->finishOperation(state, literalEcho, productId, productBytes, sourceKind, sourcePath, sourceCrc, errorCode);
}

void PayloadManager::driverAvailabilityIn_handler(FwIndexType portNum, bool available) {
    static_cast<void>(portNum);
    this->m_driverAvailable = available;
    this->writeStatusTelemetry();
}

void PayloadManager::REQUEST_PAYLOAD_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->tlmWrite_LastPayloadValue(this->m_lastPayloadValue);
    this->tlmWrite_LastCollectionId(this->m_lastCollectionId);
    this->tlmWrite_LastCaptureDurationMinutes(this->m_lastCaptureDurationMinutes);
    this->writeStatusTelemetry();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadManager::CONFIGURE_PAYLOAD_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    U32 sampleCount,
    U32 periodMs
) {
    this->m_sampleCount = sampleCount;
    this->m_samplePeriodMs = periodMs;
    this->log_ACTIVITY_HI_PayloadConfigured(this->m_sampleCount, this->m_samplePeriodMs);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void PayloadManager::START_PAYLOAD_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 collectionId) {
    this->m_lastCollectionId = collectionId;
    const bool accepted = this->startCapture(collectionId, this->m_lastCaptureDurationMinutes);
    this->cmdResponse_out(opCode, cmdSeq, accepted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void PayloadManager::SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationMinutes) {
    const U32 requestId = this->allocateRequestId();
    const bool accepted = this->startCapture(requestId, durationMinutes);
    this->cmdResponse_out(opCode, cmdSeq, accepted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void PayloadManager::SYNTHETIC_PASSTHROUGH_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    U32 requestId,
    const Fw::CmdStringArg& literalCommand
) {
    if (literalCommand.length() > Components::PAYLOAD_PASSTHROUGH_LITERAL_MAX) {
        this->log_WARNING_HI_SyntheticPassthroughRejected(requestId, literalCommand.length());
        this->rejectOperation(requestId, Components::PayloadOperationKind::PASSTHROUGH,
                              Components::PayloadOperationState::INVALID);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if ((requestId == 0U) || (literalCommand.length() == 0U)) {
        this->log_WARNING_HI_SyntheticPassthroughRejected(requestId, 3U);
        this->rejectOperation(requestId, Components::PayloadOperationKind::PASSTHROUGH,
                              Components::PayloadOperationState::INVALID);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    const bool accepted = this->startPassthrough(requestId, literalCommand);
    this->cmdResponse_out(opCode, cmdSeq, accepted ? Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR);
}

void PayloadManager::CONFIGURE_SIMULATION_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    Components::PayloadSimulationFault faultMode,
    U32 delayMs,
    U32 timeoutMs
) {
#ifndef NEUTRON2_LOCAL_YAMCS
    static_cast<void>(faultMode);
    static_cast<void>(delayMs);
    static_cast<void>(timeoutMs);
    this->log_WARNING_LO_SimulationConfigRejected(CONFIG_REJECTED_NOT_LOCAL);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    return;
#else
    if (this->m_operation.active || !this->m_driverAvailable) {
        this->log_WARNING_LO_SimulationConfigRejected(CONFIG_REJECTED_BUSY);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }
    if ((timeoutMs < MIN_OPERATION_TIMEOUT_MS) || (timeoutMs > MAX_OPERATION_TIMEOUT_MS) ||
        (delayMs > MAX_SIMULATION_DELAY_MS)) {
        this->log_WARNING_LO_SimulationConfigRejected(CONFIG_REJECTED_RANGE);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    const U8 modeValue = static_cast<U8>(faultMode);
    if (modeValue > static_cast<U8>(Components::PayloadSimulationFault::DRIVER_UNAVAILABLE) ||
        ((faultMode == Components::PayloadSimulationFault::DELAYED_RESPONSE) && (delayMs == 0U))) {
        this->log_WARNING_LO_SimulationConfigRejected(CONFIG_REJECTED_MODE);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->m_nextFaultMode = faultMode;
    this->m_nextDelayMs = delayMs;
    this->m_nextTimeoutMs = timeoutMs;
    this->m_simulationConfigured = true;
    this->log_ACTIVITY_HI_SimulationConfigured(faultMode, delayMs, timeoutMs);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
#endif
}

bool PayloadManager::startCapture(U32 requestId, U32 durationMinutes, bool scienceRequested) {
    const Components::PayloadOperationKind kind = Components::PayloadOperationKind::CAPTURE;
    if (requestId == 0U) {
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::INVALID, scienceRequested);
        return false;
    }
    if (this->m_operation.active) {
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::BUSY, scienceRequested);
        return false;
    }
    if (!this->isValidCaptureDuration(durationMinutes)) {
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::INVALID, scienceRequested);
        return false;
    }
    if (!this->isConnected_driverRequestOut_OutputPort(0) || !this->m_driverAvailable) {
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::DRIVER_UNAVAILABLE,
                              scienceRequested);
        return false;
    }

    Components::PayloadSimulationFault faultMode = Components::PayloadSimulationFault::NORMAL;
    if (this->m_simulationConfigured) {
        faultMode = this->m_nextFaultMode;
    }
    const U32 delayMs = this->m_nextDelayMs;
    const U32 timeoutMs = this->m_nextTimeoutMs;
    if (faultMode == Components::PayloadSimulationFault::DRIVER_UNAVAILABLE) {
        this->consumeSimulationFault();
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::DRIVER_UNAVAILABLE,
                              scienceRequested);
        return false;
    }

    const U32 generation = this->allocateGeneration();
    this->m_operation = Operation{
        true,
        requestId,
        generation,
        kind,
        scienceRequested,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs)
    };
    this->m_lastOperationGeneration = generation;
    this->m_lastCollectionId = requestId;
    this->m_lastCaptureDurationMinutes = durationMinutes;
    this->m_driverAvailable = false;
    this->consumeSimulationFault();
    this->emitOperationStatus(requestId, generation, kind, Components::PayloadOperationState::ACCEPTED,
                              0U, 0U, Components::ScienceProductSource::UNKNOWN, nullptr, 0U,
                              scienceRequested);
    this->log_ACTIVITY_HI_PayloadCollectionForwarded(requestId);
    this->log_ACTIVITY_HI_PayloadScienceCaptureRequested(durationMinutes);
    if (this->isConnected_simulationCaptureOptionsOut_OutputPort(0)) {
        this->simulationCaptureOptionsOut_out(0, generation, faultMode, delayMs);
    }
    this->driverRequestOut_out(0, requestId, generation, durationMinutes, timeoutMs);
    this->writeStatusTelemetry();
    return true;
}

bool PayloadManager::startPassthrough(U32 requestId, const Fw::StringBase& literalCommand) {
    const Components::PayloadOperationKind kind = Components::PayloadOperationKind::PASSTHROUGH;
    if ((requestId == 0U) || (literalCommand.length() == 0U)) {
        this->log_WARNING_HI_SyntheticPassthroughRejected(requestId, 3U);
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::INVALID);
        return false;
    }
    if (this->m_operation.active) {
        this->log_WARNING_HI_SyntheticPassthroughRejected(requestId, 2U);
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::BUSY);
        return false;
    }
    if (!this->isConnected_driverPassthroughOut_OutputPort(0) || !this->m_driverAvailable) {
        this->log_WARNING_HI_SyntheticPassthroughRejected(requestId, 1U);
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::DRIVER_UNAVAILABLE);
        return false;
    }
    Components::PayloadSimulationFault faultMode = Components::PayloadSimulationFault::NORMAL;
    if (this->m_simulationConfigured) {
        faultMode = this->m_nextFaultMode;
    }
    const U32 delayMs = this->m_nextDelayMs;
    const U32 timeoutMs = this->m_nextTimeoutMs;
    if (faultMode == Components::PayloadSimulationFault::DRIVER_UNAVAILABLE) {
        this->consumeSimulationFault();
        this->log_WARNING_HI_SyntheticPassthroughRejected(requestId, 1U);
        this->rejectOperation(requestId, kind, Components::PayloadOperationState::DRIVER_UNAVAILABLE);
        return false;
    }

    const U32 generation = this->allocateGeneration();
    this->m_operation = Operation{
        true,
        requestId,
        generation,
        kind,
        false,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs)
    };
    this->m_lastOperationGeneration = generation;
    this->consumeSimulationFault();
    this->emitOperationStatus(requestId, generation, kind, Components::PayloadOperationState::ACCEPTED);
    this->log_ACTIVITY_HI_SyntheticPassthroughForwarded(requestId, literalCommand);
    this->driverPassthroughOut_out(0, requestId, generation, literalCommand, faultMode, delayMs, timeoutMs);
    this->writeStatusTelemetry();
    return true;
}

void PayloadManager::emitOperationStatus(
    U32 requestId,
    U32 generation,
    const Components::PayloadOperationKind& kind,
    const Components::PayloadOperationState& state,
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const Fw::StringBase* sourcePath,
    U32 sourceCrc,
    bool scienceRequested
) {
    const Fw::String path = (sourcePath == nullptr) ? emptyString() : Fw::String(sourcePath->toChar());
    this->log_ACTIVITY_HI_PayloadOperationStatus(requestId, generation, kind, state);
    if (this->isConnected_operationNoticeOut_OutputPort(0)) {
        this->operationNoticeOut_out(
            0, requestId, generation, kind, state, productId, productBytes, sourceKind, path, sourceCrc,
            scienceRequested
        );
    }
}

void PayloadManager::finishOperation(
    const Components::PayloadOperationState& state,
    const Fw::StringBase& literalEcho,
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const Fw::StringBase& sourcePath,
    U32 sourceCrc,
    U32 errorCode
) {
    const Operation completed = this->m_operation;
    if (!completed.active) {
        return;
    }
    this->m_operation.active = false;
    this->m_lastCompletedGeneration = completed.generation;
    if (state == Components::PayloadOperationState::COMPLETED &&
        completed.kind == Components::PayloadOperationKind::CAPTURE) {
        this->m_lastPayloadValue = productBytes;
        this->log_ACTIVITY_LO_PayloadStatusUpdated(productBytes);
    }
    Components::ScienceProductSource completedSource = Components::ScienceProductSource::UNKNOWN;
    if (state == Components::PayloadOperationState::COMPLETED) {
        completedSource = sourceKind;
    }
    this->emitOperationStatus(
        completed.requestId,
        completed.generation,
        completed.kind,
        state,
        (state == Components::PayloadOperationState::COMPLETED) ? productId : 0U,
        (state == Components::PayloadOperationState::COMPLETED) ? productBytes : 0U,
        completedSource,
        (state == Components::PayloadOperationState::COMPLETED) ? &sourcePath : nullptr,
        (state == Components::PayloadOperationState::COMPLETED) ? sourceCrc : 0U,
        completed.scienceRequested
    );
    if ((state == Components::PayloadOperationState::COMPLETED) &&
        (completed.kind == Components::PayloadOperationKind::PASSTHROUGH)) {
        this->log_ACTIVITY_HI_SyntheticPassthroughResponse(
            completed.requestId,
            Components::PayloadPassthroughStatus::SIMULATED_ECHOED,
            literalEcho
        );
    }
    static_cast<void>(errorCode);
    this->writeStatusTelemetry();
}

void PayloadManager::rejectOperation(
    U32 requestId,
    const Components::PayloadOperationKind& kind,
    const Components::PayloadOperationState& state,
    bool scienceRequested
) {
    const Fw::String path = emptyString();
    this->emitOperationStatus(requestId, 0U, kind, state, 0U, 0U,
                              Components::ScienceProductSource::UNKNOWN, &path, 0U, scienceRequested);
}

void PayloadManager::writeStatusTelemetry() {
    this->tlmWrite_PayloadDriverAvailable(this->m_driverAvailable);
    this->tlmWrite_PayloadOperationInFlight(this->m_operation.active);
    this->tlmWrite_LastOperationGeneration(this->m_lastOperationGeneration);
}

U32 PayloadManager::allocateGeneration() {
    U32 result = this->m_nextGeneration++;
    if (result == 0U) {
        result = this->m_nextGeneration++;
    }
    if (this->m_nextGeneration == 0U) {
        this->m_nextGeneration = 1U;
    }
    return result;
}

U32 PayloadManager::allocateRequestId() {
    U32 result = this->m_nextRequestId++;
    if (result == 0U) {
        result = this->m_nextRequestId++;
    }
    if (this->m_nextRequestId == 0U) {
        this->m_nextRequestId = 1U;
    }
    return result;
}

bool PayloadManager::isValidProduct(
    U32 productId,
    U32 productBytes,
    const Components::ScienceProductSource& sourceKind,
    const Fw::StringBase& sourcePath,
    U32 sourceCrc
) const {
    if ((productId == 0U) || (productBytes == 0U) ||
        (sourceKind == Components::ScienceProductSource::UNKNOWN) || (sourcePath.length() == 0U)) {
        return false;
    }
    struct stat info {};
    if ((::stat(sourcePath.toChar(), &info) != 0) || !S_ISREG(info.st_mode) ||
        (info.st_size <= 0) || (static_cast<U64>(info.st_size) != productBytes)) {
        return false;
    }
    std::FILE* file = std::fopen(sourcePath.toChar(), "rb");
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
    return readOk && (static_cast<U32>(crc) == sourceCrc);
}

bool PayloadManager::isValidCaptureDuration(U32 durationMinutes) const {
    return (durationMinutes > 0U) && (durationMinutes <= MAX_SIMULATED_CAPTURE_DURATION_MINUTES);
}

void PayloadManager::consumeSimulationFault() {
    this->m_simulationConfigured = false;
    this->m_nextFaultMode = Components::PayloadSimulationFault::NORMAL;
    this->m_nextDelayMs = 1000U;
    this->m_nextTimeoutMs = DEFAULT_OPERATION_TIMEOUT_MS;
}

}  // namespace Components
