#ifndef Components_MissionManagerTester_HPP
#define Components_MissionManagerTester_HPP

#include "Components/MissionManager/MissionManager.hpp"
#include "Components/MissionManager/MissionManagerGTestBase.hpp"

namespace Components {

class MissionManagerTester final : public MissionManagerGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 32;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    static const FwSizeType TEST_INSTANCE_QUEUE_DEPTH = 10;

    MissionManagerTester();
    ~MissionManagerTester();

    void testRejectsInvalidServiceTransition();
    void testRejectsInvalidScheduleCommandInputs();
    void testRejectsInvalidCommandTransition();
    void testAcceptsNominalDemoStoryTransitions();
    void testAcceptsManualDownlinkRetryFromBase();
    void testEnterBaseModeCancelsPendingCollection();
    void testCancelCollectionReturnsBaseAndCancels();

  private:
    void connectPorts();
    void initComponents();

    MissionManager component;
};

}  // namespace Components

#endif
