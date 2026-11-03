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
    void testReportModeAnswersOnceAndChangesNothing();
    void testReportModeTracksEveryDemoStoryMode();

    // D5 demo modes: the transition table, every row and column
    void testTransitionTableSubsystemReports();
    void testTransitionTableGroundSetMode();
    void testTransitionTableFaults();
    // D5 demo modes: SAFE guards
    void testSafeExitBlockedByLatchUntilCleared();
    void testSafeExitWaitsForStabilityInterval();
    void testSafeExitIntervalUsesParameter();
    void testCaptureFinishingInSafeDoesNotLeaveSafe();
    void testOldCommandsRespectSafe();
    void testSafeEntryCancelsQueuedCollection();
    void testBootInSafeParameter();
    void testBootInBaseByDefault();
    void testSimulateSafeFaultOnlyInLocalBuild();
    //! ScienceApp hears every accepted mode change, so it can refuse captures in SAFE.
    void testModeSentOnEveryChange();
    // D5 demo modes: what each mode reports
    void testSubsystemPlanMatchesMopMatrix();
    void testOperationsModeTelemetryFollowsMode();
    void testFullDemoCycleWithModes();
    void testReportModeAnswersSafe();
    void testExistingIdsUnchanged();
    // MVP deck step 3.4: AUTO_RETURN_TO_BASE
    void testAutoReturnOffHoldsScienceReady();
    void testAutoReturnOnGoesBackToBase();

  private:
    void connectPorts();
    void initComponents();
    //! Sends REPORT_MODE and checks the answer is `expected`, with no side effects.
    void assertReportModeAnswers(U32 cmdSeq, Components::MissionMode expected);
    //! Reaches `mode` the way the real system does (commands and reports), then clears history.
    void driveTo(Components::MissionMode mode);
    //! One cell of the subsystem-report table: drive to `from`, report `requested`, check the result.
    void checkSubsystemReport(Components::MissionMode from, Components::MissionMode requested, bool allowed);
    //! One cell of the SET_MODE table.
    void checkSetMode(Components::MissionMode from,
                      Components::OperationsMode target,
                      Components::ModeRejectReason expected);
    //! One row of the fault table.
    void checkFault(Components::MissionMode from);
    //! Sends SET_MODE and checks only the command response.
    void sendSetMode(U32 cmdSeq, Components::OperationsMode target, Fw::CmdResponse expected);
    //! Runs `ticks` 1 Hz rate-group calls.
    void runTicks(U32 ticks);

    MissionApp component;
};

}  // namespace Components

#endif
