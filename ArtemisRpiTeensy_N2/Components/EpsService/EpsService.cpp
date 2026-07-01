#include "Components/EpsService/EpsService.hpp"

namespace Components {

EpsService::EpsService(const char* const compName)
    : EpsServiceComponentBase(compName),
      m_health(Components::HealthState::UNKNOWN),
      m_linkState(0),
      m_protocolVersion(0),
      m_outputBitmap(0),
      m_resetCause(0),
      m_faultBitmap(0),
      m_uptimeSeconds(0),
      m_capabilities(0),
      m_lastAdapterStatus(0),
      m_lastOpcode(0),
      m_serviceHeartbeat(0) {}

EpsService::~EpsService() {}

void EpsService::pingIn_handler(FwIndexType portNum, U32 key) {
    static_cast<void>(portNum);
    this->pingOut_out(0, key);
}

void EpsService::run_handler(FwIndexType portNum, U32 context) {
    static_cast<void>(portNum);
    static_cast<void>(context);
    this->m_serviceHeartbeat += 1;

    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->sendRequest(Components::EpsRequest::GET_SUMMARY_STATUS, 0, 0, 0);
    }
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_health, this->m_outputBitmap);
    }

    this->writeTelemetry();
}

void EpsService::adapterStatusIn_handler(
    FwIndexType portNum,
    const Components::HealthState& health,
    U8 linkState,
    U8 adapterProtocolVersion,
    U16 railStateBitmap,
    U8 resetCause,
    U8 faultBitmap,
    U32 uptimeSeconds,
    U8 capabilities,
    U8 adapterStatus,
    U8 lastAdapterOpcode
) {
    static_cast<void>(portNum);
    this->m_health = health;
    this->m_linkState = linkState;
    this->m_protocolVersion = adapterProtocolVersion;
    this->m_outputBitmap = railStateBitmap;
    this->m_resetCause = resetCause;
    this->m_faultBitmap = faultBitmap;
    this->m_uptimeSeconds = uptimeSeconds;
    this->m_capabilities = capabilities;
    this->m_lastAdapterStatus = adapterStatus;
    this->m_lastOpcode = lastAdapterOpcode;
    this->writeTelemetry();
    this->log_ACTIVITY_LO_EpsStatusUpdated(this->m_health, this->m_outputBitmap, this->m_faultBitmap);
    if (this->isConnected_sohStatusOut_OutputPort(0)) {
        this->sohStatusOut_out(0, this->m_health, this->m_outputBitmap);
    }
}

void EpsService::REQUEST_EPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->sendRequest(Components::EpsRequest::GET_SUMMARY_STATUS, 0, 0, 0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::PING_EPS_ADAPTER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->sendRequest(Components::EpsRequest::PING, 0, 0, 0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::REQUEST_EPS_ADAPTER_INFO_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->sendRequest(Components::EpsRequest::GET_PROTOCOL_INFO, 0, 0, 0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::REQUEST_EPS_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId) {
    if (outputId > MAX_U8_VALUE) {
        this->log_WARNING_LO_EpsCommandRejected(1, outputId);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->sendRequest(Components::EpsRequest::GET_OUTPUT_STATE, static_cast<U8>(outputId), 0, 0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::SET_EPS_RAIL_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId, U32 state, U32 confirm) {
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
    this->sendRequest(Components::EpsRequest::SET_OUTPUT_STATE, static_cast<U8>(outputId), static_cast<U8>(state), 0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::POWER_CYCLE_EPS_RAIL_cmdHandler(
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
    if (!this->isSafeOutputId(outputId) || (offMs > MAX_POWER_CYCLE_MS)) {
        this->log_WARNING_LO_EpsCommandRejected(4, outputId);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->sendRequest(
        Components::EpsRequest::POWER_CYCLE_OUTPUT,
        static_cast<U8>(outputId),
        0,
        static_cast<U16>(offMs));
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::REQUEST_CHARGER_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->sendRequest(Components::EpsRequest::GET_CHARGER_STATUS, 0, 0, 0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::SET_CHARGER_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 enable, U32 confirm) {
    if (confirm != CONFIRM_VALUE) {
        this->log_WARNING_LO_EpsCommandRejected(2, confirm);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    this->sendRequest(Components::EpsRequest::SET_CHARGER_STATE, 0, (enable == 0U) ? 0U : 1U, 0);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void EpsService::sendRequest(Components::EpsRequest request, U8 outputId, U8 state, U16 durationMs) {
    if (this->isConnected_adapterRequestOut_OutputPort(0)) {
        this->adapterRequestOut_out(0, request, outputId, state, durationMs);
    }
}

bool EpsService::isSafeOutputId(U32 outputId) const {
    // Safe public switched rails only. Burn-wire and H-bridge controls stay out
    // of the mission-operator command surface until dedicated HIL procedures exist.
    return (outputId >= 1U) && (outputId <= 8U) && (outputId != 6U);
}

void EpsService::writeTelemetry() {
    this->tlmWrite_EpsHealthState(this->m_health);
    this->tlmWrite_AdapterLinkState(this->m_linkState);
    this->tlmWrite_AdapterProtocolVersion(this->m_protocolVersion);
    this->tlmWrite_RailStateBitmap(this->m_outputBitmap);
    this->tlmWrite_AdapterResetCause(this->m_resetCause);
    this->tlmWrite_AdapterFaultBitmap(this->m_faultBitmap);
    this->tlmWrite_AdapterUptimeSeconds(this->m_uptimeSeconds);
    this->tlmWrite_LastAdapterStatus(this->m_lastAdapterStatus);
    this->tlmWrite_LastAdapterOpcode(this->m_lastOpcode);
    this->tlmWrite_ServiceHeartbeat(this->m_serviceHeartbeat);
}

}  // namespace Components
