#ifndef Components_EpsService_HPP
#define Components_EpsService_HPP

#include "Components/EpsService/EpsServiceComponentAc.hpp"

namespace Components {

class EpsService final : public EpsServiceComponentBase {
  public:
    EpsService(const char* const compName);
    ~EpsService();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void adapterStatusIn_handler(
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
    ) override;
    void REQUEST_EPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void PING_EPS_ADAPTER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REQUEST_EPS_ADAPTER_INFO_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REQUEST_EPS_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId) override;
    void SET_EPS_RAIL_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId, U32 state, U32 confirm) override;
    void POWER_CYCLE_EPS_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId, U32 offMs, U32 confirm) override;
    void REQUEST_CHARGER_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_CHARGER_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 enable, U32 confirm) override;

    void sendRequest(Components::EpsRequest request, U8 outputId, U8 state, U16 durationMs);
    bool isSafeOutputId(U32 outputId) const;
    void writeTelemetry();

    static constexpr U32 CONFIRM_VALUE = 1;
    static constexpr U32 MAX_U8_VALUE = 0xFF;
    static constexpr U32 MAX_POWER_CYCLE_MS = 10000;
    Components::HealthState m_health;
    U8 m_linkState;
    U8 m_protocolVersion;
    U16 m_outputBitmap;
    U8 m_resetCause;
    U8 m_faultBitmap;
    U32 m_uptimeSeconds;
    U8 m_capabilities;
    U8 m_lastAdapterStatus;
    U8 m_lastOpcode;
    U32 m_serviceHeartbeat;
};

}  // namespace Components

#endif
