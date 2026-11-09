#include "ScienceAppTester.hpp"

TEST(Nominal, DefaultDurationIsOneMinute) {
    Components::ScienceAppTester tester;
    tester.testDefaultDurationIsOneMinute();
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

#ifndef NEUTRON2_CONFIGURED_CAPTURE
TEST(Nominal, ScienceCaptureDoesNotPersistDuration) {
    Components::ScienceAppTester tester;
    tester.testScienceCaptureDoesNotPersistDuration();
}
#endif

TEST(ConfiguredCapture, WaitsForMatchingDurationResult) {
    Components::ScienceAppTester tester;
    tester.testConfiguredCaptureWaitsForMatchingDurationResult();
}

TEST(ConfiguredCapture, DurationFailureAllowsRetry) {
    Components::ScienceAppTester tester;
    tester.testConfiguredDurationFailureAllowsRetry();
}

TEST(ConfiguredCapture, DurationTimesOut) {
    Components::ScienceAppTester tester;
    tester.testConfiguredDurationTimesOut();
}

TEST(ConfiguredCapture, ScheduledCaptureReturnsToBaseIfDurationMissingOrPending) {
    Components::ScienceAppTester tester;
    tester.testScheduledCaptureReturnsToBaseIfDurationMissingOrPending();
}

TEST(Nominal, ParamSeedsDurationAtInit) {
    Components::ScienceAppTester tester;
    tester.testParamSeedsDurationAtInit();
}

TEST(Nominal, InvalidParamFallsBackToOneMinute) {
    Components::ScienceAppTester tester;
    tester.testInvalidParamFallsBackToOneMinute();
}

TEST(Nominal, ScienceReadyWaitsForMatchingCommit) {
    Components::ScienceAppTester tester;
    tester.testScienceReadyWaitsForMatchingCommit();
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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
