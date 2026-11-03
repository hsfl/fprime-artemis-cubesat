#ifndef Components_ScienceAppTester_HPP
#define Components_ScienceAppTester_HPP

#include "Components/ScienceApp/ScienceApp.hpp"
#include "Components/ScienceApp/ScienceAppGTestBase.hpp"

namespace Components {

class ScienceAppTester final : public ScienceAppGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    ScienceAppTester();
    ~ScienceAppTester();

    void testDefaultDurationIsThirty();
    void testScheduledCountdownFiresExactlyOnce();
    void testRejectsZeroDelayAndBadDurations();
    void testCancelClearsCountdown();
    void testQueuedCancelAtFinalTickBoundaryPreventsCollection();
    void testScienceCaptureDoesNotPersistDuration();
    void testParamSeedsDurationAtInit();
    void testInvalidParamFallsBackToThirty();
    void testCollectAndDownlinkWaitsForValidatedCompletion();
    void testCollectAndDownlinkClearsIntentOnFailure();
    void testCdrRejectsAutomaticDownlinkBeforeCaptureOrTokenAllocation();

    // SAFE inhibits science (SDD v4.0 Table 5)
    void testSafeRefusesEveryCaptureCommand();
    void testSafeRefusesScheduledRequestAndCountdown();
    void testCapturesAllowedAgainAfterSafe();
    void testCaptureRunningAtSafeEntryIsWithheld();
    void testManagerCaptureInSafeIsWithheld();
    void testInterruptedCaptureStaysWithheldAfterSafeExit();
    void testVerifiedCaptureInterruptedBySafeEndsFailed();
    void testExistingIdsUnchanged();

  private:
    void connectPorts();
    void initComponents();
    //! Delivers one MissionApp mode update on missionModeIn and dispatches it.
    void setMissionMode(Components::MissionMode mode);
    //! One PayloadManager notice for a capture, dispatched.
    void notifyCapture(U32 requestId, U32 generation, Components::PayloadOperationState state, U32 productId,
                       U32 productBytes, U32 sourceCrc, bool scienceRequested);

    ScienceApp component;
};

}  // namespace Components

#endif
