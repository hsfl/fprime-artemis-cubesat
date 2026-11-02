#ifndef Components_EpsManager_HPP
#define Components_EpsManager_HPP

#include "Components/EpsManager/EpsManagerComponentAc.hpp"
#include <Fw/Types/String.hpp>

namespace Components {

class EpsManager final : public EpsManagerComponentBase {
  public:
    EpsManager(const char* const compName);
    ~EpsManager();

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void driverStatusIn_handler(
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
    ) override;
    void railSnapshotIn_handler(FwIndexType portNum, U16 bitmap, U16 knownMask, U16 freshMask, U32 maximumAgeTicks) override;
    void GET_PDU_INFO_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void GET_PDU_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void GET_PDU_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::PduRail rail) override;
    void GET_ALL_PDU_RAILS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_PDU_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, Components::PduSwitchableRail rail, Components::PduDesiredState state, Components::PduConfirmation confirmation) override;
    Components::PduRailState railState(U8 bit) const;
    void REQUEST_EPS_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void PING_EPS_DRIVER_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REQUEST_EPS_DRIVER_INFO_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REQUEST_EPS_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId) override;
    void SET_EPS_RAIL_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId, U32 state, U32 confirm) override;
    void POWER_CYCLE_EPS_RAIL_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 outputId, U32 offMs, U32 confirm) override;
    void REQUEST_CHARGER_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_CHARGER_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 enable, U32 confirm) override;

    bool sendRequest(Components::EpsRequest request, U8 outputId, U8 state, U16 durationMs);
    void beginCommand(
        FwOpcodeType opCode,
        U32 cmdSeq,
        Components::EpsRequest request,
        U8 outputId,
        U8 state,
        U16 durationMs
    );
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
    U8 m_lastDriverStatus;
    U8 m_lastOpcode;
    Components::EpsRequest m_pendingRequest;
    bool m_requestPending;
    bool m_commandPending;
    FwOpcodeType m_pendingCommandOpcode;
    U32 m_pendingCommandSequence;
    U8 m_firmwareMajor;
    U8 m_firmwareMinor;
    U8 m_firmwarePatch;
    U8 m_buildFlags;
    Fw::String m_gitHash;
    U32 m_managerHeartbeat;
    U16 m_snapshotBitmap;
    U16 m_knownMask;
    U16 m_freshMask;
    U32 m_maximumAgeTicks;
};

}  // namespace Components

#endif
