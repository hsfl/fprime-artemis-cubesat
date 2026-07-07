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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
