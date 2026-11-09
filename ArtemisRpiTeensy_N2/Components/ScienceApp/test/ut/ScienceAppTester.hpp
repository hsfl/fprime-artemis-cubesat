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
#ifndef NEUTRON2_CONFIGURED_CAPTURE
    void testScienceCaptureDoesNotPersistDuration();
#endif
    void testConfiguredCaptureWaitsForMatchingDurationResult();
    void testConfiguredDurationFailureAllowsRetry();
    void testConfiguredDurationTimesOut();
    void testScheduledCaptureReturnsToBaseIfDurationMissingOrPending();
    void testParamSeedsDurationAtInit();
    void testInvalidParamFallsBackToThirty();
    void testCollectAndDownlinkWaitsForValidatedCompletion();
    void testCollectAndDownlinkClearsIntentOnFailure();
    void testCdrRejectsAutomaticDownlinkBeforeCaptureOrTokenAllocation();

  private:
    void connectPorts();
    void connectConfiguredCapturePorts();
    void initComponents();

    ScienceApp component;
};

}  // namespace Components

#endif
