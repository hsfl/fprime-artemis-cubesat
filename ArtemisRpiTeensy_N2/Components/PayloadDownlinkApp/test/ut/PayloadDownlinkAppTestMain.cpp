#include "PayloadDownlinkAppTester.hpp"

TEST(Nominal, HeaderRetransmitBehavior) {
    Components::PayloadDownlinkAppTester tester;
    tester.testHeaderRetransmitBehavior();
}

TEST(Nominal, BurstCountSendsGeneratedPayloadPacketsPerRun) {
    Components::PayloadDownlinkAppTester tester;
    tester.testBurstCountSendsGeneratedPayloadPacketsPerRun();
}

TEST(Nominal, ProgressTelemetryAndExplicitStatus) {
    Components::PayloadDownlinkAppTester tester;
    tester.testProgressTelemetryAndExplicitStatus();
}

TEST(Nominal, RetryBurstCountSendsGeneratedRetryPacketsPerRun) {
    Components::PayloadDownlinkAppTester tester;
    tester.testRetryBurstCountSendsGeneratedRetryPacketsPerRun();
}

TEST(Nominal, ActiveRequestGuardPreservesTransferAndProgress) {
    Components::PayloadDownlinkAppTester tester;
    tester.testActiveRequestGuardPreservesTransferAndProgress();
}

TEST(Nominal, ControlMailboxCopiesInputAndReportsOverflow) {
    Components::PayloadDownlinkAppTester tester;
    tester.testControlMailboxCopiesInputAndReportsOverflow();
}

TEST(Nominal, RetryRequestsMergeAdditivelyAndIgnoreEmptyRequest) {
    Components::PayloadDownlinkAppTester tester;
    tester.testRetryRequestsMergeAdditivelyAndIgnoreEmptyRequest();
}

TEST(Nominal, AbortClearsPendingRepairAndRejectsLaterControl) {
    Components::PayloadDownlinkAppTester tester;
    tester.testAbortClearsPendingRepairAndRejectsLaterControl();
}

TEST(Reliability, LocalRetryPreservesNominalAndEndProgress) {
    Components::PayloadDownlinkAppTester tester;
    tester.testLocalRetryPreservesNominalAndEndProgress();
}

TEST(Reliability, RepairRetryPreservesCursor) {
    Components::PayloadDownlinkAppTester tester;
    tester.testRepairRetryPreservesCursor();
}

TEST(Reliability, LocalErrorFailsWithoutAdvance) {
    Components::PayloadDownlinkAppTester tester;
    tester.testLocalErrorFailsWithoutAdvance();
}

TEST(Reliability, InitializationFailurePublishesCorrelatedIdentity) {
    Components::PayloadDownlinkAppTester tester;
    tester.testInitializationFailurePublishesCorrelatedIdentity();
}

TEST(Reliability, RepairsDoNotStarveNominalProgress) {
    Components::PayloadDownlinkAppTester tester;
    tester.testRepairsDoNotStarveNominalProgress();
}

TEST(Reliability, MalformedControlIsRejectedWithoutPoisoningStatus) {
    Components::PayloadDownlinkAppTester tester;
    tester.testMalformedControlIsRejectedWithoutPoisoningStatus();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
