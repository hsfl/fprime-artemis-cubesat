#ifndef Components_CommsManagerTester_HPP
#define Components_CommsManagerTester_HPP

#include "Components/CommsManager/CommsManager.hpp"
#include "Components/CommsManager/CommsManagerGTestBase.hpp"

namespace Components {

class CommsManagerTester final : public CommsManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    CommsManagerTester();
    ~CommsManagerTester();

    void testRejectsDownlinkWithoutScience();
    void testRequestsScienceDownlinkAndCompletionClearsState();
    void testDownlinkFailureReturnsBase();
    void testIgnoresPayloadStatusWhenInactive();
    void testAdapterStatusPollingAndRssiPing();
    void testRunPublishesHealthFromLinkState();

  private:
    void connectPorts();
    void initComponents();

    CommsManager component;
};

}  // namespace Components

#endif
