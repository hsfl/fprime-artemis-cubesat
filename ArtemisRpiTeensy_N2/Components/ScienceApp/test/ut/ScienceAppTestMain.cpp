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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
