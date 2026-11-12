#pragma once
#include "FprimeArtemisCore/Components/ScienceCoordinator/ScienceCoordinator.hpp"
#include "FprimeArtemisCore/Components/ScienceCoordinator/ScienceCoordinatorGTestBase.hpp"
namespace Components {
class ScienceCoordinatorTester : public ScienceCoordinatorGTestBase {
  public:
    static const FwSizeType MAX_HISTORY_SIZE = 8192;
    static const FwEnumStoreType TEST_INSTANCE_ID = 0;
    ScienceCoordinatorTester();
    void admissionAndReadiness();
    void completeMaximumProduct();
    void corruptedProductRefused();
    void duplicateCommitDuringDownlink();
    void staleErrorAfterSend();
    void cancelRetainsPiIndependentCache();
    void lateProductAfterTimeout();
    void localRadioFailureIsBounded();
    void failedStageAbortsAndInvalidates();
    void newScheduleRetiresOldRepair();
    void explicitRestageAfterReset();
  private:
    void connectPorts();
    void initComponents();
    void tick(unsigned count = 1);
    void capture(TwinScience::Frame& request);
    void stage(TwinScience::Frame& commit, unsigned size = 9, bool corrupt = false);
    void drainRadio();
    ScienceCoordinator component;
};
}
