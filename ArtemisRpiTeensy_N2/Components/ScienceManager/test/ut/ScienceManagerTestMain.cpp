#include "ScienceManagerTester.hpp"

TEST(Nominal, DefaultDurationIsThirty) {
    Components::ScienceManagerTester tester;
    tester.testDefaultDurationIsThirty();
}

TEST(Nominal, ScheduledCountdownFiresExactlyOnce) {
    Components::ScienceManagerTester tester;
    tester.testScheduledCountdownFiresExactlyOnce();
}

TEST(Nominal, RejectsZeroDelayAndBadDurations) {
    Components::ScienceManagerTester tester;
    tester.testRejectsZeroDelayAndBadDurations();
}

TEST(Nominal, CancelClearsCountdown) {
    Components::ScienceManagerTester tester;
    tester.testCancelClearsCountdown();
}

TEST(Nominal, QueuedCancelAtFinalTickBoundaryPreventsCollection) {
    Components::ScienceManagerTester tester;
    tester.testQueuedCancelAtFinalTickBoundaryPreventsCollection();
}

TEST(Nominal, ScienceCaptureDoesNotPersistDuration) {
    Components::ScienceManagerTester tester;
    tester.testScienceCaptureDoesNotPersistDuration();
}

TEST(Nominal, ParamSeedsDurationAtInit) {
    Components::ScienceManagerTester tester;
    tester.testParamSeedsDurationAtInit();
}

TEST(Nominal, InvalidParamFallsBackToThirty) {
    Components::ScienceManagerTester tester;
    tester.testInvalidParamFallsBackToThirty();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
