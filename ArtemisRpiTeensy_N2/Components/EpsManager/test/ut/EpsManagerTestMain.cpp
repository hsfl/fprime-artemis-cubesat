#include "EpsManagerTester.hpp"

TEST(Nominal, CommandWaitsForTerminalResponseAndBusyDoesNotCompleteIt) {
    Components::EpsManagerTester tester;
    tester.testCommandWaitsForTerminalResponseAndBusyDoesNotCompleteIt();
}

TEST(Nominal, NegativeMalformedAndTimeoutStatusesFailCommand) {
    Components::EpsManagerTester tester;
    tester.testNegativeMalformedAndTimeoutStatusesFailCommand();
}

TEST(Nominal, NamedCommandsAndGuards) {
    Components::EpsManagerTester tester;
    tester.testNamedCommandsAndGuards();
}

TEST(Nominal, SnapshotStates) {
    Components::EpsManagerTester tester;
    tester.testSnapshotStates();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
