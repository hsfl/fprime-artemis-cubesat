#include "PayloadStreamAppTester.hpp"

TEST(Nominal, ResponsePacedUploadStartsNextFrameImmediately) {
    Components::PayloadStreamAppTester tester;
    tester.testResponsePacedUploadStartsNextFrameImmediately();
}

TEST(Nominal, TargetFailureDropsWithoutRetry) {
    Components::PayloadStreamAppTester tester;
    tester.testTargetFailureDropsWithoutRetry();
}

TEST(Nominal, LostResponseTimesOutAndAdvancesToNewFrame) {
    Components::PayloadStreamAppTester tester;
    tester.testLostResponseTimesOutAndAdvancesToNewFrame();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
