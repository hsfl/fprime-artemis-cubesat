#include "ScienceAppTester.hpp"

TEST(Nominal, DefaultDurationIsThirty) {
    Components::ScienceAppTester tester;
    tester.testDefaultDurationIsThirty();
}

TEST(Nominal, ScheduledCountdownFiresExactlyOnce) {
    Components::ScienceAppTester tester;
    tester.testScheduledCountdownFiresExactlyOnce();
}

TEST(Nominal, RejectsZeroDelayAndBadDurations) {
    Components::ScienceAppTester tester;
    tester.testRejectsZeroDelayAndBadDurations();
}

TEST(Nominal, CancelClearsCountdown) {
    Components::ScienceAppTester tester;
    tester.testCancelClearsCountdown();
}

TEST(Nominal, QueuedCancelAtFinalTickBoundaryPreventsCollection) {
    Components::ScienceAppTester tester;
    tester.testQueuedCancelAtFinalTickBoundaryPreventsCollection();
}

TEST(Nominal, ScienceCaptureDoesNotPersistDuration) {
    Components::ScienceAppTester tester;
    tester.testScienceCaptureDoesNotPersistDuration();
}

TEST(Nominal, ParamSeedsDurationAtInit) {
    Components::ScienceAppTester tester;
    tester.testParamSeedsDurationAtInit();
}

TEST(Nominal, InvalidParamFallsBackToThirty) {
    Components::ScienceAppTester tester;
    tester.testInvalidParamFallsBackToThirty();
}

#ifndef NEUTRON2_CDR_TCP
TEST(Nominal, CollectAndDownlinkWaitsForValidatedCompletion) {
    Components::ScienceAppTester tester;
    tester.testCollectAndDownlinkWaitsForValidatedCompletion();
}

TEST(Nominal, CollectAndDownlinkClearsIntentOnFailure) {
    Components::ScienceAppTester tester;
    tester.testCollectAndDownlinkClearsIntentOnFailure();
}
#else
TEST(Nominal, CdrRejectsAutomaticDownlinkBeforeCaptureOrTokenAllocation) {
    Components::ScienceAppTester tester;
    tester.testCdrRejectsAutomaticDownlinkBeforeCaptureOrTokenAllocation();
}
#endif

TEST(SafeMode, RefusesEveryCaptureCommand) {
    Components::ScienceAppTester tester;
    tester.testSafeRefusesEveryCaptureCommand();
}

TEST(SafeMode, RefusesScheduledRequestAndCountdown) {
    Components::ScienceAppTester tester;
    tester.testSafeRefusesScheduledRequestAndCountdown();
}

TEST(SafeMode, CapturesAllowedAgainAfterSafe) {
    Components::ScienceAppTester tester;
    tester.testCapturesAllowedAgainAfterSafe();
}

TEST(SafeMode, CaptureRunningAtSafeEntryIsWithheld) {
    Components::ScienceAppTester tester;
    tester.testCaptureRunningAtSafeEntryIsWithheld();
}

TEST(SafeMode, ManagerCaptureInSafeIsWithheld) {
    Components::ScienceAppTester tester;
    tester.testManagerCaptureInSafeIsWithheld();
}

TEST(SafeMode, InterruptedCaptureStaysWithheldAfterSafeExit) {
    Components::ScienceAppTester tester;
    tester.testInterruptedCaptureStaysWithheldAfterSafeExit();
}

#ifndef NEUTRON2_CDR_TCP
TEST(SafeMode, VerifiedCaptureInterruptedBySafeEndsFailed) {
    Components::ScienceAppTester tester;
    tester.testVerifiedCaptureInterruptedBySafeEndsFailed();
}
#endif

TEST(SafeMode, ExistingIdsUnchanged) {
    Components::ScienceAppTester tester;
    tester.testExistingIdsUnchanged();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
