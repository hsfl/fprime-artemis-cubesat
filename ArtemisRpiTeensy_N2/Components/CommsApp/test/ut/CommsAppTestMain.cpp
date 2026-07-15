#include "CommsAppTester.hpp"

TEST(Nominal, RejectsDownlinkWithoutScience) {
    Components::CommsAppTester tester;
    tester.testRejectsDownlinkWithoutScience();
}

TEST(Nominal, RequestsScienceDownlinkAndCompletionClearsState) {
    Components::CommsAppTester tester;
    tester.testRequestsScienceDownlinkAndCompletionClearsState();
}

TEST(Nominal, DuplicateAndConflictingActiveRequestsAreGuarded) {
    Components::CommsAppTester tester;
    tester.testDuplicateAndConflictingActiveRequestsAreGuarded();
}

TEST(Nominal, NewPendingProductStartsAfterActiveTerminalStatus) {
    Components::CommsAppTester tester;
    tester.testNewPendingProductStartsAfterActiveTerminalStatus();
}

TEST(Nominal, StaleOrUncorrelatedStatusCannotEndActiveTransfer) {
    Components::CommsAppTester tester;
    tester.testStaleOrUncorrelatedStatusCannotEndActiveTransfer();
}

TEST(Nominal, DownlinkFailureReturnsBase) {
    Components::CommsAppTester tester;
    tester.testDownlinkFailureReturnsBase();
}

TEST(Nominal, IgnoresPayloadStatusWhenInactive) {
    Components::CommsAppTester tester;
    tester.testIgnoresPayloadStatusWhenInactive();
}

TEST(Nominal, DriverStatusPollingAndRssiPing) {
    Components::CommsAppTester tester;
    tester.testDriverStatusPollingAndRssiPing();
}

TEST(Nominal, RunPublishesHealthFromLinkState) {
    Components::CommsAppTester tester;
    tester.testRunPublishesHealthFromLinkState();
}

TEST(Reliability, InitializationFailureAndInvalidReissueDoNotWedge) {
    Components::CommsAppTester tester;
    tester.testInitializationFailureAndInvalidReissueDoNotWedge();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
