#include "MissionAppTester.hpp"

TEST(Nominal, RejectsInvalidManagerTransition) {
    Components::MissionAppTester tester;
    tester.testRejectsInvalidManagerTransition();
}

TEST(Nominal, RejectsInvalidScheduleCommandInputs) {
    Components::MissionAppTester tester;
    tester.testRejectsInvalidScheduleCommandInputs();
}

TEST(Nominal, RejectsInvalidCommandTransition) {
    Components::MissionAppTester tester;
    tester.testRejectsInvalidCommandTransition();
}

TEST(Nominal, AcceptsNominalDemoStoryTransitions) {
    Components::MissionAppTester tester;
    tester.testAcceptsNominalDemoStoryTransitions();
}

TEST(Nominal, AcceptsManualDownlinkRetryFromBase) {
    Components::MissionAppTester tester;
    tester.testAcceptsManualDownlinkRetryFromBase();
}

TEST(Nominal, EnterBaseModeCancelsPendingCollection) {
    Components::MissionAppTester tester;
    tester.testEnterBaseModeCancelsPendingCollection();
}

TEST(Nominal, CancelCollectionReturnsBaseAndCancels) {
    Components::MissionAppTester tester;
    tester.testCancelCollectionReturnsBaseAndCancels();
}

TEST(Nominal, ReportModeAnswersOnceAndChangesNothing) {
    Components::MissionAppTester tester;
    tester.testReportModeAnswersOnceAndChangesNothing();
}

TEST(Nominal, ReportModeTracksEveryDemoStoryMode) {
    Components::MissionAppTester tester;
    tester.testReportModeTracksEveryDemoStoryMode();
}

TEST(TransitionTable, SubsystemReports) {
    Components::MissionAppTester tester;
    tester.testTransitionTableSubsystemReports();
}

TEST(TransitionTable, GroundSetMode) {
    Components::MissionAppTester tester;
    tester.testTransitionTableGroundSetMode();
}

TEST(TransitionTable, Faults) {
    Components::MissionAppTester tester;
    tester.testTransitionTableFaults();
}

TEST(SafeMode, ExitBlockedByLatchUntilCleared) {
    Components::MissionAppTester tester;
    tester.testSafeExitBlockedByLatchUntilCleared();
}

TEST(SafeMode, ExitWaitsForStabilityInterval) {
    Components::MissionAppTester tester;
    tester.testSafeExitWaitsForStabilityInterval();
}

TEST(SafeMode, ExitIntervalUsesParameter) {
    Components::MissionAppTester tester;
    tester.testSafeExitIntervalUsesParameter();
}

TEST(SafeMode, CaptureFinishingInSafeDoesNotLeaveSafe) {
    Components::MissionAppTester tester;
    tester.testCaptureFinishingInSafeDoesNotLeaveSafe();
}

TEST(SafeMode, OldCommandsRespectSafe) {
    Components::MissionAppTester tester;
    tester.testOldCommandsRespectSafe();
}

TEST(SafeMode, EntryCancelsQueuedCollection) {
    Components::MissionAppTester tester;
    tester.testSafeEntryCancelsQueuedCollection();
}

TEST(SafeMode, BootInSafeParameter) {
    Components::MissionAppTester tester;
    tester.testBootInSafeParameter();
}

TEST(SafeMode, BootInBaseByDefault) {
    Components::MissionAppTester tester;
    tester.testBootInBaseByDefault();
}

TEST(SafeMode, SimulateSafeFaultOnlyInLocalBuild) {
    Components::MissionAppTester tester;
    tester.testSimulateSafeFaultOnlyInLocalBuild();
}

TEST(SafeMode, ModeSentToScienceAppOnEveryChange) {
    Components::MissionAppTester tester;
    tester.testModeSentOnEveryChange();
}

TEST(DemoModes, SubsystemPlanMatchesMopMatrix) {
    Components::MissionAppTester tester;
    tester.testSubsystemPlanMatchesMopMatrix();
}

TEST(DemoModes, OperationsModeTelemetryFollowsMode) {
    Components::MissionAppTester tester;
    tester.testOperationsModeTelemetryFollowsMode();
}

TEST(DemoModes, FullDemoCycleWithModes) {
    Components::MissionAppTester tester;
    tester.testFullDemoCycleWithModes();
}

TEST(DemoModes, ReportModeAnswersSafe) {
    Components::MissionAppTester tester;
    tester.testReportModeAnswersSafe();
}

TEST(DemoModes, ExistingIdsUnchanged) {
    Components::MissionAppTester tester;
    tester.testExistingIdsUnchanged();
}

TEST(AutoReturn, OffHoldsScienceReady) {
    Components::MissionAppTester tester;
    tester.testAutoReturnOffHoldsScienceReady();
}

TEST(AutoReturn, OnGoesBackToBase) {
    Components::MissionAppTester tester;
    tester.testAutoReturnOnGoesBackToBase();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
