#include "MissionManagerTester.hpp"

TEST(Nominal, RejectsInvalidServiceTransition) {
    Components::MissionManagerTester tester;
    tester.testRejectsInvalidServiceTransition();
}

TEST(Nominal, RejectsInvalidScheduleCommandInputs) {
    Components::MissionManagerTester tester;
    tester.testRejectsInvalidScheduleCommandInputs();
}

TEST(Nominal, RejectsInvalidCommandTransition) {
    Components::MissionManagerTester tester;
    tester.testRejectsInvalidCommandTransition();
}

TEST(Nominal, AcceptsNominalDemoStoryTransitions) {
    Components::MissionManagerTester tester;
    tester.testAcceptsNominalDemoStoryTransitions();
}

TEST(Nominal, AcceptsManualDownlinkRetryFromBase) {
    Components::MissionManagerTester tester;
    tester.testAcceptsManualDownlinkRetryFromBase();
}

TEST(Nominal, EnterBaseModeCancelsPendingCollection) {
    Components::MissionManagerTester tester;
    tester.testEnterBaseModeCancelsPendingCollection();
}

TEST(Nominal, CancelCollectionReturnsBaseAndCancels) {
    Components::MissionManagerTester tester;
    tester.testCancelCollectionReturnsBaseAndCancels();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
