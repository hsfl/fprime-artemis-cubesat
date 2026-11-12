#pragma once

#include "FprimeArtemisCore/Components/EpsManager/EpsManagerComponentAc.hpp"
#include "PduClient.hpp"

namespace Components {
class EpsManager final : public EpsManagerComponentBase {
  public:
    explicit EpsManager(const char* name);
    // run is configured at 100 Hz in the FC topology.
    void configure(U32 runPeriodMs = 10, U32 pollIntervalMs = 2000);
  private:
    enum class Phase : U8 { IDLE, PING, SUMMARY, SET, CYCLE_BEGIN, CYCLE_WAIT, CYCLE_VERIFY };
    void run_handler(FwIndexType, U32) override;
    void pingIn_handler(FwIndexType, U32) override;
    void drvReceiveIn_handler(FwIndexType, Fw::Buffer&, const Drv::ByteStreamStatus&) override;
    void drvConnected_handler(FwIndexType) override;
    void readingGet_handler(FwIndexType, EpsStatus&) override;
    void PING_cmdHandler(FwOpcodeType, U32) override;
    void GET_SUMMARY_STATUS_cmdHandler(FwOpcodeType, U32) override;
    void SET_OUTPUT_STATE_cmdHandler(FwOpcodeType, U32, U8, const Fw::On&) override;
    void POWER_CYCLE_OUTPUT_cmdHandler(FwOpcodeType, U32, U8, U16) override;
    bool command(FwOpcodeType opcode, U32 sequence, Phase phase);
    bool send(U8 opcode, const U8* payload = nullptr, FwSizeType length = 0);
    void consume();
    void finish(bool success, U32 reason = 0, bool protocolBusy = false, bool observed = false);
    void unknown(U8 output);
    void readback(U8 output, bool on);
    void publish();
    Artemis::Eps::PduClient client;
    Phase phase = Phase::IDLE;
    EpsLinkState linkState = EpsLinkState::UNKNOWN;
    U32 nowMs = 0, periodMs = 10, pollIntervalMs = 2000, nextPoll = 0, lastResponse = 0;
    U32 outputUpdatedAt[PDU_V2_OUTPUT_COUNT] = {};
    U16 knownOutputs = 0, outputBitmap = 0;
    U8 resetCause = 0, faults = 0, capabilities = 0, lastStatus = 0xFF;
    U32 uptimeSeconds = 0;
    bool connected = false, tracked = false, haveResponse = false;
    FwOpcodeType commandOpcode = 0;
    U32 commandSequence = 0;
    U8 requestedOutput = 0;
    bool requestedState = false;
    U16 offMs = 0;
    U32 cyclePollAt = 0, cycleDeadline = 0;
    U8 activeOpcode = 0;
};
}  // namespace Components
