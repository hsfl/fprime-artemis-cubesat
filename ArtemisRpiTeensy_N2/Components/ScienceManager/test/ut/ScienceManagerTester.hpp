#ifndef Components_ScienceManagerTester_HPP
#define Components_ScienceManagerTester_HPP

#include "Components/ScienceManager/ScienceManager.hpp"
#include "Components/ScienceManager/ScienceManagerGTestBase.hpp"

namespace Components {

class ScienceManagerTester final : public ScienceManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    ScienceManagerTester();
    ~ScienceManagerTester();

    void testDefaultDurationIsThirty();
    void testScheduledCountdownFiresExactlyOnce();
    void testRejectsZeroDelayAndBadDurations();
    void testCancelClearsCountdown();
    void testQueuedCancelAtFinalTickBoundaryPreventsCollection();
    void testScienceCaptureDoesNotPersistDuration();
    void testParamSeedsDurationAtInit();
    void testInvalidParamFallsBackToThirty();

  private:
    void connectPorts();
    void initComponents();

    ScienceManager component;
};

}  // namespace Components

#endif
