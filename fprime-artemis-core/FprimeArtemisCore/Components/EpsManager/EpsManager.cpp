#include "EpsManager.hpp"

namespace Components {
using Artemis::Eps::Completion;
using Artemis::Eps::PduClient;
using Artemis::Eps::Result;

EpsManager::EpsManager(const char* name) : EpsManagerComponentBase(name) {}
void EpsManager::configure(U32 runPeriodMs, U32 pollingMs) {
    periodMs = runPeriodMs ? runPeriodMs : 10;
    pollIntervalMs = pollingMs;
}
void EpsManager::pingIn_handler(FwIndexType, U32 key) { pingOut_out(0, key); }
void EpsManager::drvConnected_handler(FwIndexType) { connected = true; }

void EpsManager::drvReceiveIn_handler(FwIndexType, Fw::Buffer& buffer, const Drv::ByteStreamStatus& status) {
    if (status == Drv::ByteStreamStatus::OP_OK) client.receive(buffer.getData(), buffer.getSize(), nowMs);
    else if (status != Drv::ByteStreamStatus::RECV_NO_DATA) {
        client.transportResult(false);
        linkState = EpsLinkState::FAULT;
        knownOutputs = 0;
    }
    drvReceiveReturnOut_out(0, buffer);
    consume();
}

bool EpsManager::command(FwOpcodeType opcode, U32 sequence, Phase requestedPhase) {
    if (phase != Phase::IDLE || client.pending()) {
        cmdResponse_out(opcode, sequence, Fw::CmdResponse::BUSY);
        return false;
    }
    if (!connected || !isConnected_drvSendOut_OutputPort(0)) {
        cmdResponse_out(opcode, sequence, Fw::CmdResponse::EXECUTION_ERROR);
        return false;
    }
    phase = requestedPhase;
    tracked = true;
    commandOpcode = opcode;
    commandSequence = sequence;
    return true;
}

bool EpsManager::send(U8 opcode, const U8* payload, FwSizeType length) {
    if (!client.request(opcode, payload, length, nowMs)) return false;
    activeOpcode = opcode;
    Fw::Buffer frame(const_cast<U8*>(client.requestBytes()), static_cast<FwSizeType>(client.requestLength()));
    client.transportResult(drvSendOut_out(0, frame) == Drv::ByteStreamStatus::OP_OK);
    return true;
}

void EpsManager::PING_cmdHandler(FwOpcodeType opcode, U32 sequence) {
    if (command(opcode, sequence, Phase::PING)) {
        if (!send(PDU_V2_OP_PING)) finish(false, 0x101);
        consume();
    }
}
void EpsManager::GET_SUMMARY_STATUS_cmdHandler(FwOpcodeType opcode, U32 sequence) {
    if (command(opcode, sequence, Phase::SUMMARY)) {
        if (!send(PDU_V2_OP_GET_SUMMARY_STATUS)) finish(false, 0x101);
        consume();
    }
}
void EpsManager::SET_OUTPUT_STATE_cmdHandler(FwOpcodeType opcode, U32 sequence, U8 output, const Fw::On& state) {
    if (!PduClient::normalRail(output) || (state != Fw::On::ON && state != Fw::On::OFF)) {
        cmdResponse_out(opcode, sequence, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!command(opcode, sequence, Phase::SET)) return;
    requestedOutput = output;
    requestedState = state == Fw::On::ON;
    unknown(output);
    const U8 payload[] = {output, static_cast<U8>(requestedState)};
    if (!send(PDU_V2_OP_SET_OUTPUT_STATE, payload, sizeof(payload))) finish(false, 0x101);
    consume();
}
void EpsManager::POWER_CYCLE_OUTPUT_cmdHandler(FwOpcodeType opcode, U32 sequence, U8 output, U16 durationMs) {
    if (!PduClient::normalRail(output) || durationMs == 0 || durationMs > PDU_V2_POWER_CYCLE_MAX_MS) {
        cmdResponse_out(opcode, sequence, Fw::CmdResponse::VALIDATION_ERROR);
        return;
    }
    if (!command(opcode, sequence, Phase::CYCLE_BEGIN)) return;
    requestedOutput = output;
    offMs = durationMs;
    unknown(output);
    const U8 payload[] = {output, static_cast<U8>(durationMs), static_cast<U8>(durationMs >> 8U)};
    if (!send(PDU_V2_OP_POWER_CYCLE_OUTPUT, payload, sizeof(payload))) finish(false, 0x101);
    consume();
}

void EpsManager::unknown(U8 output) {
    const int bit = PduClient::outputBit(output);
    if (bit >= 0) knownOutputs &= static_cast<U16>(~(1U << static_cast<unsigned>(bit)));
}
void EpsManager::readback(U8 output, bool on) {
    const int bit = PduClient::outputBit(output);
    if (bit < 0) return;
    const U16 mask = static_cast<U16>(1U << static_cast<unsigned>(bit));
    knownOutputs |= mask;
    if (on) outputBitmap |= mask;
    else outputBitmap &= static_cast<U16>(~mask);
    outputUpdatedAt[bit] = nowMs;
    log_ACTIVITY_HI_OutputReadback(output, on ? EpsOutputState::ON : EpsOutputState::OFF);
}

void EpsManager::finish(bool success, U32 reason, bool protocolBusy, bool observed) {
    if (!success) {
        if (!observed && (phase == Phase::SET || phase == Phase::CYCLE_BEGIN || phase == Phase::CYCLE_WAIT || phase == Phase::CYCLE_VERIFY))
            unknown(requestedOutput);
        log_WARNING_HI_OperationFailed(activeOpcode, reason);
    }
    phase = Phase::IDLE;
    nextPoll = nowMs + pollIntervalMs;
    if (tracked) {
        cmdResponse_out(commandOpcode, commandSequence,
                        success ? Fw::CmdResponse::OK : (protocolBusy ? Fw::CmdResponse::BUSY : Fw::CmdResponse::EXECUTION_ERROR));
        tracked = false;
    }
    publish();
}

void EpsManager::consume() {
    Result result;
    while (client.takeResult(result)) {
        if (result.completion != Completion::RESPONSE) {
            linkState = EpsLinkState::FAULT;
            knownOutputs = 0;
            finish(false, result.completion == Completion::TIMEOUT ? 0x102 : 0x103);
            continue;
        }
        linkState = EpsLinkState::RESPONDING;
        haveResponse = true;
        lastResponse = nowMs;
        lastStatus = result.status;
        log_ACTIVITY_LO_PduResponse(result.opcode, result.status);
        if (result.status != PDU_V2_STATUS_OK) {
            finish(false, result.status, result.status == PDU_V2_STATUS_BUSY);
            continue;
        }
        const U8* bytes = result.payload.data();
        if (phase == Phase::PING) {
            finish(result.length == PDU_V2_PING_LEN && bytes[0] == PDU_V2_VERSION, 0x104);
        } else if (phase == Phase::SUMMARY) {
            if (result.length != PDU_V2_SUMMARY_STATUS_LEN) { finish(false, 0x104); continue; }
            outputBitmap = static_cast<U16>(static_cast<U16>(bytes[0]) | static_cast<U16>(static_cast<U16>(bytes[1]) << 8U));
            knownOutputs = static_cast<U16>((1U << PDU_V2_OUTPUT_COUNT) - 1U);
            for (unsigned i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) outputUpdatedAt[i] = nowMs;
            resetCause = bytes[2]; faults = bytes[3]; capabilities = bytes[8];
            uptimeSeconds = static_cast<U32>(bytes[4]) | static_cast<U32>(bytes[5]) << 8U |
                            static_cast<U32>(bytes[6]) << 16U | static_cast<U32>(bytes[7]) << 24U;
            finish(true);
        } else if (phase == Phase::SET || phase == Phase::CYCLE_BEGIN || phase == Phase::CYCLE_VERIFY) {
            if (result.length != 2 || bytes[0] != requestedOutput || bytes[1] > 1) { finish(false, 0x104); continue; }
            readback(requestedOutput, bytes[1] == 1);
            if (phase == Phase::SET) {
                // A mismatched readback is an observed state, but not achieved intent.
                const bool achieved = (bytes[1] != 0) == requestedState;
                finish(achieved, 0x105, false, true);
            } else if (phase == Phase::CYCLE_BEGIN) {
                if (bytes[1] != 0) { finish(false, 0x105, false, true); continue; }
                log_ACTIVITY_HI_CycleStarted(requestedOutput, offMs);
                phase = Phase::CYCLE_WAIT;
                cyclePollAt = nowMs + offMs + 50U;
                cycleDeadline = cyclePollAt + 1500U;
                publish();
            } else if (bytes[1] == 1) {
                log_ACTIVITY_HI_CycleRestored(requestedOutput);
                finish(true);
            } else if (static_cast<I32>(nowMs - cycleDeadline) >= 0) {
                finish(false, 0x106, false, true);
            } else {
                phase = Phase::CYCLE_WAIT;
                cyclePollAt = nowMs + 100U;
            }
        } else finish(false, 0x107);
    }
}

void EpsManager::run_handler(FwIndexType, U32) {
    nowMs += periodMs;
    client.tick(nowMs);
    consume();
    if (phase == Phase::CYCLE_WAIT && static_cast<I32>(nowMs - cyclePollAt) >= 0) {
        const U8 payload[] = {requestedOutput};
        phase = Phase::CYCLE_VERIFY;
        if (!send(PDU_V2_OP_GET_OUTPUT_STATE, payload, sizeof(payload))) finish(false, 0x101);
        consume();
    } else if (phase == Phase::IDLE && connected && pollIntervalMs && static_cast<I32>(nowMs - nextPoll) >= 0) {
        phase = Phase::SUMMARY;
        if (!send(PDU_V2_OP_GET_SUMMARY_STATUS)) finish(false, 0x101);
        consume();
    }
    for (unsigned i = 0; i < PDU_V2_OUTPUT_COUNT; ++i) {
        if ((knownOutputs & (1U << i)) && static_cast<U32>(nowMs - outputUpdatedAt[i]) >= 5000)
            knownOutputs &= static_cast<U16>(~(1U << i));
    }
    if (haveResponse && linkState == EpsLinkState::RESPONDING && static_cast<U32>(nowMs - lastResponse) >= 5000)
        linkState = EpsLinkState::UNKNOWN;
    if (nowMs % 1000U < periodMs) publish();
}

void EpsManager::readingGet_handler(FwIndexType, EpsStatus& status) {
    status.set_linkState(linkState); status.set_knownOutputs(knownOutputs); status.set_outputBitmap(outputBitmap);
    status.set_resetCause(resetCause); status.set_faults(faults); status.set_uptimeSeconds(uptimeSeconds);
    status.set_capabilities(capabilities); status.set_busy(phase != Phase::IDLE);
}
void EpsManager::publish() {
    tlmWrite_LinkState(linkState); tlmWrite_KnownOutputs(knownOutputs); tlmWrite_OutputBitmap(outputBitmap);
    tlmWrite_ResetCause(resetCause); tlmWrite_FaultBitmap(faults); tlmWrite_UptimeSeconds(uptimeSeconds);
    tlmWrite_Capabilities(capabilities); tlmWrite_Busy(phase != Phase::IDLE);
    tlmWrite_CrcErrors(client.crcErrors()); tlmWrite_FramingErrors(client.framingErrors());
    tlmWrite_Timeouts(client.timeouts()); tlmWrite_LastProtocolStatus(lastStatus);
}
}  // namespace Components
