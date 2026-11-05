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
    void testScienceReadyWaitsForStorageCommit();
    void testStorageFailureReturnsToBaseNotReady();
    void testStorageFailureSendsNoDelivery();
    void testInhibitedCaptureIsReportedAndChangesNoMode();
    void testCancelReportsBackToMissionApp();

  private:
    void connectPorts();
    void initComponents();
    void captureToStorage(U32 requestId, U32 productId, U32 generation);

    ScienceApp component;
};

}  // namespace Components

#endif
