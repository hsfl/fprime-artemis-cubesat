#include "MissionManagerTester.hpp"

TEST(Nominal, RejectsInvalidServiceTransition) {
    Components::MissionManagerTester tester;
    tester.testRejectsInvalidServiceTransition();
}

TEST(Nominal, AcceptsNominalDemoStoryTransitions) {
    Components::MissionManagerTester tester;
    tester.testAcceptsNominalDemoStoryTransitions();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
