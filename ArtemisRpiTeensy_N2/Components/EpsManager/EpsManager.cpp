#include "Components/EpsManager/EpsManager.hpp"

namespace Components {

namespace {
constexpr U8 LINK_REQUEST_QUEUED = 1U;
constexpr U8 LINK_PROTOCOL_OK = 2U;
}

EpsManager::EpsManager(const char* const compName)
    : EpsManagerComponentBase(compName),
      m_health(Components::HealthState::UNKNOWN),
      m_linkState(0),
      m_protocolVersion(0),
      m_outputBitmap(0),
      m_resetCause(0),
      m_faultBitmap(0),
      m_uptimeSeconds(0),
      m_capabilities(0),
      m_lastDriverStatus(0),
      m_lastOpcode(0),
      m_pendingRequest(Components::EpsRequest::GET_SUMMARY_STATUS),
      m_requestPending(false),
      m_commandPending(false),
      m_pendingCommandOpcode(0),
      m_pendingCommandSequence(0),
      m_firmwareMajor(0),
      m_firmwareMinor(0),
      m_firmwarePatch(0),
      m_buildFlags(0),
      m_gitHash("000000000000"),
      m_managerHeartbeat(0),
      m_snapshotBitmap(0),
      m_knownMask(0),
      m_freshMask(0),
      m_maximumAgeTicks(0) {}

EpsManager::~EpsManager() {}

void EpsManager::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void EpsManager::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_managerHeartbeat += 1;

    if (!this->m_requestPending) {
        this->sendRequest(Components::EpsRequest::GET_SUMMARY_STATUS, 0, 0, 0);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_health, this->m_outputBitmap);
    }

    this->writeTelemetry();
}

void EpsManager::driverStatusIn_handler(
    FwIndexType portNum,
    const Components::HealthState& health,
    U8 linkState,
    U8 driverProtocolVersion,
    U16 railStateBitmap,
    U8 resetCause,
    U8 faultBitmap,
    U32 uptimeSeconds,
    U8 capabilities,
    U8 driverStatus,
    U8 lastDriverOpcode,
    const Components::EpsRequest& driverRequest,
    U8 firmwareMajor,
    U8 firmwareMinor,
    U8 firmwarePatch,
    U8 buildFlags,
    const Fw::StringBase& gitHash
) {
    static_cast<void>(portNum);
    this->m_health = health;
    this->m_linkState = linkState;
    this->m_protocolVersion = driverProtocolVersion;
    this->m_outputBitmap = railStateBitmap;
    this->m_resetCause = resetCause;
    this->m_faultBitmap = faultBitmap;
    this->m_uptimeSeconds = uptimeSeconds;
    this->m_capabilities = capabilities;
    this->m_lastDriverStatus = driverStatus;
    this->m_lastOpcode = lastDriverOpcode;
    this->m_firmwareMajor = firmwareMajor;
    this->m_firmwareMinor = firmwareMinor;
    this->m_firmwarePatch = firmwarePatch;
    this->m_buildFlags = buildFlags;
    this->m_gitHash = gitHash.toChar();

    FwOpcodeType completedCommandOpcode = 0;
    U32 completedCommandSequence = 0;
    bool respond = false;
    if (this->m_requestPending && (driverRequest == this->m_pendingRequest) &&
        (linkState != LINK_REQUEST_QUEUED)) {
        this->m_requestPending = false;
        if (this->m_commandPending) {
            completedCommandOpcode = this->m_pendingCommandOpcode;
            completedCommandSequence = this->m_pendingCommandSequence;
            this->m_commandPending = false;
            respond = true;
        }
    }

    this->writeTelemetry();
    this->log_ACTIVITY_LO_EpsStatusUpdated(this->m_health, this->m_outputBitmap, this->m_faultBitmap);
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_health, this->m_outputBitmap);
    }
    if (respond) {
        const Fw::CmdResponse response = ((linkState == LINK_PROTOCOL_OK) && (driverStatus == 0U)) ?
                                             Fw::CmdResponse::OK : Fw::CmdResponse::EXECUTION_ERROR;
        if (response == Fw::CmdResponse::OK) {
            if (driverRequest == Components::EpsRequest::GET_SUMMARY_STATUS) {
                const Components::PduRail::T rails[] = {Components::PduRail::RAIL_3V3_1, Components::PduRail::RAIL_3V3_2,
                    Components::PduRail::RAIL_5V_1, Components::PduRail::RAIL_5V_2, Components::PduRail::RAIL_5V_3,
                    Components::PduRail::RAIL_12V, Components::PduRail::RAIL_VBATT, Components::PduRail::RAIL_BURN1, Components::PduRail::RAIL_BURN2};
                for (U8 i = 0; i < 9U; ++i) {
                    const Components::PduRailState state = (railStateBitmap & (1U << i)) ? Components::PduRailState::ON : Components::PduRailState::OFF;
                    this->log_ACTIVITY_HI_PduSummaryRail(completedCommandSequence, completedCommandOpcode, rails[i], state);
                }
            } else if (driverRequest == Components::EpsRequest::GET_PROTOCOL_INFO) {
                this->log_ACTIVITY_HI_PduInfoResult(completedCommandSequence, completedCommandOpcode, driverProtocolVersion,
                    firmwareMajor, firmwareMinor, firmwarePatch, buildFlags, gitHash);
            }
        } else {
            this->log_WARNING_LO_PduCommandFailed(completedCommandSequence, completedCommandOpcode, linkState, driverStatus);
        }
        this->cmdResponse_out(completedCommandOpcode, completedCommandSequence, response);
    }
}

void EpsManager::railSnapshotIn_handler(FwIndexType portNum, U16 bitmap, U16 knownMask, U16 freshMask, U32 maximumAgeTicks) {
    static_cast<void>(portNum);
    this->m_snapshotBitmap = bitmap;
    this->m_knownMask = knownMask;
    this->m_freshMask = freshMask;
    this->m_maximumAgeTicks = maximumAgeTicks;
    this->writeTelemetry();
}

Components::PduRailState EpsManager::railState(U8 bit) const {
    const U16 mask = static_cast<U16>(1U << bit);
    if ((this->m_knownMask & mask) == 0U) return Components::PduRailState::UNKNOWN;
    if ((this->m_freshMask & mask) == 0U) return Components::PduRailState::STALE;
    return (this->m_snapshotBitmap & mask) ? Components::PduRailState::ON : Components::PduRailState::OFF;
}

void EpsManager::GET_PDU_INFO_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
}
void EpsManager::GET_PDU_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::GET_SUMMARY_STATUS, 0, 0, 0);
}
void EpsManager::GET_PDU_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::PduRail rail) {
    const U8 id = static_cast<U8>(rail);
    if (!rail.isValid() || !((id >= 1U && id <= 5U) || (id >= 7U && id <= 10U))) {
        this->log_WARNING_LO_PduCommandRejected(cmdSeq, opCode, Fw::String("INVALID_RAIL"));
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::GET_OUTPUT_STATE, id, 0, 0);
}
void EpsManager::GET_ALL_PDU_RAILS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::GET_OUTPUT_STATE, 255U, 0, 0);
}
void EpsManager::SET_PDU_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::PduSwitchableRail rail, Components::PduDesiredState state, Components::PduConfirmation confirmation) {
    const U8 id = static_cast<U8>(rail);
    if (!rail.isValid() || !state.isValid() || !confirmation.isValid() || !this->isSafeOutputId(id) || confirmation != Components::PduConfirmation::CONFIRM) {
        this->log_WARNING_LO_PduCommandRejected(cmdSeq, opCode, Fw::String("INVALID_OR_CANCELLED"));
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::SET_OUTPUT_STATE, id, static_cast<U8>(state), 0);
}

void EpsManager::REQUEST_EPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::GET_SUMMARY_STATUS, 0, 0, 0);
}

void EpsManager::PING_EPS_DRIVER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::PING, 0, 0, 0);
}

void EpsManager::REQUEST_EPS_DRIVER_INFO_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
}

void EpsManager::REQUEST_EPS_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId) {
    if (outputId > MAX_U8_VALUE) {
        this->log_WARNING_LO_EpsCommandRejected(1, outputId);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->beginCommand(
        opCode,
        cmdSeq,
        Components::EpsRequest::GET_OUTPUT_STATE,
        static_cast<U8>(outputId),
        0,
        0);
}

void EpsManager::SET_EPS_RAIL_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId, U32 state, U32 confirm) {
    if (confirm != CONFIRM_VALUE) {
        this->log_WARNING_LO_EpsCommandRejected(2, confirm);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!this->isSafeOutputId(outputId) || (state > 1U)) {
        this->log_WARNING_LO_EpsCommandRejected(3, outputId);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->beginCommand(
        opCode,
        cmdSeq,
        Components::EpsRequest::SET_OUTPUT_STATE,
        static_cast<U8>(outputId),
        static_cast<U8>(state),
        0);
}

void EpsManager::POWER_CYCLE_EPS_RAIL_cmdHandler(
    FwOpcodeType opCode,
    U32 cmdSeq,
    U32 outputId,
    U32 offMs,
    U32 confirm
) {
    if (confirm != CONFIRM_VALUE) {
        this->log_WARNING_LO_EpsCommandRejected(2, confirm);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!this->isSafeOutputId(outputId) || (offMs == 0U) || (offMs > MAX_POWER_CYCLE_MS)) {
        this->log_WARNING_LO_EpsCommandRejected(4, outputId);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->beginCommand(
        opCode,
        cmdSeq,
        Components::EpsRequest::POWER_CYCLE_OUTPUT,
        static_cast<U8>(outputId),
        0,
        static_cast<U16>(offMs));
}

void EpsManager::REQUEST_CHARGER_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->beginCommand(opCode, cmdSeq, Components::EpsRequest::GET_CHARGER_STATUS, 0, 0, 0);
}

void EpsManager::SET_CHARGER_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 enable, U32 confirm) {
    if (confirm != CONFIRM_VALUE) {
        this->log_WARNING_LO_EpsCommandRejected(2, confirm);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (enable > 1U) {
        this->log_WARNING_LO_EpsCommandRejected(5, enable);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->beginCommand(
        opCode,
        cmdSeq,
        Components::EpsRequest::SET_CHARGER_STATE,
        0,
        static_cast<U8>(enable),
        0);
}

bool EpsManager::sendRequest(Components::EpsRequest request, U8 outputId, U8 state, U16 durationMs) {
    if (this->m_requestPending || !this->isConnected_driverRequestOut_OutputPort(0)) {
        return false;
    }
    this->m_requestPending = true;
    this->m_pendingRequest = request;
    this->driverRequestOut_out(0, request, outputId, state, durationMs);
    return true;
}

void EpsManager::beginCommand(
    FwOpcodeType opCode,
    U32 cmdSeq,
    Components::EpsRequest request,
    U8 outputId,
    U8 state,
    U16 durationMs
) {
    if (this->m_requestPending || this->m_commandPending) {
        this->log_WARNING_LO_PduCommandRejected(cmdSeq, opCode, Fw::String("BUSY"));
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::BUSY);
        return;
    }
    if (!this->isConnected_driverRequestOut_OutputPort(0)) {
        this->log_WARNING_LO_PduCommandRejected(cmdSeq, opCode, Fw::String("DRIVER_UNAVAILABLE"));
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
        return;
    }

    // Reserve identity before the synchronous driver call can queue a failure status.
    this->m_commandPending = true;
    this->m_pendingCommandOpcode = opCode;
    this->m_pendingCommandSequence = cmdSeq;
    if (!this->sendRequest(request, outputId, state, durationMs)) {
        this->m_commandPending = false;
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    }
}

bool EpsManager::isSafeOutputId(U32 outputId) const {
    // Safe public switched rails only. Burn-wire and H-bridge controls stay out
    // of the mission-operator command surface until dedicated HIL procedures exist.
    return (outputId >= 1U) && (outputId <= 8U) && (outputId != 6U);
}

void EpsManager::writeTelemetry() {
    this->tlmWrite_EpsHealthState(this->m_health);
    this->tlmWrite_DriverLinkState(this->m_linkState);
    this->tlmWrite_DriverProtocolVersion(this->m_protocolVersion);
    this->tlmWrite_RailStateBitmap(this->m_outputBitmap);
    this->tlmWrite_DriverResetCause(this->m_resetCause);
    this->tlmWrite_DriverFaultBitmap(this->m_faultBitmap);
    this->tlmWrite_DriverUptimeSeconds(this->m_uptimeSeconds);
    this->tlmWrite_LastDriverStatus(this->m_lastDriverStatus);
    this->tlmWrite_LastDriverOpcode(this->m_lastOpcode);
    this->tlmWrite_ManagerHeartbeat(this->m_managerHeartbeat);
    this->tlmWrite_DriverFirmwareMajor(this->m_firmwareMajor);
    this->tlmWrite_DriverFirmwareMinor(this->m_firmwareMinor);
    this->tlmWrite_DriverFirmwarePatch(this->m_firmwarePatch);
    this->tlmWrite_DriverBuildFlags(this->m_buildFlags);
    this->tlmWrite_DriverGitHash(this->m_gitHash);
    this->tlmWrite_Rail3V3_1(this->railState(0));
    this->tlmWrite_Rail3V3_2(this->railState(1));
    this->tlmWrite_Rail5V_1(this->railState(2));
    this->tlmWrite_Rail5V_2(this->railState(3));
    this->tlmWrite_Rail5V_3(this->railState(4));
    this->tlmWrite_Rail12V(this->railState(5));
    this->tlmWrite_RailVBatt(this->railState(6));
    this->tlmWrite_RailBurn1(this->railState(7));
    this->tlmWrite_RailBurn2(this->railState(8));
    this->tlmWrite_RailKnownMask(this->m_knownMask);
    this->tlmWrite_RailFreshMask(this->m_freshMask);
    this->tlmWrite_RailMaximumAgeTicks(this->m_maximumAgeTicks);
}

}  // namespace Components
