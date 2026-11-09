#ifndef Components_ScienceApp_HPP
#define Components_ScienceApp_HPP

#include "Components/ScienceApp/ScienceAppComponentAc.hpp"

namespace Components {

class ScienceApp final : public ScienceAppComponentBase {
  public:
    ScienceApp(const char* const compName);
    ~ScienceApp();
    void preamble() override;

  private:
    void pingIn_handler(FwIndexType portNum, U32 key) override;
    void run_handler(FwIndexType portNum, U32 context) override;
    void requestIn_handler(FwIndexType portNum, U32 delaySeconds) override;
    void cancelRequestIn_handler(FwIndexType portNum, U32 key) override;
    void durationConfigResultIn_handler(
        FwIndexType portNum, U32 requestId, const Components::PayloadOperationState& state
    ) override;
    void operationNoticeIn_handler(
        FwIndexType portNum,
        U32 requestId,
        U32 generation,
        const Components::PayloadOperationKind& kind,
        const Components::PayloadOperationState& state,
        U32 productId,
        U32 productBytes,
        const Components::ScienceProductSource& sourceKind,
        const Fw::StringBase& sourcePath,
        U32 sourceCrc,
        bool scienceRequested
    ) override;
    void START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
#ifdef NEUTRON2_CONFIGURED_CAPTURE
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
#else
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
#endif
    void COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void VERIFIED_COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq,
                                                  U32 verificationToken, U32 durationSeconds) override;
    void parameterUpdated(FwPrmIdType id) override;
    void seedCaptureDurationFromParam();
    bool isValidCaptureDuration(U32 durationSeconds) const;
    void applyCaptureDuration(U32 durationSeconds);
    void finishDurationConfig(const Components::PayloadOperationState& state);
    void cancelPendingCollection();
    U32 allocateRequestId();
    bool startConfiguredCapture();
    bool startCapture(U32 durationSeconds, bool requestDownlink = false, U32* requestIdOut = nullptr);
    struct PendingDurationConfig {
        U32 requestId;
        U32 durationSeconds;
        FwOpcodeType opCode;
        U32 cmdSeq;
        U32 issuedTick;
    };
    struct VerifiedCaptureRecord {
        U32 verificationToken;
        U32 requestId;
        U32 generation;
        bool terminal;
    };
    static constexpr U32 VERIFIED_CAPTURE_CAPACITY = 32U;
    VerifiedCaptureRecord* findVerifiedCaptureByToken(U32 verificationToken);
    VerifiedCaptureRecord* findVerifiedCaptureByRequestId(U32 requestId);
    void logVerifiedCaptureStatus(U32 verificationToken, U32 requestId, U32 generation,
                                  const Components::PayloadOperationState& state, U32 productId,
                                  U32 productBytes, U32 sourceCrc, U32 reasonCode);
    bool rememberDownlinkIntent(U32 requestId);
    void setDownlinkIntentGeneration(U32 requestId, U32 generation);
    bool hasDownlinkIntent(U32 requestId, U32 generation) const;
    void clearDownlinkIntent(U32 requestId, U32 generation);
    void publishScienceProduct(U32 productId, U32 productBytes,
                               const Components::ScienceProductSource& sourceKind,
                               const Fw::StringBase& sourcePath, U32 sourceCrc);
    bool isValidProduct(U32 productId, U32 productBytes,
                        const Components::ScienceProductSource& sourceKind,
                        const Fw::StringBase& sourcePath, U32 sourceCrc) const;
    void writeTelemetry();

    U32 m_pendingDelaySeconds;
    U32 m_captureDurationSeconds;
    U32 m_collectionCount;
    U32 m_runTicks;
    U32 m_lastTelemetryTick;
    U32 m_nextRequestId;
    PendingDurationConfig m_pendingDurationConfig;
    U32 m_verifiedCaptureCount;
    VerifiedCaptureRecord m_verifiedCaptures[VERIFIED_CAPTURE_CAPACITY];
    U32 m_activeCaptureRequestId;
    U32 m_activeCaptureGeneration;
    bool m_activeCaptureScienceRequested;
    static constexpr U32 DOWNLINK_INTENT_CAPACITY = 8U;
    U32 m_downlinkRequestIds[DOWNLINK_INTENT_CAPACITY];
    U32 m_downlinkRequestGenerations[DOWNLINK_INTENT_CAPACITY];
};

}  // namespace Components

#endif
