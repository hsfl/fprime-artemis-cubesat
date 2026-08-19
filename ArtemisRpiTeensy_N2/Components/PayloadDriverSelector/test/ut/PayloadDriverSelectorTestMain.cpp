#include "PayloadDriverSelectorTester.hpp"

TEST(Nominal, LeptonDefaultAndCompletion) {
    Components::PayloadDriverSelectorTester tester;
    tester.testLeptonDefaultAndCompletion();
}

TEST(Nominal, BosonSelectionAndCompletion) {
    Components::PayloadDriverSelectorTester tester;
    tester.testBosonSelectionAndCompletion();
}

TEST(Guard, SelectionRejectedWhileBusy) {
    Components::PayloadDriverSelectorTester tester;
    tester.testSelectionRejectedWhileBusy();
}

TEST(Guard, DuplicateRequestDoesNotTerminateActiveCapture) {
    Components::PayloadDriverSelectorTester tester;
    tester.testDuplicateRequestDoesNotTerminateActiveCapture();
}

TEST(Guard, NonSelectedStatusIgnored) {
    Components::PayloadDriverSelectorTester tester;
    tester.testNonSelectedStatusIgnored();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
