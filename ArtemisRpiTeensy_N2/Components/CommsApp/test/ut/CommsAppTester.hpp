#ifndef Components_CommsAppTester_HPP
#define Components_CommsAppTester_HPP

#include "Components/CommsApp/CommsApp.hpp"
#include "Components/CommsApp/CommsAppGTestBase.hpp"

namespace Components {

class CommsAppTester final : public CommsAppGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    CommsAppTester();
    ~CommsAppTester();

    void testRejectsDownlinkWithoutScience();
    void testCompletedDownlinkRemainsAvailableForRetry();
    void testDuplicateAndConflictingActiveRequestsAreGuarded();
    void testNewPendingProductStartsAfterActiveTerminalStatus();
    void testStaleOrUncorrelatedStatusCannotEndActiveTransfer();
    void testDownlinkFailureReturnsBase();
    void testIgnoresPayloadStatusWhenInactive();
    void testDriverStatusPollingAndRssiPing();
    void testBootReconcilesOffToReadyAndPublishesHealth();
    void testReadyWithLocalFaultIsDegradedAndReinitialized();
    void testRecoveryBackoffIsCappedAndStatusIsObservational();
    void testInitializationFailureAndInvalidReissueDoNotWedge();

  private:
    void connectPorts();
    void initComponents();

    CommsApp component;
};

}  // namespace Components

#endif
