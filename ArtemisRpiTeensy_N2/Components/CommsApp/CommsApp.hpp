#ifndef Components_CommsApp_HPP
#define Components_CommsApp_HPP

#include "Components/CommsApp/CommsAppComponentAc.hpp"

#include <string>

namespace Components {

class CommsApp final : public CommsAppComponentBase {
  public:
    CommsApp(const char* const compName);
    ~CommsApp();

  private:
    friend class CommsAppTester;

    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void scienceReadyIn_handler(FwIndexType portNum,
                                U32 productId,
                                U32 productBytes,
                                const Components::ScienceProductSource& sourceKind,
                                const Fw::StringBase& sourcePath,
                                U32 sourceCrc) override;
    void driverStatusIn_handler(FwIndexType portNum,
                                const Components::RadioOperation& operation,
                                const Components::RadioRpcResult& result,
                                const Components::RadioState& state,
                                const Components::RadioFault& fault,
                                U8 bootFlags,
                                U8 rssiValid,
                                I32 rssiDbm,
                                U32 rssiAgeMs,
                                U32 initAttempts,
                                U32 rfRxPackets,
                                U32 rfTxPackets,
                                U32 rfTxDrops) override;
    void payloadDownlinkStatusIn_handler(
        FwIndexType portNum,
        U32 state,
        U32 transferId,
        U32 productId,
        U32 totalBytes,
        U32 packetsSent,
        U32 totalPackets,
        U32 lastError
    ) override;
    void REQUEST_SCIENCE_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void REQUEST_LINK_STATUS_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void PING_LINK_RSSI_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    bool requestDriver(const Components::RadioOperation& operation, U8 enabled);
    void scheduleRecovery();
    void emitRadioTelemetry();
    void emitRadioHealth();
    bool pendingRequestMatchesActive() const;
    void clearActiveDownlink();
    void emitDownlinkTelemetry();

    static constexpr U32 POLICY_TICK_SECONDS = 2U;
    static constexpr U32 READY_STATUS_POLL_TICKS = 30U;
    static constexpr U32 RETRY_COUNTDOWN_TLM_TICKS = 15U;

    bool m_desiredRadioEnabled;
    bool m_radioStatusKnown;
    bool m_driverRequestPending;
    Components::RadioOperation m_pendingRadioOperation;
    Components::RadioState m_radioState;
    Components::RadioFault m_radioFault;
    Components::RadioRpcResult m_radioRpcResult;
    U8 m_radioBootFlags;
    U8 m_rssiValid;
    I32 m_rssiDbm;
    U32 m_rssiAgeMs;
    U32 m_radioInitAttempts;
    U32 m_rfRxPackets;
    U32 m_rfTxPackets;
    U32 m_rfTxDrops;
    U32 m_radioRecoveryFailures;
    U32 m_radioRetryTicks;
    U32 m_readyStatusPollTicks;
    bool m_rssiPingPending;
    bool m_linkStatusPollPending;
    U32 m_pendingProductId;
    U32 m_pendingScienceBytes;
    Components::ScienceProductSource m_pendingSourceKind;
    Fw::String m_pendingSourcePath;
    U32 m_pendingSourceCrc;
    U32 m_linkPollCount;
    U32 m_activeDownlinkBytes;
    U32 m_activeProductId;
    Components::ScienceProductSource m_activeSourceKind;
    Fw::String m_activeSourcePath;
    U32 m_activeSourceCrc;
    U32 m_activeTransferId;
    U32 m_downlinkRequestDisposition;
    U32 m_lastPayloadDownlinkState;
    bool m_downlinkActive;
};

}  // namespace Components

#endif
