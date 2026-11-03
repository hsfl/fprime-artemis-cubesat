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
    //! MissionApp's mode on every change. SAFE closes every capture path, any other mode opens them.
    void missionModeIn_handler(FwIndexType portNum, const Components::MissionMode& mode, U32 detail) override;
    void START_COLLECTION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void CONFIGURE_CAPTURE_DURATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void SCIENCE_CAPTURE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 durationSeconds) override;
    void VERIFIED_COLLECT_AND_DOWNLINK_cmdHandler(FwOpcodeType opCode, U32 cmdSeq,
                                                  U32 verificationToken, U32 durationSeconds) override;
    void parameterUpdated(FwPrmIdType id) override;
    void seedCaptureDurationFromParam();
    bool isValidCaptureDuration(U32 durationSeconds) const;
    void cancelPendingCollection();
    //! False, with a ScienceInhibited event, while SAFE inhibits science. Every capture path asks first.
    bool scienceAllowed(Components::ScienceInhibitSource source);
    bool startCapture(U32 durationSeconds, Components::ScienceInhibitSource source,
                      bool requestDownlink = false, U32* requestIdOut = nullptr);
    //! Tells MissionApp where science work has reached. Sends nothing in SAFE, where MissionApp holds anyway.
    void reportMissionMode(Components::MissionMode mode, U32 detail);
    //! Forgets the active capture once it has ended, however it ended.
    void clearActiveCapture();
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
    U32 m_verifiedCaptureCount;
    VerifiedCaptureRecord m_verifiedCaptures[VERIFIED_CAPTURE_CAPACITY];
    U32 m_activeCaptureRequestId;
    U32 m_activeCaptureGeneration;
    bool m_activeCaptureScienceRequested;
    //! The active capture ran during SAFE, so its product is withheld when it ends.
    bool m_activeCaptureInterrupted;
    static constexpr U32 DOWNLINK_INTENT_CAPACITY = 8U;
    U32 m_downlinkRequestIds[DOWNLINK_INTENT_CAPACITY];
    U32 m_downlinkRequestGenerations[DOWNLINK_INTENT_CAPACITY];
    //! True while MissionApp reports SAFE: every capture is refused (SDD v4.0 Table 5).
    bool m_scienceInhibited;
};

}  // namespace Components

#endif
