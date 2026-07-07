#include "CommsManagerTester.hpp"

TEST(Nominal, RejectsDownlinkWithoutScience) {
    Components::CommsManagerTester tester;
    tester.testRejectsDownlinkWithoutScience();
}

TEST(Nominal, RequestsScienceDownlinkAndCompletionClearsState) {
    Components::CommsManagerTester tester;
    tester.testRequestsScienceDownlinkAndCompletionClearsState();
}

TEST(Nominal, DownlinkFailureReturnsBase) {
    Components::CommsManagerTester tester;
    tester.testDownlinkFailureReturnsBase();
}

TEST(Nominal, IgnoresPayloadStatusWhenInactive) {
    Components::CommsManagerTester tester;
    tester.testIgnoresPayloadStatusWhenInactive();
}

TEST(Nominal, AdapterStatusPollingAndRssiPing) {
    Components::CommsManagerTester tester;
    tester.testAdapterStatusPollingAndRssiPing();
}

TEST(Nominal, RunPublishesHealthFromLinkState) {
    Components::CommsManagerTester tester;
    tester.testRunPublishesHealthFromLinkState();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
