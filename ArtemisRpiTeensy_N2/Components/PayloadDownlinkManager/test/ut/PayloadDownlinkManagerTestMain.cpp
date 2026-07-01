#include "PayloadDownlinkManagerTester.hpp"

TEST(Nominal, FileBackedVariableLengthPackets) {
    Components::PayloadDownlinkManagerTester tester;
    tester.testFileBackedVariableLengthPackets();
}

TEST(Nominal, ProgressEventsEveryTenPercent) {
    Components::PayloadDownlinkManagerTester tester;
    tester.testProgressEventsEveryTenPercent();
}

TEST(Nominal, QueuesRetryPacketsForScheduledResend) {
    Components::PayloadDownlinkManagerTester tester;
    tester.testQueuesRetryPacketsForScheduledResend();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
