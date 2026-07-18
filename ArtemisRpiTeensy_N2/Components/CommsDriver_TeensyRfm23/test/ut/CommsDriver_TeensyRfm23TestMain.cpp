#include "CommsDriver_TeensyRfm23Tester.hpp"

TEST(Nominal, ParsesCorrelatedStatus) {
    Components::CommsDriver_TeensyRfm23Tester tester;
    tester.testParsesCorrelatedStatus();
}

TEST(Reliability, TargetErrorPreservesFactualOffState) {
    Components::CommsDriver_TeensyRfm23Tester tester;
    tester.testTargetErrorPreservesFactualOffState();
}

TEST(Reliability, RejectsStaleResponseAndTimesOutPendingRequest) {
    Components::CommsDriver_TeensyRfm23Tester tester;
    tester.testRejectsStaleResponseAndTimesOutPendingRequest();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
