#ifndef Components_MissionAppTester_HPP
#define Components_MissionAppTester_HPP

#include "Components/MissionApp/MissionApp.hpp"
#include "Components/MissionApp/MissionAppGTestBase.hpp"

namespace Components {

class MissionAppTester final : public MissionAppGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    MissionAppTester();
    ~MissionAppTester();

    void testRejectsInvalidManagerTransition();
    void testRejectsInvalidScheduleCommandInputs();
    void testRejectsInvalidCommandTransition();
    void testAcceptsNominalDemoStoryTransitions();
    void testAcceptsManualDownlinkRetryFromBase();
    void testEnterBaseModeCancelsPendingCollection();
    void testCancelCollectionReturnsBaseAndCancels();

  private:
    void connectPorts();
    void initComponents();

    MissionApp component;
};

}  // namespace Components

#endif
