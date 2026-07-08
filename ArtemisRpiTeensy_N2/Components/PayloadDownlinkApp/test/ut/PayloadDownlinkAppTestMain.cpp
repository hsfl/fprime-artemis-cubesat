#include "PayloadDownlinkAppTester.hpp"

TEST(Nominal, HeaderRetransmitBehavior) {
    Components::PayloadDownlinkAppTester tester;
    tester.testHeaderRetransmitBehavior();
}

TEST(Nominal, BurstCountSendsGeneratedPayloadPacketsPerRun) {
    Components::PayloadDownlinkAppTester tester;
    tester.testBurstCountSendsGeneratedPayloadPacketsPerRun();
}

TEST(Nominal, ProgressEventsEveryTenPercent) {
    Components::PayloadDownlinkAppTester tester;
    tester.testProgressEventsEveryTenPercent();
}

TEST(Nominal, RetryBurstCountSendsGeneratedRetryPacketsPerRun) {
    Components::PayloadDownlinkAppTester tester;
    tester.testRetryBurstCountSendsGeneratedRetryPacketsPerRun();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
