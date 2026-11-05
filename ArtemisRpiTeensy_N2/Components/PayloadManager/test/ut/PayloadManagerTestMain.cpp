#include "PayloadManagerTester.hpp"

TEST(CaptureGate, DirectCaptureCommandsRefusedWhileInhibited) {
    Components::PayloadManagerTester tester;
    tester.testDirectCaptureCommandsRefusedWhileInhibited();
}

TEST(CaptureGate, ScienceRequestRefusedWhileInhibited) {
    Components::PayloadManagerTester tester;
    tester.testScienceRequestRefusedWhileInhibited();
}

TEST(CaptureGate, PassthroughRefusedWhileInhibited) {
    Components::PayloadManagerTester tester;
    tester.testPassthroughRefusedWhileInhibited();
}

TEST(CaptureGate, CapturesResumeWhenEnabled) {
    Components::PayloadManagerTester tester;
    tester.testCapturesResumeWhenEnabled();
}

TEST(CaptureGate, RunningCaptureFinishesUnderInhibit) {
    Components::PayloadManagerTester tester;
    tester.testRunningCaptureFinishesUnderInhibit();
}

TEST(CaptureGate, RequestQueuedAfterInhibitIsRefused) {
    Components::PayloadManagerTester tester;
    tester.testRequestQueuedAfterInhibitIsRefused();
}

TEST(CaptureGate, CapturesStartBeforeAnyControlArrives) {
    Components::PayloadManagerTester tester;
    tester.testCapturesStartBeforeAnyControlArrives();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
